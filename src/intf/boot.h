#pragma once
#include <stddef.h>
#include <print.h>

struct boot_info 
{
	uint32_t size;
	uint32_t reserved;
} __attribute__((packed));

class BootInfo
{	
	private:
		TerminalPrinter *m_terminal;
	public:
		BootInfo(TerminalPrinter* terminal)
		{
			this->m_terminal = terminal;
		}
		void parse(boot_info *boot_info);
};


struct basic_tag 
{
	uint32_t type;
	uint32_t size;
} __attribute__((packed));

struct boot_loader_name 
{
	uint32_t type;
	uint32_t size;
	char*	name;
} __attribute__((packed));

struct boot_loader_physical_address
{
	uint32_t type;
	uint32_t size;
	uint32_t load_base_addr;
} __attribute__((packed));

struct boot_command_line
{
	uint32_t type;
	uint32_t size;
	char *cmd;
} __attribute__((packed));

struct boot_apm_table
{
	uint32_t type;
	uint32_t size;
	uint16_t version;
	uint16_t cseg;
	uint32_t offset;
	uint16_t cseg_16;
	uint16_t dseg;
	uint16_t flags;
	uint16_t cseg_len;
	uint16_t cseg_16_len;
	uint16_t dseg_len;
} __attribute__((packed));


struct memory_map_entry
{
	uint64_t base_addr;
	uint64_t length;
	uint32_t type;
	uint32_t reserved;
} __attribute__((packed));

struct boot_memory_map
{
	uint32_t type;
	uint32_t size;
	uint32_t entry_size;
	uint32_t entry_version;
	memory_map_entry* entries;
} __attribute__((packed));

struct boot_elf_symbols
{
	uint32_t type;
	uint32_t size;
	uint16_t num;
	uint16_t entsize;
	uint16_t shndx;
	uint16_t reserved;
} __attribute__((packed));

struct boot_basic_memory
{
	uint32_t type;
	uint32_t size;
	uint32_t mem_lower;
	uint32_t mem_upper;
} __attribute__((packed));

struct boot_bios_device
{
	uint32_t type;
	uint32_t size;
	uint32_t biosdev;
	uint32_t partition;
	uint32_t sub_partition;
} __attribute__((packed));

struct boot_acpi
{
	uint32_t type;
	uint32_t size;
} __attribute__((packed));

struct boot_framebuffer
{
	uint32_t type;
	uint32_t size;
	uint64_t framebuffer_addr;
	uint32_t framebuffer_pitch;
	uint32_t framebuffer_width;
	uint32_t framebuffer_height;
	uint8_t framebuffer_bpp;
	uint8_t framebuffer_type;
	uint8_t reserved;
} __attribute__((packed));

struct boot_vbe_info
{
	uint32_t type;
	uint32_t size;
	uint16_t vbe_mode;
	uint16_t vbe_infterface_seg;
	uint16_t vbe_infterface_off;
	uint16_t vbe_interface_len;
	uint8_t  vbe_control_info[512];
	uint8_t  vbe_mode_info[256];
} __attribute__((packed));