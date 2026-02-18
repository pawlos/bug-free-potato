#include "acpi.h"
#include "kernel.h"
#include "io.h"
#include "virtual.h"

ACPI_RSDP* ACPI::find_rsdp()
{
    // Search for RSDP in the EBDA and main BIOS ROM area
    // First check EBDA (0x40:0x0E is a pointer to EBDA base)
    pt::uint16_t* ebda_ptr = (pt::uint16_t*)(0x40E);
    pt::uintptr_t ebda_base = (*ebda_ptr) << 4;  // Convert segment to address

    klog("[ACPI] Searching for RSDP signature...\n");

    // Search EBDA (first 1KB)
    for (pt::uintptr_t addr = ebda_base; addr < ebda_base + 1024; addr += 16)
    {
        ACPI_RSDP* rsdp = (ACPI_RSDP*)addr;
        if (rsdp->signature[0] == 'R' && rsdp->signature[1] == 'S' &&
            rsdp->signature[2] == 'D' && rsdp->signature[3] == ' ' &&
            rsdp->signature[4] == 'P' && rsdp->signature[5] == 'T' &&
            rsdp->signature[6] == 'R' && rsdp->signature[7] == ' ')
        {
            klog("[ACPI] Found RSDP at %x\n", addr);
            return rsdp;
        }
    }

    // Search main BIOS ROM area (0xE0000 to 0xFFFFF)
    for (pt::uintptr_t addr = 0xE0000; addr < 0x100000; addr += 16)
    {
        ACPI_RSDP* rsdp = (ACPI_RSDP*)addr;
        if (rsdp->signature[0] == 'R' && rsdp->signature[1] == 'S' &&
            rsdp->signature[2] == 'D' && rsdp->signature[3] == ' ' &&
            rsdp->signature[4] == 'P' && rsdp->signature[5] == 'T' &&
            rsdp->signature[6] == 'R' && rsdp->signature[7] == ' ')
        {
            klog("[ACPI] Found RSDP at %x\n", addr);
            return rsdp;
        }
    }

    klog("[ACPI] RSDP not found\n");
    return nullptr;
}

ACPI_FADT* ACPI::find_fadt(ACPI_RSDP* rsdp)
{
    if (rsdp == nullptr) return nullptr;

    // Get RSDT address from RSDP
    pt::uint32_t rsdt_addr = rsdp->rsdt_address;
    ACPI_SDT_Header* rsdt = (ACPI_SDT_Header*)rsdt_addr;

    klog("[ACPI] RSDT at %x\n", rsdt_addr);

    // Verify RSDT signature
    if (rsdt->signature[0] != 'R' || rsdt->signature[1] != 'S' ||
        rsdt->signature[2] != 'D' || rsdt->signature[3] != 'T')
    {
        klog("[ACPI] Invalid RSDT signature\n");
        return nullptr;
    }

    // Search for FADT in RSDT
    pt::uint32_t* entries = (pt::uint32_t*)(rsdt + 1);
    pt::uint32_t num_entries = (rsdt->length - sizeof(ACPI_SDT_Header)) / sizeof(pt::uint32_t);

    for (pt::uint32_t i = 0; i < num_entries; i++)
    {
        ACPI_SDT_Header* header = (ACPI_SDT_Header*)entries[i];

        if (header->signature[0] == 'F' && header->signature[1] == 'A' &&
            header->signature[2] == 'D' && header->signature[3] == 'T')
        {
            klog("[ACPI] Found FADT at %x\n", entries[i]);
            return (ACPI_FADT*)header;
        }
    }

    klog("[ACPI] FADT not found\n");
    return nullptr;
}

bool ACPI::verify_checksum(ACPI_SDT_Header* header)
{
    pt::uint8_t* bytes = (pt::uint8_t*)header;
    pt::uint8_t checksum = 0;

    for (pt::uint32_t i = 0; i < header->length; i++)
    {
        checksum += bytes[i];
    }

    return checksum == 0;
}

void ACPI::shutdown()
{
    klog("[ACPI] Attempting shutdown...\n");

    // Try ACPI shutdown first
    ACPI_RSDP* rsdp = find_rsdp();
    if (rsdp != nullptr)
    {
        ACPI_FADT* fadt = find_fadt(rsdp);
        if (fadt != nullptr && fadt->pm1a_cnt_blk != 0)
        {
            // Get PM1a control block address
            pt::uint16_t pm1a_cnt = fadt->pm1a_cnt_blk;

            // Get sleep type values from DSDT (we'll use S5 for shutdown)
            // For now, use hardcoded values for S5 (shutdown state)
            // SLP_TYPa for S5 is typically 0x1, SLP_EN is 0x2000

            pt::uint16_t sleep_control = 0x1 << 10 | 0x2000;  // SLP_TYPa=1, SLP_EN=1

            // Write to PM1a control block
            IO::outw(pm1a_cnt, sleep_control);

            klog("[ACPI] Shutdown command sent to PM1a\n");
            return;
        }
    }

    // Fallback: use PS/2 controller reset
    klog("[ACPI] ACPI not available, using PS/2 shutdown\n");
    for (pt::uint8_t i = 0; i < 3; i++)
    {
        pt::uint8_t cmd = IO::inb(0x64);  // Get controller status
        if ((cmd & 0x02) == 0) break;     // Wait for input buffer to be empty
    }

    // Send PS/2 controller shutdown command
    IO::outb(0x64, 0xFE);  // Pulse reset line

    // If we reach here, halt
    halt();
}

void ACPI::reboot()
{
    klog("[ACPI] Attempting reboot...\n");

    // Use keyboard controller reset (port 0x64)
    // This is the most reliable method for reboot

    // Wait for input buffer to be empty
    for (int i = 0; i < 100; i++)
    {
        if ((IO::inb(0x64) & 0x02) == 0) break;
    }

    // Send reboot command to keyboard controller
    IO::outb(0x64, 0xFE);

    // If that doesn't work, halt
    klog("[ACPI] Reboot command failed\n");
    halt();
}
