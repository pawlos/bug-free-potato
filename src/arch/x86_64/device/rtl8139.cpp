#include "net/net.h"
#include "io.h"
#include "virtual.h"
#include "kernel.h"
#include "device/pci.h"
#include "device/pic.h"

// ---------------------------------------------------------------------------
// Forward declarations for PCI config helpers (defined in pci.cpp)
// ---------------------------------------------------------------------------
extern pt::uint32_t pciConfigReadDWord(const pt::uint8_t bus, const pt::uint8_t slot,
                                       const pt::uint8_t func, const pt::uint8_t offset);
extern void pciConfigWriteDWord(const pt::uint8_t bus, const pt::uint8_t slot,
                                const pt::uint8_t func, const pt::uint8_t offset,
                                pt::uint32_t value);

// ---------------------------------------------------------------------------
// RTL8139 register offsets (relative to io_base)
// ---------------------------------------------------------------------------
static constexpr pt::uint16_t RTL_IDR0   = 0x00;  // MAC address (6 bytes)
static constexpr pt::uint16_t RTL_TSD0   = 0x10;  // TX status DWORDs [0..3]
static constexpr pt::uint16_t RTL_TSAD0  = 0x20;  // TX start address DWORDs [0..3]
static constexpr pt::uint16_t RTL_RBSTART = 0x30; // RX buffer start (DWORD, physical)
static constexpr pt::uint16_t RTL_CR     = 0x37;  // Command register (BYTE)
static constexpr pt::uint16_t RTL_CAPR   = 0x38;  // Current addr of packet read (WORD)
static constexpr pt::uint16_t RTL_CBR    = 0x3A;  // Current buffer address (WORD)
static constexpr pt::uint16_t RTL_IMR    = 0x3C;  // Interrupt mask register (WORD)
static constexpr pt::uint16_t RTL_ISR    = 0x3E;  // Interrupt status register (WORD)
static constexpr pt::uint16_t RTL_TCR    = 0x40;  // TX config (DWORD)
static constexpr pt::uint16_t RTL_RCR    = 0x44;  // RX config (DWORD)
static constexpr pt::uint16_t RTL_CONFIG1 = 0x52; // Config register 1 (BYTE)

static constexpr pt::uint16_t RTL_VENDOR = 0x10EC;
static constexpr pt::uint16_t RTL_DEVICE = 0x8139;

// RX ring size: 8 KB + 16 bytes overflow guard
static constexpr pt::uint32_t RX_BUF_SIZE = 8192;

// ---------------------------------------------------------------------------
// Static member definitions
// ---------------------------------------------------------------------------
bool         RTL8139::initialized = false;
pt::uint16_t RTL8139::io_base     = 0;
pt::uint8_t  RTL8139::mac[6]      = {};
pt::uint8_t* RTL8139::rx_buf      = nullptr;
pt::uint32_t RTL8139::rx_read     = 0;
pt::uint8_t* RTL8139::tx_buf[4]   = {};
pt::uint8_t  RTL8139::tx_slot     = 0;
pt::uint16_t RTL8139::pci_bus     = 0;
pt::uint8_t  RTL8139::pci_dev     = 0;

// ---------------------------------------------------------------------------
// PCI config helpers
// ---------------------------------------------------------------------------
pt::uint32_t RTL8139::pci_read_dword(pt::uint8_t offset) {
    return pciConfigReadDWord(static_cast<pt::uint8_t>(pci_bus), pci_dev, 0, offset);
}

void RTL8139::pci_write_dword(pt::uint8_t offset, pt::uint32_t value) {
    pciConfigWriteDWord(static_cast<pt::uint8_t>(pci_bus), pci_dev, 0, offset, value);
}

