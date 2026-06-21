#include "device/ahci.h"
#include "device/pic.h"
#include "io.h"
#include "virtual.h"
#include "kernel.h"

// ── External PCI config helpers (defined in pci.cpp) ─────────────────────
extern pt::uint32_t pciConfigReadDWord(pt::uint8_t bus, pt::uint8_t slot,
                                       pt::uint8_t func, pt::uint8_t offset);
extern void pciConfigWriteDWord(pt::uint8_t bus, pt::uint8_t slot,
                                pt::uint8_t func, pt::uint8_t offset,
                                pt::uint32_t value);

// ── Static member storage ────────────────────────────────────────────────
bool         AHCI::initialized  = false;
bool         AHCI::present      = false;
pt::uint16_t AHCI::pci_bus      = 0;
pt::uint8_t  AHCI::pci_dev      = 0;
volatile pt::uint32_t* AHCI::abar     = nullptr;
pt::uintptr_t          AHCI::abar_phys = 0;
pt::uintptr_t          AHCI::abar_size = 0;

AHCI::AHCI_PORT_STATE AHCI::ports[AHCI_MAX_PORTS] = {};
AHCIDrive       AHCI::drives[AHCI_MAX_DRIVES]      = {};
pt::uint8_t     AHCI::drive_count                  = 0;
pt::uint64_t    AHCI::command_count                = 0;

// ── PCI config helpers ───────────────────────────────────────────────────
pt::uint32_t AHCI::pci_read_dword(pt::uint8_t offset) {
    return pciConfigReadDWord(static_cast<pt::uint8_t>(pci_bus), pci_dev, 0, offset);
}

void AHCI::pci_write_dword(pt::uint8_t offset, pt::uint32_t value) {
    pciConfigWriteDWord(static_cast<pt::uint8_t>(pci_bus), pci_dev, 0, offset, value);
}

// ── MMIO register accessors ──────────────────────────────────────────────
pt::uint32_t AHCI::ghc_read(pt::uint8_t reg) {
    return abar[reg / 4];
}

void AHCI::ghc_write(pt::uint8_t reg, pt::uint32_t value) {
    abar[reg / 4] = value;
}

pt::uint32_t AHCI::port_read(pt::uint8_t port, pt::uint8_t reg) {
    const pt::uintptr_t offset = PORT_BASE + port * PORT_OFFSET + reg;
    return abar[offset / 4];
}

void AHCI::port_write(pt::uint8_t port, pt::uint8_t reg, pt::uint32_t value) {
    const pt::uintptr_t offset = PORT_BASE + port * PORT_OFFSET + reg;
    abar[offset / 4] = value;
}

// ── reset_hba: global HBA reset ──────────────────────────────────────────
bool AHCI::reset_hba() {
    klog("[AHCI] Resetting HBA...\n");

    // Set HBA Reset bit
    ghc_write(AHCI_GHC, GHC_HR);

    // Wait for HR to clear (hardware completes reset)
    for (pt::uint32_t t = 0; t < 1000000; t++) {
        if (!(ghc_read(AHCI_GHC) & GHC_HR))
            return true;
        IO::io_wait();
    }

    klog("[AHCI] HBA reset timeout\n");
    return false;
}

// ── stop_port: stop command and FIS receive engines ─────────────────────
void AHCI::stop_port(pt::uint8_t port) {
    // Clear ST (Start) bit
    pt::uint32_t cmd = port_read(port, PORT_PxCMD);
    if (cmd & PxCMD_ST) {
        port_write(port, PORT_PxCMD, cmd & ~PxCMD_ST);
        // Wait for CR (Command Running) to clear
        for (int t = 0; t < 100000; t++) {
            if (!(port_read(port, PORT_PxCMD) & PxCMD_CR))
                break;
            IO::io_wait();
        }
    }

    // Clear FRE (FIS Receive Enable)
    cmd = port_read(port, PORT_PxCMD);
    if (cmd & PxCMD_FRE) {
        port_write(port, PORT_PxCMD, cmd & ~PxCMD_FRE);
        for (int t = 0; t < 100000; t++) {
            if (!(port_read(port, PORT_PxCMD) & PxCMD_FR))
                break;
            IO::io_wait();
        }
    }
}

// ── start_port: start command and FIS receive engines ────────────────────
bool AHCI::start_port(pt::uint8_t port) {
    // Enable FIS receive
    pt::uint32_t cmd = port_read(port, PORT_PxCMD);
    port_write(port, PORT_PxCMD, cmd | PxCMD_FRE);

    // Wait for FR (FIS Receive Running)
    for (int t = 0; t < 100000; t++) {
        if (port_read(port, PORT_PxCMD) & PxCMD_FR)
            break;
        IO::io_wait();
        if (t == 99999) {
            klog("[AHCI] Port %d: FRE timeout\n", port);
            return false;
        }
    }

    // Start command list processing
    cmd = port_read(port, PORT_PxCMD);
    port_write(port, PORT_PxCMD, cmd | PxCMD_ST);

    // Wait for CR (Command List Running)
    for (int t = 0; t < 100000; t++) {
        if (port_read(port, PORT_PxCMD) & PxCMD_CR)
            return true;
        IO::io_wait();
        if (t == 99999) {
            klog("[AHCI] Port %d: ST timeout\n", port);
            return false;
        }
    }
    return false;
}

