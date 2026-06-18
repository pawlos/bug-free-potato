#pragma once

#include "defs.h"
#include "pci.h"

// ── AHCI driver (Serial ATA, DMA-based) ──────────────────────────────────
// Replaces legacy PIO IDE with modern AHCI.  The Disk layer tries AHCI
// first and falls back to IDE if no AHCI controller is found.

// ── PCI identification ───────────────────────────────────────────────────
constexpr pt::uint8_t  AHCI_CLASS    = 0x01;  // Mass storage
constexpr pt::uint8_t  AHCI_SUBCLASS = 0x06;  // Serial ATA
constexpr pt::uint8_t  AHCI_PROG_IF  = 0x01;  // AHCI

// ── Generic Host Control registers (relative to ABAR) ────────────────────
constexpr pt::uint8_t  AHCI_CAP    = 0x00;
constexpr pt::uint8_t  AHCI_GHC    = 0x04;
constexpr pt::uint8_t  AHCI_IS     = 0x08;
constexpr pt::uint8_t  AHCI_PI     = 0x0C;
constexpr pt::uint8_t  AHCI_VS     = 0x10;
constexpr pt::uint8_t  AHCI_CAP2   = 0x24;
constexpr pt::uint8_t  AHCI_BOHC   = 0x28;

// GHC bits
constexpr pt::uint32_t GHC_HR  = 0x00000001;  // HBA Reset
constexpr pt::uint32_t GHC_IE  = 0x00000002;  // Interrupt Enable
constexpr pt::uint32_t GHC_AE  = 0x80000000;  // AHCI Enable

// CAP bits
constexpr pt::uint32_t CAP_NP   = 0x0000001F;  // Number of Ports
constexpr pt::uint32_t CAP_S64A = 0x80000000;  // 64-bit Addressing

// ── Port register offsets (port * 0x80 + base) ──────────────────────────
constexpr pt::uint16_t PORT_OFFSET    = 0x80;
constexpr pt::uint16_t PORT_BASE      = 0x100;

constexpr pt::uint8_t  PORT_PxCLB  = 0x00;  // Command List Base (dword, low)
constexpr pt::uint8_t  PORT_PxCLBU = 0x04;  // Command List Base Upper
constexpr pt::uint8_t  PORT_PxFB   = 0x08;  // FIS Base (dword, low)
constexpr pt::uint8_t  PORT_PxFBU  = 0x0C;  // FIS Base Upper
constexpr pt::uint8_t  PORT_PxIS   = 0x10;  // Interrupt Status
constexpr pt::uint8_t  PORT_PxIE   = 0x14;  // Interrupt Enable
constexpr pt::uint8_t  PORT_PxCMD  = 0x18;  // Command & Status
constexpr pt::uint8_t  PORT_PxTFD  = 0x20;  // Task File Data
constexpr pt::uint8_t  PORT_PxSIG  = 0x24;  // Signature
constexpr pt::uint8_t  PORT_PxSSTS = 0x28;  // Serial ATA Status
constexpr pt::uint8_t  PORT_PxSCTL = 0x2C;  // Serial ATA Control
constexpr pt::uint8_t  PORT_PxSERR = 0x30;  // Serial ATA Error
constexpr pt::uint8_t  PORT_PxSACT = 0x34;  // Serial ATA Active
constexpr pt::uint8_t  PORT_PxCI   = 0x38;  // Command Issue
constexpr pt::uint8_t  PORT_PxSNTF = 0x3C;  // Serial ATA Notification

// PxCMD bits
constexpr pt::uint32_t PxCMD_ST   = 0x00000001;  // Start (command list)
constexpr pt::uint32_t PxCMD_CR   = 0x00008000;  // Command List Running
constexpr pt::uint32_t PxCMD_FRE  = 0x00000010;  // FIS Receive Enable
constexpr pt::uint32_t PxCMD_FR   = 0x00004000;  // FIS Receive Running
constexpr pt::uint32_t PxCMD_MPSS = 0x00000100;  // Mechanical Presence Switch
constexpr pt::uint32_t PxCMD_CPD  = 0x00000200;  // Cold Presence Detection
constexpr pt::uint32_t PxCMD_ESP  = 0x00000800;  // External SATA Port
constexpr pt::uint32_t PxCMD_FBSCP = 0x00001000; // FIS-based Switching Capable Port
constexpr pt::uint32_t PxCMD_APSTE = 0x00002000; // Aggressive PMP Enable
constexpr pt::uint32_t PxCMD_ATAPI = 0x02000000; // Device is ATAPI
constexpr pt::uint32_t PxCMD_ICC   = 0x1F000000; // Interface Communication Control

// PxSSTS bits
constexpr pt::uint32_t SSTS_DET = 0x0000000F;  // Device Detection
constexpr pt::uint32_t DET_NO_DEVICE     = 0;
constexpr pt::uint32_t DET_PRESENT       = 1;  // device present but not comm
constexpr pt::uint32_t DET_ESTABLISHED   = 3;  // communication established

