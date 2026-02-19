#pragma once
#include "defs.h"

// Standard ELF64 section header (64 bytes)
struct Elf64_Shdr {
    pt::uint32_t sh_name;
    pt::uint32_t sh_type;
    pt::uint64_t sh_flags;
    pt::uint64_t sh_addr;
    pt::uint64_t sh_offset;
    pt::uint64_t sh_size;
    pt::uint32_t sh_link;
    pt::uint32_t sh_info;
    pt::uint64_t sh_addralign;
    pt::uint64_t sh_entsize;
} __attribute__((packed));

// Standard ELF64 symbol table entry (24 bytes)
struct Elf64_Sym {
    pt::uint32_t st_name;
    pt::uint8_t  st_info;
    pt::uint8_t  st_other;
    pt::uint16_t st_shndx;
    pt::uint64_t st_value;
    pt::uint64_t st_size;
} __attribute__((packed));

constexpr pt::uint32_t SHT_SYMTAB = 2;
constexpr pt::uint32_t SHT_STRTAB = 3;
constexpr pt::uint8_t  STT_FUNC   = 2;