// ── recover_port: bring a halted/errored port back to life ───────────────
// When the HBA hits a fatal error (bad DMA descriptor, device error, …) it
// halts the port command engine (PxCMD.CR clears) and refuses further
// commands.  Standard recovery: stop the engines, clear the latched SATA
// error and interrupt status, then restart.  Without this, a single failed
// command wedges the disk permanently.
void AHCI::recover_port(pt::uint8_t port) {
    stop_port(port);
    port_write(port, PORT_PxSERR, 0xFFFFFFFF);  // W1C all error bits
    port_write(port, PORT_PxIS,   0xFFFFFFFF);  // W1C all interrupt status
    start_port(port);
}

// ── dump_state: capture every boundary that could explain a silent wedge ──
// A timeout with no error bits (TFD ERR=0, IS=0, SERR=0) but CI stuck means
// the HBA accepted the command yet never DMA'd it.  The candidates are:
//   • PCI Bus Master Enable cleared  → cmd_sts bit2 == 0  (QEMU silently drops
//     all DMA, including the command-table fetch, with no AHCI error bit)
//   • GHC.AE cleared / HBA reset     → ghc bit31 == 0
//   • SATA link dropped              → PxSSTS DET != 3
//   • Command header corrupted       → opts/ctba not what we programmed
// Printing all four lets the next wedge name the failing boundary directly.
void AHCI::dump_state(pt::uint8_t port, pt::uint8_t slot, const char* who) {
    pt::uint32_t cmd_sts = pci_read_dword(0x04);
    pt::uint32_t ghc     = ghc_read(AHCI_GHC);
    pt::uint32_t gis     = ghc_read(AHCI_IS);

    klog("[AHCI] === %s wedge: full state (port=%d slot=%d) ===\n", who, port, slot);
    klog("[AHCI]  PCI cmd/sts=%x  BusMaster=%d MMIO=%d IO=%d\n",
         cmd_sts, (cmd_sts >> 2) & 1, (cmd_sts >> 1) & 1, cmd_sts & 1);
    klog("[AHCI]  GHC=%x (AE=%d IE=%d)  global IS=%x\n",
         ghc, (ghc >> 31) & 1, (ghc >> 1) & 1, gis);
    klog("[AHCI]  PxSSTS=%x (DET=%d)  PxSCTL=%x  PxTFD=%x  PxSERR=%x\n",
         port_read(port, PORT_PxSSTS), port_read(port, PORT_PxSSTS) & 0xF,
         port_read(port, PORT_PxSCTL), port_read(port, PORT_PxTFD),
         port_read(port, PORT_PxSERR));
    klog("[AHCI]  PxCMD=%x  PxCI=%x  PxIS=%x  PxIE=%x\n",
         port_read(port, PORT_PxCMD), port_read(port, PORT_PxCI),
         port_read(port, PORT_PxIS), port_read(port, PORT_PxIE));

    // Command header as the HBA would read it (from our virtual mapping of the
    // command-list frame).  opts should be CFL(5)|PRDTL(n)|maybe WRITE; ctba
    // should point inside our ct_area_phys.  Garbage here == memory corruption.
    if (ports[port].valid) {
        HBA_CMD_HEADER* h = &ports[port].cmd_list[slot];
        klog("[AHCI]  CMD_HDR opts=%x prdbc=%x ctba=%x (expect ctba in ct_area_phys=%x)\n",
             h->opts, h->prdbc, h->ctba,
             static_cast<pt::uint32_t>(ports[port].ct_area_phys));
        // First PRDT entry the HBA would walk.
        auto* prdt = reinterpret_cast<HBA_PRDT_ENTRY*>(
            ports[port].ct_area + slot * AHCI_SLOT_SIZE + 0x80);
        klog("[AHCI]  PRDT0 dba=%x dbau=%x dbc=%x\n",
             prdt[0].dba, prdt[0].dbau, prdt[0].dbc);
    }
    klog("[AHCI] === end state dump ===\n");
}

// ── find_free_slot: find a command slot not in use ───────────────────────
pt::uint8_t AHCI::find_free_slot(pt::uint8_t port) {
    // PxCI has bit N set when slot N is in use.  Only the first AHCI_CT_SLOTS
    // slots have a command table backing them, so never return beyond that.
    pt::uint32_t ci = port_read(port, PORT_PxCI);
    for (pt::uint8_t i = 0; i < AHCI_CT_SLOTS; i++) {
        if (!(ci & (1u << i)))
            return i;
    }
    return 0xFF;  // all slots busy
}

