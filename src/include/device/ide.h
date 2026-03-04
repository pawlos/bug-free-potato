#pragma once

#include "defs.h"

// IDE Controller I/O ports (Primary bus)
constexpr pt::uint16_t IDE_PRIMARY_DATA = 0x1F0;
constexpr pt::uint16_t IDE_PRIMARY_ERROR = 0x1F1;
constexpr pt::uint16_t IDE_PRIMARY_FEATURES = 0x1F1;
constexpr pt::uint16_t IDE_PRIMARY_SECTOR_COUNT = 0x1F2;
constexpr pt::uint16_t IDE_PRIMARY_LBA_LOW = 0x1F3;
constexpr pt::uint16_t IDE_PRIMARY_LBA_MID = 0x1F4;
constexpr pt::uint16_t IDE_PRIMARY_LBA_HIGH = 0x1F5;
constexpr pt::uint16_t IDE_PRIMARY_DRIVE_SELECT = 0x1F6;
constexpr pt::uint16_t IDE_PRIMARY_STATUS = 0x1F7;
constexpr pt::uint16_t IDE_PRIMARY_COMMAND = 0x1F7;
constexpr pt::uint16_t IDE_PRIMARY_ALT_STATUS = 0x3F6;
constexpr pt::uint16_t IDE_PRIMARY_CONTROL = 0x3F6;

// IDE Commands
constexpr pt::uint8_t IDE_CMD_READ_SECTORS = 0x20;
constexpr pt::uint8_t IDE_CMD_WRITE_SECTORS = 0x30;
constexpr pt::uint8_t IDE_CMD_IDENTIFY = 0xEC;

// Drive selection bits
constexpr pt::uint8_t IDE_DRIVE_MASTER = 0xA0;
constexpr pt::uint8_t IDE_DRIVE_SLAVE = 0xB0;

// Status register bits
constexpr pt::uint8_t IDE_STATUS_ERR = 0x01;
constexpr pt::uint8_t IDE_STATUS_DRQ = 0x08;
constexpr pt::uint8_t IDE_STATUS_SRV = 0x10;
constexpr pt::uint8_t IDE_STATUS_DF = 0x20;
constexpr pt::uint8_t IDE_STATUS_RDY = 0x40;
constexpr pt::uint8_t IDE_STATUS_BSY = 0x80;

struct IDEDevice {
    bool present;
    bool is_master;
    pt::uint32_t sector_count;
    char model[41];
};

class IDE {
public:
    static void initialize();
    static bool read_sectors(pt::uint8_t drive, pt::uint32_t lba, pt::uint8_t sector_count, void* buffer);
    static bool write_sectors(pt::uint8_t drive, pt::uint32_t lba, pt::uint8_t sector_count, const void* buffer);
    static bool identify_drive(pt::uint8_t drive, IDEDevice* device);
    static bool is_drive_present(pt::uint8_t drive);
    static pt::uint32_t get_sector_count(pt::uint8_t drive);

private:
    static bool wait_for_ready();
    static bool wait_for_data();
    static bool wait_for_completion();
    static void select_drive(pt::uint8_t drive);
    static void software_reset();
    
    static IDEDevice drives[2];  // Master (0) and Slave (1)
    static bool initialized;
};
