#pragma once
#include <stddef.h>

struct boot_info 
{
	uint32_t size;
	uint32_t reserved;
} __attribute__((packed));

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
};