// ── calc_prdt_entries: how many PRD entries for a buffer ─────────────────
// Each PRD entry must not cross a 4K boundary.  Returns the TRUE number of
// entries required (not clamped) so the caller can reject transfers that
// would overflow the command table's PRDT area (AHCI_PRDT_MAX entries).
pt::uint8_t AHCI::calc_prdt_entries(pt::uintptr_t data_phys,
                                    pt::uint32_t byte_count) {
    pt::uint8_t count = 0;
    pt::uint32_t remaining = byte_count;
    pt::uintptr_t addr = data_phys;

    while (remaining > 0) {
        count++;
        pt::uint32_t page_offset = static_cast<pt::uint32_t>(addr & 0xFFF);
        pt::uint32_t space_in_page = 4096 - page_offset;
        if (remaining <= space_in_page)
            break;
        remaining -= space_in_page;
        addr += space_in_page;
    }
    if (count == 0) count = 1;
    return count;
}

// ── init_port: initialize a single port and identify the drive ───────────
bool AHCI::init_port(pt::uint8_t port) {
    klog("[AHCI] Initializing port %d\n", port);

    // Check device detection.  Note: PxSIG is NOT valid yet — the device only
    // latches its signature into PxSIG after it delivers its first D2H FIS,
    // which requires FRE+ST running.  So the signature check is deferred until
    // after start_port() below.
    pt::uint32_t ssts = port_read(port, PORT_PxSSTS);
    if ((ssts & SSTS_DET) != DET_ESTABLISHED) {
        klog("[AHCI] Port %d: no device (SSTS=%x)\n", port, ssts);
        return false;
    }

    // Stop port engines before programming DMA descriptors
    stop_port(port);

    // ── Allocate Command List (must be 1024-byte aligned, use 4K frame) ──
    pt::uintptr_t cl_phys = vmm.allocate_frame();
    auto* cl_virt = reinterpret_cast<HBA_CMD_HEADER*>(cl_phys + KERNEL_OFFSET);
    for (pt::size_t i = 0; i < 4096; i++)
        reinterpret_cast<pt::uint8_t*>(cl_virt)[i] = 0;

    port_write(port, PORT_PxCLB,  static_cast<pt::uint32_t>(cl_phys & 0xFFFFFFFF));
    port_write(port, PORT_PxCLBU, static_cast<pt::uint32_t>((cl_phys >> 32) & 0xFFFFFFFF));

    // ── Allocate Received FIS (must be 256-byte aligned) ─────────────────
    pt::uintptr_t fis_phys = vmm.allocate_frame();
    auto* fis_virt = reinterpret_cast<pt::uint8_t*>(fis_phys + KERNEL_OFFSET);
    for (pt::size_t i = 0; i < 4096; i++)
        fis_virt[i] = 0;

    port_write(port, PORT_PxFB,  static_cast<pt::uint32_t>(fis_phys & 0xFFFFFFFF));
    port_write(port, PORT_PxFBU, static_cast<pt::uint32_t>((fis_phys >> 32) & 0xFFFFFFFF));

    // ── Allocate Command Table area (single 4K frame) ────────────────────
    // Each slot gets AHCI_SLOT_SIZE bytes: 128 for CFIS/ACMD/reserved + room
    // for 8 PRDT entries (8 × 16).  4096 / 256 = AHCI_CT_SLOTS slots, all
    // within one physically-contiguous frame (consecutive allocate_frame()
    // calls are NOT guaranteed contiguous, so a single frame is mandatory).
    pt::uintptr_t ct_phys = vmm.allocate_frame();
    auto* ct_virt = reinterpret_cast<pt::uint8_t*>(ct_phys + KERNEL_OFFSET);
    for (pt::size_t i = 0; i < 4096; i++)
        ct_virt[i] = 0;

    // Link each command header to its command table slot
    for (pt::uint8_t s = 0; s < AHCI_CT_SLOTS; s++) {
        pt::uintptr_t slot_ct_phys = ct_phys + s * AHCI_SLOT_SIZE;
        cl_virt[s].ctba  = static_cast<pt::uint32_t>(slot_ct_phys & 0xFFFFFFFF);
        cl_virt[s].ctbau = static_cast<pt::uint32_t>((slot_ct_phys >> 32) & 0xFFFFFFFF);
    }

    // Store port state
    ports[port].valid = true;
    ports[port].cmd_list = cl_virt;
    ports[port].cmd_list_phys = cl_phys;
    ports[port].rx_fis = fis_virt;
    ports[port].rx_fis_phys = fis_phys;
    ports[port].ct_area = ct_virt;
    ports[port].ct_area_phys = ct_phys;

    // Clear any latched SATA errors before bringing the engines up.
    port_write(port, PORT_PxSERR, 0xFFFFFFFF);

    // Memory fence before starting
    asm volatile("mfence" ::: "memory");

    // Helper lambda for the common teardown-on-failure path.
    auto release = [&]() {
        stop_port(port);
        vmm.free_frame(cl_phys);
        vmm.free_frame(fis_phys);
        vmm.free_frame(ct_phys);
        ports[port] = AHCI_PORT_STATE{};
    };

    // Start port engines
    if (!start_port(port)) {
        klog("[AHCI] Port %d: failed to start\n", port);
        release();
        return false;
    }

    // Wait for the device to become ready (PxTFD.BSY and DRQ clear) — only
    // then has the initial D2H FIS arrived and PxSIG/identify are usable.
    constexpr pt::uint32_t TFD_BSY = 0x80;  // task file busy
    constexpr pt::uint32_t TFD_DRQ = 0x08;  // data request
    bool ready = false;
    for (pt::uint32_t t = 0; t < AHCI_TIMEOUT; t++) {
        if (!(port_read(port, PORT_PxTFD) & (TFD_BSY | TFD_DRQ))) {
            ready = true;
            break;
        }
        IO::io_wait();
    }
    if (!ready) {
        klog("[AHCI] Port %d: device not ready (TFD=%x)\n",
             port, port_read(port, PORT_PxTFD));
        release();
        return false;
    }

    // Now the signature is valid — we only support plain SATA disks.
    pt::uint32_t sig = port_read(port, PORT_PxSIG);
    if (sig != SIG_SATA) {
        klog("[AHCI] Port %d: non-SATA signature %x\n", port, sig);
        release();
        return false;
    }

    // Enable interrupts on this port
    port_write(port, PORT_PxIE, PxIS_DHRS | PxIS_PCS | PxIS_PRCS |
                                PxIS_OF | PxIS_INFS | PxIS_IFS | PxIS_HBDS |
                                PxIS_DPS | PxIS_UFS | PxIS_SDBS | PxIS_DSS |
                                PxIS_PSS);

    return true;
}