// ---------------------------------------------------------------------------
// initialize
// ---------------------------------------------------------------------------
bool RTL8139::initialize() {
    klog("[RTL8139] Initializing...\n");

    // --- Find RTL8139 on PCI bus ---
    bool found = false;
    pci_device* devices = pci::enumerate();
    pci_device* d = devices;
    while (d && d->vendor_id != 0xFFFF) {
        if (d->vendor_id == RTL_VENDOR && d->device_id == RTL_DEVICE) {
            pci_bus = d->bus;
            pci_dev = d->device;
            found = true;
            klog("[RTL8139] Found at bus=%d dev=%d\n", d->bus, d->device);
            break;
        }
        d++;
    }
    VMM::Instance()->kfree(devices);

    if (!found) {
        klog("[RTL8139] Not found on PCI bus\n");
        return false;
    }

    // --- Read BAR0 (I/O space) ---
    pt::uint32_t bar0 = pci_read_dword(0x10);
    if (!(bar0 & 0x1)) {
        klog("[RTL8139] BAR0 is not I/O space (bar0=%x)\n", bar0);
        return false;
    }
    io_base = (pt::uint16_t)(bar0 & 0xFFFC);
    klog("[RTL8139] io_base=0x%x\n", (pt::uint32_t)io_base);

    // --- Enable I/O space + bus master ---
    pt::uint32_t cmd_sts = pci_read_dword(0x04);
    pt::uint32_t cmd = cmd_sts & 0xFFFF;
    cmd |= 0x05;  // bit 0: I/O enable, bit 2: bus master enable
    pci_write_dword(0x04, (cmd_sts & 0xFFFF0000u) | cmd);

    // --- Power on (CONFIG1 = 0) ---
    IO::outb(io_base + RTL_CONFIG1, 0x00);

    // --- Software reset ---
    IO::outb(io_base + RTL_CR, 0x10);
    for (int t = 0; t < 10000; t++) {
        if (!(IO::inb(io_base + RTL_CR) & 0x10))
            break;
        IO::io_wait();
    }

    // --- Allocate and set up RX ring buffer (8 KB + 16 bytes overflow) ---
    rx_buf = static_cast<pt::uint8_t*>(
        VMM::Instance()->kcalloc(RX_BUF_SIZE + 16));
    if (!rx_buf) {
        klog("[RTL8139] RX buffer allocation failed\n");
        return false;
    }
    rx_read = 0;
    pt::uint32_t rx_phys = static_cast<pt::uint32_t>(VMM::virt_to_phys(rx_buf));
    IO::outd(io_base + RTL_RBSTART, rx_phys);

    // --- Allocate TX buffers (4 × 1536 bytes each) ---
    for (int i = 0; i < 4; i++) {
        tx_buf[i] = static_cast<pt::uint8_t*>(
            VMM::Instance()->kcalloc(1536));
        if (!tx_buf[i]) {
            klog("[RTL8139] TX buffer %d allocation failed\n", i);
            for (int j = 0; j < i; j++) VMM::Instance()->kfree(tx_buf[j]);
            VMM::Instance()->kfree(rx_buf);
            rx_buf = nullptr;
            return false;
        }
    }

    // --- Configure interrupts: RX OK (bit 0) + TX OK (bit 2) ---
    IO::outw(io_base + RTL_IMR, 0x0005);

    // --- Configure RX: accept broadcast + multicast + physical, no wrap ---
    IO::outd(io_base + RTL_RCR, 0x0000000F);

    // --- Configure TX: standard settings ---
    IO::outd(io_base + RTL_TCR, 0x03000600);

    // --- Enable TX + RX ---
    IO::outb(io_base + RTL_CR, 0x0C);

    // --- Read MAC address from IDR0-5 ---
    for (int i = 0; i < 6; i++)
        mac[i] = IO::inb(io_base + RTL_IDR0 + i);

    klog("[RTL8139] MAC: %x:%x:%x:%x:%x:%x\n",
         (pt::uint32_t)mac[0], (pt::uint32_t)mac[1],
         (pt::uint32_t)mac[2], (pt::uint32_t)mac[3],
         (pt::uint32_t)mac[4], (pt::uint32_t)mac[5]);

    initialized = true;
    klog("[RTL8139] Ready, io=0x%x\n", (pt::uint32_t)io_base);
    return true;
}

// ---------------------------------------------------------------------------
// is_present / get_io_base / get_mac
// ---------------------------------------------------------------------------
bool RTL8139::is_present() { return initialized; }

pt::uint16_t RTL8139::get_io_base() { return io_base; }

void RTL8139::get_mac(pt::uint8_t out_mac[6]) {
    for (int i = 0; i < 6; i++) out_mac[i] = mac[i];
}

