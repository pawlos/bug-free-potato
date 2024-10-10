#pragma once
#include "defs.h"

constexpr pt::uint32_t BOOT_CMDLINE = 1;
constexpr pt::uint32_t BOOT_LOADER_NAME = 2;
constexpr pt::uint32_t BOOT_BASIC_MEM = 4;
constexpr pt::uint32_t BOOT_BIOS = 5;
constexpr pt::uint32_t BOOT_MMAP = 6;
constexpr pt::uint32_t BOOT_VBE_INFO = 7;
constexpr pt::uint32_t BOOT_FRAMEBUFFER = 8;
constexpr pt::uint32_t BOOT_ELF_SYMBOLS = 9;
constexpr pt::uint32_t BOOT_APM_TABLE = 10;
constexpr pt::uint32_t BOOT_ACPI = 14;
constexpr pt::uint32_t BOOT_PHYSICAL = 21;

struct boot_info 
{
	pt::uint32_t size;
	pt::uint32_t reserved;
} __attribute__((packed));


struct basic_tag 
{
	pt::uint32_t type;
	pt::uint32_t size;
} __attribute__((packed));

struct boot_loader_name 
{
	pt::uint32_t type;
	pt::uint32_t size;
	char*	name;
} __attribute__((packed));

struct boot_loader_physical_address
{
	pt::uint32_t type;
	pt::uint32_t size;
	pt::uint32_t load_base_addr;
} __attribute__((packed));

struct boot_command_line
{
	pt::uint32_t type;
	pt::uint32_t size;
	char *cmd;
} __attribute__((packed));

struct boot_apm_table
{
	pt::uint32_t type;
	pt::uint32_t size;
	pt::uint16_t version;
	pt::uint16_t cseg;
	pt::uint32_t offset;
	pt::uint16_t cseg_16;
	pt::uint16_t dseg;
	pt::uint16_t flags;
	pt::uint16_t cseg_len;
	pt::uint16_t cseg_16_len;
	pt::uint16_t dseg_len;
} __attribute__((packed));


struct memory_map_entry
{
	pt::uint64_t base_addr;
	pt::uint64_t length;
	pt::uint32_t type;
	pt::uint32_t reserved;
} __attribute__((packed));

struct boot_memory_map
{
	pt::uint32_t type;
	pt::uint32_t size;
	pt::uint32_t entry_size;
	pt::uint32_t entry_version;
	memory_map_entry* entries;
} __attribute__((packed));

struct boot_elf_symbols
{
	pt::uint32_t type;
	pt::uint32_t size;
	pt::uint16_t num;
	pt::uint16_t entsize;
	pt::uint16_t shndx;
	pt::uint16_t reserved;
} __attribute__((packed));

struct boot_basic_memory
{
	pt::uint32_t type;
	pt::uint32_t size;
	pt::uint32_t mem_lower;
	pt::uint32_t mem_upper;
} __attribute__((packed));

struct boot_bios_device
{
	pt::uint32_t type;
	pt::uint32_t size;
	pt::uint32_t biosdev;
	pt::uint32_t partition;
	pt::uint32_t sub_partition;
} __attribute__((packed));

struct boot_acpi
{
	pt::uint32_t type;
	pt::uint32_t size;
} __attribute__((packed));

struct boot_framebuffer
{
	pt::uint32_t type;
	pt::uint32_t size;
	pt::uint64_t framebuffer_addr;
	pt::uint32_t framebuffer_pitch;
	pt::uint32_t framebuffer_width;
	pt::uint32_t framebuffer_height;
	pt::uint8_t framebuffer_bpp;
	pt::uint8_t framebuffer_type;
	pt::uint8_t reserved;
} __attribute__((packed));

struct boot_vbe_info
{
	pt::uint32_t type;
	pt::uint32_t size;
	pt::uint16_t vbe_mode;
	pt::uint16_t vbe_interface_seg;
	pt::uint16_t vbe_interface_off;
	pt::uint16_t vbe_interface_len;
	pt::uint8_t  vbe_control_info[512];
	pt::uint8_t  vbe_mode_info[256];
} __attribute__((packed));

#define MEMORY_ENTRIES_LIMIT 7

class BootInfo
{
		pt::size_t size;
		boot_command_line* cmd_line;
		boot_loader_name* loader_name;
		boot_basic_memory* basic_mem;
		boot_bios_device* bios;
		boot_memory_map* mmap;
		boot_vbe_info* vbe;
		boot_framebuffer* framebuffer;
		boot_elf_symbols* elf;
		boot_apm_table* apm_table;
		boot_acpi* acpi;
		boot_loader_physical_address* physical;
		memory_map_entry* memory_entry[MEMORY_ENTRIES_LIMIT];
		void log();
	public:
		void parse(boot_info *boot_info);
		[[nodiscard]] boot_framebuffer* get_framebuffer() const;
		memory_map_entry** get_memory_maps();
};