// ── identify_drive: issue IDENTIFY DEVICE and populate drive info ────────
bool AHCI::identify_drive(pt::uint8_t port, pt::uint8_t drive_idx) {
    if (!ports[port].valid) return false;

    // Allocate a 512-byte aligned buffer for IDENTIFY data
    // Use a page-aligned frame to avoid crossing 4K boundaries
    pt::uintptr_t id_phys = vmm.allocate_frame();
    auto* id_data = reinterpret_cast<pt::uint16_t*>(id_phys + KERNEL_OFFSET);
    for (pt::size_t i = 0; i < 512; i++)
        reinterpret_cast<pt::uint8_t*>(id_data)[i] = 0;

    pt::uint8_t slot = find_free_slot(port);
    if (slot == 0xFF) {
        klog("[AHCI] No free slot for IDENTIFY\n");
        vmm.free_frame(id_phys);
        return false;
    }

    HBA_CMD_HEADER* cmd_hdr = &ports[port].cmd_list[slot];
    pt::uint8_t* ct = ports[port].ct_area + slot * AHCI_SLOT_SIZE;

    // Build command header
    // CFL = 5 DWORDs (20 bytes for Register H2D FIS), 1 PRD entry, read
    cmd_hdr->opts  = AHCI_CMD_CFL(5) | AHCI_CMD_PRDTL(1);
    cmd_hdr->prdbc = 0;

    // Build PRDT entry (single, no 4K crossing since buffer is page-aligned)
    auto* prdt = reinterpret_cast<HBA_PRDT_ENTRY*>(ct + 0x80);
    prdt[0].dba = static_cast<pt::uint32_t>(id_phys & 0xFFFFFFFF);
    prdt[0].dbau = static_cast<pt::uint32_t>((id_phys >> 32) & 0xFFFFFFFF);
    prdt[0].dbc = (SATA_SECTOR_SIZE - 1) & PRDT_DBC_MASK;  // count-1
    prdt[0].reserved = 0;

    // Build Register H2D FIS
    auto* fis = reinterpret_cast<FIS_H2D*>(ct);
    fis->fis_type    = FIS_TYPE_H2D;
    fis->pmport      = 0x80;  // bit7 = C (Command); without it the device ignores the FIS
    fis->command     = ATA_CMD_IDENTIFY;
    fis->features_low = 0;
    fis->lba0 = fis->lba1 = fis->lba2 = 0;
    fis->device      = 0;  // non-LBA addressing for IDENTIFY
    fis->lba3 = fis->lba4 = fis->lba5 = 0;
    fis->features_high = 0;
    fis->count_low   = 0;
    fis->count_high  = 0;
    fis->icc         = 0;
    fis->control     = 0;

    // Clear pending IS bits
    port_write(port, PORT_PxIS, 0xFFFFFFFF);

    asm volatile("mfence" ::: "memory");

    // Issue command
    port_write(port, PORT_PxCI, (1u << slot));

    // Wait for completion (poll PxCI bit to clear)
    bool timed_out = true;
    for (pt::uint32_t t = 0; t < AHCI_TIMEOUT; t++) {
        if (!(port_read(port, PORT_PxCI) & (1u << slot))) {
            timed_out = false;
            break;
        }
        IO::io_wait();
    }

    if (timed_out) {
        klog("[AHCI] Port %d: IDENTIFY timeout\n", port);
        vmm.free_frame(id_phys);
        return false;
    }

    // Check for error
    if (port_read(port, PORT_PxTFD) & 0x80) {  // ERR bit
        klog("[AHCI] Port %d: IDENTIFY error (TFD=%x)\n",
             port, port_read(port, PORT_PxTFD));
        vmm.free_frame(id_phys);
        return false;
    }

    // Parse IDENTIFY data
    pt::uint32_t sector_count =
        static_cast<pt::uint32_t>(id_data[60]) |
        (static_cast<pt::uint32_t>(id_data[61]) << 16);

    bool lba48 = false;
    if (id_data[83] & (1 << 10)) {  // word 83 bit 10 = LBA48 supported
        lba48 = true;
        // 48-bit sector count is in words 100-103
        sector_count =
            static_cast<pt::uint32_t>(id_data[100]) |
            (static_cast<pt::uint32_t>(id_data[101]) << 16);
        // Upper 32 bits (words 102-103) — ignore for now (< 2TB)
    }

    drives[drive_idx].present = true;
    drives[drive_idx].port = port;
    drives[drive_idx].sector_count = sector_count;
    drives[drive_idx].lba48 = lba48;

    // Model string (words 27-46, 20 words = 40 bytes, byte-swapped)
    for (int i = 0; i < 20; i++) {
        pt::uint16_t w = id_data[27 + i];
        drives[drive_idx].model[i * 2]     = static_cast<char>(w >> 8);
        drives[drive_idx].model[i * 2 + 1] = static_cast<char>(w & 0xFF);
    }
    drives[drive_idx].model[40] = '\0';
    // Trim trailing spaces
    for (int i = 39; i >= 0 && drives[drive_idx].model[i] == ' '; i--)
        drives[drive_idx].model[i] = '\0';

    klog("[AHCI] Port %d: drive %d: %s, %d sectors%s\n",
         port, drive_idx, drives[drive_idx].model,
         sector_count, lba48 ? " (LBA48)" : "");

    vmm.free_frame(id_phys);
    return true;
}