// PxSIG values (when communication established)
constexpr pt::uint32_t SIG_SATA   = 0x00000101;
constexpr pt::uint32_t SIG_ATAPI  = 0xEB140101;
constexpr pt::uint32_t SIG_PM     = 0x96690101;
constexpr pt::uint32_t SIG_SEMB   = 0xC33C0101;

// PxIS bits
constexpr pt::uint32_t PxIS_DHRS = 0x00000001;  // Device-to-Host Register FIS
constexpr pt::uint32_t PxIS_PSS  = 0x00000002;  // PIO Setup FIS
constexpr pt::uint32_t PxIS_DSS  = 0x00000004;  // DMA Setup FIS
constexpr pt::uint32_t PxIS_SDBS = 0x00000008;  // Set Device Bits
constexpr pt::uint32_t PxIS_UFS  = 0x00000010;  // Unknown FIS
constexpr pt::uint32_t PxIS_DPS  = 0x00000020;  // Descriptor Processed
constexpr pt::uint32_t PxIS_PCS  = 0x00000040;  // Port Connect Change
constexpr pt::uint32_t PxIS_DMP  = 0x00000080;  // Device Mechanical Presence
constexpr pt::uint32_t PxIS_PRCS = 0x00004000;  // PhyRdy Change Status
constexpr pt::uint32_t PxIS_IPMS = 0x00008000;  // Incorrect Port Multiplier
constexpr pt::uint32_t PxIS_OF   = 0x10000000;  // Overflow
constexpr pt::uint32_t PxIS_INFS = 0x20000000;  // Invalid FIS
constexpr pt::uint32_t PxIS_IFS  = 0x40000000;  // Interface Fatal Error
constexpr pt::uint32_t PxIS_HBDS = 0x80000000;  // Host Bus Data Error

// ── Command List entry (32 bytes) ────────────────────────────────────────
// DW0 layout (little-endian x86):
//   bits  4:0  = CFL (Command FIS Length in DWORDs, min 5)
//   bit   5    = ATAPI
//   bit   6    = Write (0=read, 1=write)
//   bits 31:16 = PRDTL (Physical Region Descriptor Table Length)
struct __attribute__((packed)) HBA_CMD_HEADER {
    pt::uint32_t opts;      // DW0: CFL + flags + PRDTL
    pt::uint32_t prdbc;     // DW1: PRD Byte Count (written by hw)
    pt::uint32_t ctba;      // DW2: Command Table Base Address (low)
    pt::uint32_t ctbau;     // DW3: Command Table Base Address (high)
    pt::uint32_t reserved1[4];
};

// DW0 flag helpers
inline pt::uint32_t AHCI_CMD_CFL(pt::uint32_t n)   { return n & 0x1Fu; }
constexpr pt::uint32_t AHCI_CMD_ATAPI               = (1u << 5);
constexpr pt::uint32_t AHCI_CMD_WRITE               = (1u << 6);
inline pt::uint32_t AHCI_CMD_PRDTL(pt::uint32_t n)  { return ((n & 0xFFFFu) << 16); }

// ── PRDT entry (16 bytes) ────────────────────────────────────────────────
struct __attribute__((packed)) HBA_PRDT_ENTRY {
    pt::uint32_t dba;       // Data Base Address (low)
    pt::uint32_t dbau;      // Data Base Address (high)
    pt::uint32_t reserved;
    pt::uint32_t dbc;       // Data Byte Count: bits 21:0 = count-1, bit31=I
};

constexpr pt::uint32_t PRDT_DBC_MASK = 0x003FFFFF;
constexpr pt::uint32_t PRDT_IOC      = 0x80000000;

// ── Command Table (variable size, 128 bytes minimum) ────────────────────
struct __attribute__((packed)) HBA_CMD_TABLE {
    pt::uint8_t  cfis[64];    // Command FIS (Register H2D, type=0x27)
    pt::uint8_t  acmd[32];    // ATAPI command (unused)
    pt::uint8_t  reserved[32];
    HBA_PRDT_ENTRY prdt[1];   // Variable-length PRDT
};

// ── Register H2D FIS (20 bytes) ──────────────────────────────────────────
struct __attribute__((packed)) FIS_H2D {
    pt::uint8_t  fis_type;    // 0x27
    pt::uint8_t  pmport;      // PM port (bits 3:0) + C (bit 7)
    pt::uint8_t  command;     // ATA command
    pt::uint8_t  features_low;
    pt::uint8_t  lba0;
    pt::uint8_t  lba1;
    pt::uint8_t  lba2;
    pt::uint8_t  device;      // 0x40 = LBA mode
    pt::uint8_t  lba3;
    pt::uint8_t  lba4;
    pt::uint8_t  lba5;
    pt::uint8_t  features_high;
    pt::uint8_t  count_low;
    pt::uint8_t  count_high;
    pt::uint8_t  icc;
    pt::uint8_t  control;
    pt::uint8_t  reserved[4];
};

constexpr pt::uint8_t FIS_TYPE_H2D = 0x27;

// ── Drive state ──────────────────────────────────────────────────────────
struct AHCIDrive {
    bool         present;
    pt::uint8_t  port;          // HBA port this drive lives on
    pt::uint32_t sector_count;
    bool         lba48;
    char         model[41];
};