// ---------------------------------------------------------------------------
// send — transmit a frame using the next available TX descriptor
// ---------------------------------------------------------------------------
void RTL8139::send(const pt::uint8_t* data, pt::uint32_t len) {
    if (!initialized || !data || len == 0 || len > 1536) return;

    // Wait for the current slot to be free (OWN bit 13 = 1 means we own it)
    pt::uint16_t tsd_off = (pt::uint16_t)(RTL_TSD0 + tx_slot * 4);
    for (int t = 0; t < 100000; t++) {
        if (IO::ind(io_base + tsd_off) & (1u << 13))
            break;
        IO::io_wait();
    }

    // Copy frame to TX buffer
    pt::uint8_t* buf = tx_buf[tx_slot];
    for (pt::uint32_t i = 0; i < len; i++) buf[i] = data[i];

    // Write physical address of TX buffer
    pt::uint32_t phys = static_cast<pt::uint32_t>(VMM::virt_to_phys(buf));
    IO::outd(io_base + RTL_TSAD0 + tx_slot * 4, phys);

    // Write packet size (clears OWN bit → NIC takes ownership and transmits)
    // Bits 23:16 = early TX threshold (0x3F = max), bits 12:0 = length
    IO::outd(io_base + tsd_off, 0x003F0000u | len);

    tx_slot = (pt::uint8_t)((tx_slot + 1) % 4);
}

// ---------------------------------------------------------------------------
// handle_irq — called from irq11_handler (idt.cpp)
// ---------------------------------------------------------------------------
void RTL8139::handle_irq() {
    if (!initialized) return;

    pt::uint16_t status = IO::inw(io_base + RTL_ISR);
    IO::outw(io_base + RTL_ISR, status);   // write-1-to-clear all bits

    // RX OK
    if (status & 0x01) {
        // Read packets while buffer is not empty (CR bit 0 = BUFE)
        while (!(IO::inb(io_base + RTL_CR) & 0x01)) {
            // Read 4-byte packet header at current rx_read position
            pt::uint32_t offset = rx_read % RX_BUF_SIZE;
            const pt::uint8_t* hdr_ptr = rx_buf + offset;
            pt::uint16_t pkt_status = (pt::uint16_t)(hdr_ptr[0] | ((pt::uint16_t)hdr_ptr[1] << 8));
            pt::uint16_t pkt_len    = (pt::uint16_t)(hdr_ptr[2] | ((pt::uint16_t)hdr_ptr[3] << 8));

            if (pkt_len < 4 || pkt_len > 1536) {
                // Corrupt or oversized — reset read pointer
                rx_read = IO::inw(io_base + RTL_CBR);
                IO::outw(io_base + RTL_CAPR, (pt::uint16_t)(rx_read - 16));
                break;
            }

            if (pkt_status & 0x0001) {  // ROK: received OK
                // Data starts after the 4-byte header; subtract 4-byte CRC
                pt::uint32_t data_offset = (offset + 4) % RX_BUF_SIZE;
                pt::uint32_t data_len = pkt_len - 4;

                // Handle packets that wrap around the ring buffer
                if (data_offset + data_len <= RX_BUF_SIZE) {
                    // Contiguous — pass directly
                    net_receive(rx_buf + data_offset, data_len);
                } else {
                    // Wrap: copy to a temporary flat buffer
                    static pt::uint8_t tmp[1536];
                    pt::uint32_t first  = RX_BUF_SIZE - data_offset;
                    pt::uint32_t second = data_len - first;
                    if (data_len <= 1536) {
                        for (pt::uint32_t i = 0; i < first; i++)
                            tmp[i] = rx_buf[data_offset + i];
                        for (pt::uint32_t i = 0; i < second; i++)
                            tmp[first + i] = rx_buf[i];
                        net_receive(tmp, data_len);
                    }
                }
            }

            // Advance read pointer: pkt_len (from header) + 4-byte header, 4-byte aligned
            rx_read = (rx_read + pkt_len + 4 + 3) & ~3u;
            // Update CAPR (NIC quirk: CAPR must be 16 bytes behind our read position)
            IO::outw(io_base + RTL_CAPR, (pt::uint16_t)((rx_read % RX_BUF_SIZE) - 16));
        }
    }
    // TX OK (bit 2) — no action needed; just acknowledged
}