// ── issue_command: generic DMA command ───────────────────────────────────
bool AHCI::issue_command(pt::uint8_t port, pt::uint8_t slot,
                         pt::uint8_t cmd, pt::uint64_t lba,
                         pt::uint16_t sector_count,
                         pt::uintptr_t data_phys, bool is_write,
                         pt::uint8_t prd_count, bool lba48) {
    HBA_CMD_HEADER* cmd_hdr = &ports[port].cmd_list[slot];
    pt::uint8_t* ct = ports[port].ct_area + slot * AHCI_SLOT_SIZE;

    // Zero the command table region (CFIS + PRDT area)
    for (pt::size_t i = 0; i < AHCI_SLOT_SIZE; i++)
        ct[i] = 0;

    // Build command header: CFL=5 DWORDs, write flag if needed, PRDTL
    pt::uint32_t opts = AHCI_CMD_CFL(5) | AHCI_CMD_PRDTL(prd_count);
    if (is_write)
        opts |= AHCI_CMD_WRITE;
    cmd_hdr->opts  = opts;
    cmd_hdr->prdbc = 0;

    // Build PRDT entries
    auto* prdt = reinterpret_cast<HBA_PRDT_ENTRY*>(ct + 0x80);
    pt::uintptr_t addr = data_phys;
    pt::uint32_t remaining = static_cast<pt::uint32_t>(sector_count) * SATA_SECTOR_SIZE;

    for (pt::uint8_t p = 0; p < prd_count; p++) {
        pt::uint32_t page_offset = static_cast<pt::uint32_t>(addr & 0xFFF);
        pt::uint32_t chunk = 4096 - page_offset;
        if (chunk > remaining) chunk = remaining;

        prdt[p].dba = static_cast<pt::uint32_t>(addr & 0xFFFFFFFF);
        prdt[p].dbau = static_cast<pt::uint32_t>((addr >> 32) & 0xFFFFFFFF);
        prdt[p].dbc = (chunk - 1) & PRDT_DBC_MASK;
        prdt[p].reserved = 0;

        remaining -= chunk;
        addr += chunk;
    }

    // Build Register H2D FIS
    auto* fis = reinterpret_cast<FIS_H2D*>(ct);
    fis->fis_type = FIS_TYPE_H2D;
    fis->pmport   = 0x80;  // bit7 = C (Command); without it the device ignores the FIS
    fis->command  = cmd;
    fis->features_low = 0;
    fis->features_high = 0;
    fis->icc      = 0;
    fis->control  = 0;

    if (lba48) {
        // 48-bit LBA
        fis->device      = 0x40;  // LBA mode bit
        fis->lba0        = static_cast<pt::uint8_t>(lba & 0xFF);
        fis->lba1        = static_cast<pt::uint8_t>((lba >> 8) & 0xFF);
        fis->lba2        = static_cast<pt::uint8_t>((lba >> 16) & 0xFF);
        fis->lba3        = static_cast<pt::uint8_t>((lba >> 24) & 0xFF);
        fis->lba4        = static_cast<pt::uint8_t>((lba >> 32) & 0xFF);
        fis->lba5        = static_cast<pt::uint8_t>((lba >> 40) & 0xFF);
        fis->count_low   = static_cast<pt::uint8_t>(sector_count & 0xFF);
        fis->count_high  = static_cast<pt::uint8_t>((sector_count >> 8) & 0xFF);
    } else {
        // 28-bit LBA
        fis->device      = 0x40 | static_cast<pt::uint8_t>((lba >> 24) & 0x0F);
        fis->lba0        = static_cast<pt::uint8_t>(lba & 0xFF);
        fis->lba1        = static_cast<pt::uint8_t>((lba >> 8) & 0xFF);
        fis->lba2        = static_cast<pt::uint8_t>((lba >> 16) & 0xFF);
        fis->lba3 = fis->lba4 = fis->lba5 = 0;
        fis->count_low   = static_cast<pt::uint8_t>(sector_count & 0xFF);
        fis->count_high  = 0;
    }

    // Clear pending IS bits
    port_write(port, PORT_PxIS, 0xFFFFFFFF);

    asm volatile("mfence" ::: "memory");

    // Issue command
    command_count++;
    port_write(port, PORT_PxCI, (1u << slot));

    return true;
}