// ── Driver limits ────────────────────────────────────────────────────────
constexpr pt::uint8_t  AHCI_MAX_PORTS   = 32;
constexpr pt::uint8_t  AHCI_MAX_SLOTS   = 32;
constexpr pt::uint8_t  AHCI_MAX_DRIVES  = 8;
constexpr pt::uint32_t AHCI_TIMEOUT     = 10000000;  // busy-wait bound (error path only)
constexpr pt::uint16_t SATA_SECTOR_SIZE = 512;
constexpr pt::uint8_t  AHCI_PRDT_MAX    = 8;   // max PRD entries per command table
constexpr pt::size_t   AHCI_SLOT_SIZE   = 256; // bytes per command table slot
// Command tables are packed into a single 4K frame: 4096 / 256 = 16 usable
// slots.  This driver is synchronous (one outstanding command at a time), so
// slot 0 is all that is ever needed; 16 is plenty of head-room.
constexpr pt::uint8_t  AHCI_CT_SLOTS    = 16;

// ── ATA commands ─────────────────────────────────────────────────────────
constexpr pt::uint8_t ATA_CMD_READ_DMA      = 0xC8;
constexpr pt::uint8_t ATA_CMD_READ_DMA_EXT  = 0x25;
constexpr pt::uint8_t ATA_CMD_WRITE_DMA     = 0xCA;
constexpr pt::uint8_t ATA_CMD_WRITE_DMA_EXT = 0x35;
constexpr pt::uint8_t ATA_CMD_IDENTIFY      = 0xEC;

// ── AHCI class ───────────────────────────────────────────────────────────
class AHCI {
public:
    static bool initialize();
    static bool is_present();
    static pt::uint8_t get_drive_count();
    static pt::uint32_t get_sector_count(pt::uint8_t drive);
    static bool read_sectors(pt::uint8_t drive, pt::uint32_t lba,
                             pt::uint8_t count, void* buffer);
    static bool write_sectors(pt::uint8_t drive, pt::uint32_t lba,
                              pt::uint8_t count, const void* buffer);

    // Called from shared IRQ handler (IRQ11, shared with RTL8139)
    static bool check_irq();

private:
    // ── Per-port state ───────────────────────────────────────────────
    struct AHCI_PORT_STATE {
        bool     valid;
        HBA_CMD_HEADER* cmd_list;    // virtual address of command list
        pt::uintptr_t cmd_list_phys; // physical address of command list
        pt::uint8_t*  rx_fis;        // virtual address of received FIS
        pt::uintptr_t rx_fis_phys;   // physical address of received FIS
        // Command tables: each slot gets a 256-byte region in one shared page
        pt::uint8_t*  ct_area;       // virtual (shared across slots)
        pt::uintptr_t ct_area_phys;  // physical
    };

    static bool         initialized;
    static bool         present;
    static pt::uint16_t pci_bus;
    static pt::uint8_t  pci_dev;

    // ABAR (MMIO)
    static volatile pt::uint32_t* abar;
    static pt::uintptr_t          abar_phys;
    static pt::uintptr_t          abar_size;

    // Ports
    static AHCI_PORT_STATE ports[AHCI_MAX_PORTS];
    static AHCIDrive       drives[AHCI_MAX_DRIVES];
    static pt::uint8_t     drive_count;

    // ── Low-level register access ────────────────────────────────────
    static pt::uint32_t pci_read_dword(pt::uint8_t offset);
    static void         pci_write_dword(pt::uint8_t offset, pt::uint32_t value);

    static pt::uint32_t ghc_read(pt::uint8_t reg);
    static void         ghc_write(pt::uint8_t reg, pt::uint32_t value);
    static pt::uint32_t port_read(pt::uint8_t port, pt::uint8_t reg);
    static void         port_write(pt::uint8_t port, pt::uint8_t reg, pt::uint32_t value);

    // ── Internal helpers ────────────────────────────────────────────
    static bool reset_hba();
    static bool init_port(pt::uint8_t port);
    static bool identify_drive(pt::uint8_t port, pt::uint8_t drive_idx);
    static bool start_port(pt::uint8_t port);
    static void stop_port(pt::uint8_t port);
    // Fatal-error recovery: stop the port, clear SERR/IS, restart engines.
    static void recover_port(pt::uint8_t port);
    // Diagnostic: dump full port + PCI + command-header state on a wedge.
    static void dump_state(pt::uint8_t port, pt::uint8_t slot, const char* who);
    static pt::uint8_t find_free_slot(pt::uint8_t port);
    static bool issue_command(pt::uint8_t port, pt::uint8_t slot,
                              pt::uint8_t cmd, pt::uint64_t lba,
                              pt::uint16_t sector_count,
                              pt::uintptr_t data_phys, bool is_write,
                              pt::uint8_t prd_count, bool lba48);
    static pt::uint8_t calc_prdt_entries(pt::uintptr_t data_phys,
                                         pt::uint32_t byte_count);
};
