#pragma once
#include "defs.h"

// ACPI RSDP (Root System Description Pointer) signature
#define ACPI_RSDP_SIGNATURE "RSD PTR "

struct ACPI_RSDP {
    pt::uint8_t signature[8];
    pt::uint8_t checksum;
    pt::uint8_t oemid[6];
    pt::uint8_t revision;
    pt::uint32_t rsdt_address;
    pt::uint32_t length;
    pt::uint64_t xsdt_address;
    pt::uint8_t extended_checksum;
    pt::uint8_t reserved[3];
} __attribute__((packed));

struct ACPI_SDT_Header {
    pt::uint8_t signature[4];
    pt::uint32_t length;
    pt::uint8_t revision;
    pt::uint8_t checksum;
    pt::uint8_t oemid[6];
    pt::uint8_t oem_table_id[8];
    pt::uint32_t oem_revision;
    pt::uint32_t creator_id;
    pt::uint32_t creator_revision;
} __attribute__((packed));

struct ACPI_FADT {
    ACPI_SDT_Header header;
    pt::uint32_t firmware_ctrl;
    pt::uint32_t dsdt;
    pt::uint8_t  int_model;
    pt::uint8_t  preferred_pm_profile;
    pt::uint16_t sci_int;
    pt::uint32_t smi_cmd;
    pt::uint8_t  acpi_enable;
    pt::uint8_t  acpi_disable;
    pt::uint8_t  s4bios_req;
    pt::uint8_t  pstate_cnt;
    pt::uint32_t pm1a_evt_blk;
    pt::uint32_t pm1b_evt_blk;
    pt::uint32_t pm1a_cnt_blk;
    pt::uint32_t pm1b_cnt_blk;
    pt::uint32_t pm2_cnt_blk;
    pt::uint32_t pm_tmr_blk;
    pt::uint32_t gpe0_blk;
    pt::uint32_t gpe1_blk;
    pt::uint8_t  pm1_evt_len;
    pt::uint8_t  pm1_cnt_len;
    pt::uint8_t  pm2_cnt_len;
    pt::uint8_t  pm_tmr_len;
    pt::uint8_t  gpe0_blk_len;
    pt::uint8_t  gpe1_blk_len;
    pt::uint8_t  gpe1_base;
    pt::uint8_t  cst_cnt;
    pt::uint16_t p_lvl2_lat;
    pt::uint16_t p_lvl3_lat;
    pt::uint16_t flush_size;
    pt::uint16_t flush_stride;
    pt::uint8_t  duty_offset;
    pt::uint8_t  duty_width;
    pt::uint8_t  day_alrm;
    pt::uint8_t  mon_alrm;
    pt::uint8_t  century;
    pt::uint16_t iapc_boot_arch;
    pt::uint8_t  reserved;
    pt::uint32_t flags;
    // Extended fields for revision 4+
    // (we'll keep it simple for now)
} __attribute__((packed));

class ACPI {
public:
    // Find RSDP in memory
    static ACPI_RSDP* find_rsdp();

    // Find FADT from RSDT
    static ACPI_FADT* find_fadt(ACPI_RSDP* rsdp);

    // Power management operations
    static void shutdown();
    static void reboot();

    // Helper to check ACPI checksum
    static bool verify_checksum(ACPI_SDT_Header* header);
};