pt::uint64_t AHCI::get_command_count() {
    return command_count;
}

// ── initialize: find AHCI controller, init all ports, identify drives ───
bool AHCI::initialize() {
    if (initialized) return present;
    initialized = true;

    klog("[AHCI] Probing PCI...\n");

    // ── Find AHCI controller on PCI bus ──────────────────────────────
    bool found = false;
    pci_device* devices = pci::enumerate();
    pci_device* d = devices;

    while (d && d->vendor_id != 0xFFFF) {
        klog("[AHCI] PCI %d:%d vendor=%x device=%x class=%x subclass=%x\n",
             d->bus, d->device, d->vendor_id, d->device_id,
             d->class_code, d->subclass_code);
        if (d->class_code == AHCI_CLASS &&
            d->subclass_code == AHCI_SUBCLASS) {
            pci_bus = static_cast<pt::uint16_t>(d->bus);
            pci_dev = d->device;
            found = true;
            klog("[AHCI] AHCI controller at bus=%d dev=%d (vendor=%x device=%x)\n",
                 d->bus, d->device, d->vendor_id, d->device_id);
            break;
        }
        d++;
    }
    vmm.kfree(devices);

    if (!found) {
        klog("[AHCI] No AHCI controller found\n");
        return false;
    }

    // ── Read BAR5 (ABAR — AHCI Base Address Register) ────────────────
    // ABAR is a memory BAR.  Bit0=0 (memory), bits[2:1] give the width
    // (00 = 32-bit, 10 = 64-bit), bit3 = prefetchable.  Per the AHCI spec
    // ABAR is a 32-bit non-prefetchable MMIO BAR (low nibble 0x0), but we
    // decode generically so a 64-bit BAR is handled too.
    pt::uint32_t bar5_low  = pci_read_dword(0x24);

    if (bar5_low & 0x1) {
        klog("[AHCI] BAR5 is an I/O BAR (%x), not supported\n", bar5_low);
        return false;
    }

    pt::uint8_t mem_type = static_cast<pt::uint8_t>((bar5_low >> 1) & 0x3);
    abar_phys = bar5_low & 0xFFFFFFF0;
    if (mem_type == 0x2) {
        // 64-bit memory BAR — high half is in the next dword (offset 0x28).
        pt::uint32_t bar5_high = pci_read_dword(0x28);
        abar_phys |= (static_cast<pt::uintptr_t>(bar5_high) << 32);
    }

    // Map enough of the register space to cover all 32 possible ports:
    //   PORT_BASE + 32 * PORT_OFFSET = 0x100 + 0x1000 = 0x1100 → 2 pages.
    abar_size = (PORT_BASE + AHCI_MAX_PORTS * PORT_OFFSET + 0xFFF) & ~0xFFFu;

    klog("[AHCI] ABAR phys=%lx size=%x\n", abar_phys, abar_size);

    // ── Enable PCI I/O space + Bus Master + MMIO ─────────────────────
    pt::uint32_t cmd_sts = pci_read_dword(0x04);
    pt::uint16_t cmd_val = static_cast<pt::uint16_t>(cmd_sts & 0xFFFF);
    cmd_val |= 0x07;  // bit0=IO, bit1=MMIO, bit2=BusMaster
    pci_write_dword(0x04, (cmd_sts & 0xFFFF0000u) | cmd_val);

    // ── Map ABAR into virtual address space (uncacheable MMIO) ───────
    // PCD|PWT (0x18) marks the pages cache-disabled; HBA registers must not
    // be cached or write-combined or reads/writes could be reordered/stale.
    for (pt::size_t offset = 0; offset < abar_size; offset += 0x1000) {
        vmm.map_page(KERNEL_OFFSET + abar_phys + offset,
                     abar_phys + offset,
                     0x1B);  // present + writable + PWT + PCD
    }

    abar = reinterpret_cast<volatile pt::uint32_t*>(KERNEL_OFFSET + abar_phys);
    klog("[AHCI] ABAR mapped at %p\n", abar);

    // ── Global HBA reset ─────────────────────────────────────────────
    if (!reset_hba()) {
        klog("[AHCI] HBA reset failed\n");
        return false;
    }

    // ── Enable AHCI mode ─────────────────────────────────────────────
    // GHC.AE must be re-asserted after a reset.  GHC.IE is deliberately left
    // OFF: this driver detects completion by polling PxCI, so global
    // interrupts are unwanted — with IE off the HBA never asserts IRQ11 and
    // AHCI::check_irq() stays a harmless no-op on the shared line.
    ghc_write(AHCI_GHC, ghc_read(AHCI_GHC) | GHC_AE);

    // ── Read capabilities (from MMIO, not PCI config) ────────────────
    pt::uint32_t cap = ghc_read(AHCI_CAP);
    pt::uint8_t np = static_cast<pt::uint8_t>((cap & CAP_NP) + 1);

    // ── Read Ports Implemented ───────────────────────────────────────
    pt::uint32_t pi = ghc_read(AHCI_PI);
    klog("[AHCI] CAP=%x NP=%d Ports Implemented mask: %x\n", cap, np, pi);

    // ── Initialize each implemented port with a device ───────────────
    // PI (Ports Implemented) is the authoritative source; scan all 32.
    drive_count = 0;
    for (pt::uint8_t p = 0; p < AHCI_MAX_PORTS; p++) {
        if (!(pi & (1u << p)))
            continue;

        if (!init_port(p)) {
            klog("[AHCI] Port %d skipped\n", p);
            continue;
        }

        if (drive_count < AHCI_MAX_DRIVES &&
            identify_drive(p, drive_count)) {
            drive_count++;
        } else {
            // Port came up but has no usable drive (or the drive table is
            // full): tear the port back down so it doesn't masquerade as a
            // valid drive-bearing port in read/write_sectors.
            klog("[AHCI] Port %d: no usable drive, releasing\n", p);
            stop_port(p);
            vmm.free_frame(ports[p].cmd_list_phys);
            vmm.free_frame(ports[p].rx_fis_phys);
            vmm.free_frame(ports[p].ct_area_phys);
            ports[p] = AHCI_PORT_STATE{};
        }
    }

    if (drive_count == 0) {
        klog("[AHCI] No drives found\n");
        return false;
    }

    present = true;

    // Make the shared IRQ line explicit.  The completion path polls PxCI, so
    // this is belt-and-suspenders, but it documents that AHCI shares IRQ11
    // (with RTL8139) and keeps the line unmasked.
    pt::uint8_t irq_line = static_cast<pt::uint8_t>(pci_read_dword(0x3C) & 0xFF);
    klog("[AHCI] PCI interrupt line: IRQ %d (handler wired on IRQ11)\n", irq_line);
    PIC::unmask_irq(11);

    klog("[AHCI] Ready: %d drive(s)\n", drive_count);
    return true;
}

// ── read_sectors ─────────────────────────────────────────────────────────
bool AHCI::read_sectors(pt::uint8_t drive, pt::uint32_t lba,
                        pt::uint8_t count, void* buffer) {
    if (!present || drive >= drive_count || !drives[drive].present)
        return false;
    if (count == 0 || buffer == nullptr)
        return false;

    pt::uint8_t port = drives[drive].port;
    if (port >= AHCI_MAX_PORTS || !ports[port].valid)
        return false;

    pt::uintptr_t data_phys = VMM::virt_to_phys(buffer);
    pt::uint32_t byte_count = static_cast<pt::uint32_t>(count) * SATA_SECTOR_SIZE;
    pt::uint8_t prd_count = calc_prdt_entries(data_phys, byte_count);
    if (prd_count > AHCI_PRDT_MAX) {
        klog("[AHCI] Read too large: %d sectors need %d PRD entries (max %d)\n",
             count, prd_count, AHCI_PRDT_MAX);
        return false;
    }

    pt::uint8_t slot = find_free_slot(port);
    if (slot == 0xFF) {
        klog("[AHCI] No free slot for read\n");
        return false;
    }

    bool lba48 = drives[drive].lba48;
    pt::uint8_t cmd = lba48 ? ATA_CMD_READ_DMA_EXT : ATA_CMD_READ_DMA;

    issue_command(port, slot, cmd,
                  static_cast<pt::uint64_t>(lba), count,
                  data_phys, false, prd_count, lba48);

    // Wait for completion
    for (pt::uint32_t t = 0; t < AHCI_TIMEOUT; t++) {
        if (!(port_read(port, PORT_PxCI) & (1u << slot)))
            return true;  // completed
        IO::io_wait();
    }

    klog("[AHCI] Read timeout (drive=%d lba=%d count=%d)\n", drive, lba, count);
    klog("[AHCI]  TFD=%x IS=%x SERR=%x CMD=%x CI=%x CLB=%x prdbc=%x\n",
         port_read(port, PORT_PxTFD), port_read(port, PORT_PxIS),
         port_read(port, PORT_PxSERR), port_read(port, PORT_PxCMD),
         port_read(port, PORT_PxCI), port_read(port, PORT_PxCLB),
         ports[port].cmd_list[slot].prdbc);
    klog("[AHCI]  our CLB_phys=%x data_phys=%x prd_count=%d\n",
         static_cast<pt::uint32_t>(ports[port].cmd_list_phys),
         static_cast<pt::uint32_t>(data_phys), prd_count);
    dump_state(port, slot, "read");
    recover_port(port);  // un-wedge the port so future I/O can succeed
    return false;
}

// ── write_sectors ────────────────────────────────────────────────────────
bool AHCI::write_sectors(pt::uint8_t drive, pt::uint32_t lba,
                         pt::uint8_t count, const void* buffer) {
    if (!present || drive >= drive_count || !drives[drive].present)
        return false;
    if (count == 0 || buffer == nullptr)
        return false;

    pt::uint8_t port = drives[drive].port;
    if (port >= AHCI_MAX_PORTS || !ports[port].valid)
        return false;

    pt::uintptr_t data_phys = VMM::virt_to_phys(const_cast<void*>(buffer));
    pt::uint32_t byte_count = static_cast<pt::uint32_t>(count) * SATA_SECTOR_SIZE;
    pt::uint8_t prd_count = calc_prdt_entries(data_phys, byte_count);
    if (prd_count > AHCI_PRDT_MAX) {
        klog("[AHCI] Write too large: %d sectors need %d PRD entries (max %d)\n",
             count, prd_count, AHCI_PRDT_MAX);
        return false;
    }

    pt::uint8_t slot = find_free_slot(port);
    if (slot == 0xFF) {
        klog("[AHCI] No free slot for write\n");
        return false;
    }

    bool lba48 = drives[drive].lba48;
    pt::uint8_t cmd = lba48 ? ATA_CMD_WRITE_DMA_EXT : ATA_CMD_WRITE_DMA;

    issue_command(port, slot, cmd,
                  static_cast<pt::uint64_t>(lba), count,
                  data_phys, true, prd_count, lba48);

    for (pt::uint32_t t = 0; t < AHCI_TIMEOUT; t++) {
        if (!(port_read(port, PORT_PxCI) & (1u << slot)))
            return true;
        IO::io_wait();
    }

    klog("[AHCI] Write timeout (drive=%d lba=%d count=%d)\n", drive, lba, count);
    klog("[AHCI]  TFD=%x IS=%x SERR=%x CMD=%x CI=%x prdbc=%x data_phys=%x\n",
         port_read(port, PORT_PxTFD), port_read(port, PORT_PxIS),
         port_read(port, PORT_PxSERR), port_read(port, PORT_PxCMD),
         port_read(port, PORT_PxCI), ports[port].cmd_list[slot].prdbc,
         static_cast<pt::uint32_t>(data_phys));
    dump_state(port, slot, "write");
    recover_port(port);  // un-wedge the port so future I/O can succeed
    return false;
}

// ── check_irq: called from shared IRQ handler ────────────────────────────
// Returns true if this HBA raised the interrupt.
bool AHCI::check_irq() {
    if (!present) return false;

    pt::uint32_t is = ghc_read(AHCI_IS);
    if (is == 0) return false;

    // Per the AHCI spec the per-port PxIS must be cleared BEFORE the global
    // IS bit, otherwise a completion that races the clear can be lost.
    for (pt::uint8_t p = 0; p < AHCI_MAX_PORTS; p++) {
        if (!(is & (1u << p)))
            continue;
        if (!ports[p].valid)
            continue;

        pt::uint32_t pis = port_read(p, PORT_PxIS);
        if (pis) {
            // W1C port interrupt status.  Completion itself is detected by
            // polling PxCI; here we only acknowledge the line.
            port_write(p, PORT_PxIS, pis);
        }
    }

    // Now W1C the global interrupt status.
    ghc_write(AHCI_IS, is);

    return true;
}

// ── get_drive_count / get_sector_count / is_present ──────────────────────
pt::uint8_t AHCI::get_drive_count() {
    return drive_count;
}

pt::uint32_t AHCI::get_sector_count(pt::uint8_t drive) {
    if (drive >= drive_count) return 0;
    return drives[drive].sector_count;
}

bool AHCI::is_present() {
    return present;
}
