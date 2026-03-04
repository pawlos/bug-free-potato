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

// ELF file header (64 bytes)
struct Elf64_Ehdr {
    pt::uint8_t  e_ident[16];
    pt::uint16_t e_type;
    pt::uint16_t e_machine;
    pt::uint32_t e_version;
    pt::uint64_t e_entry;      // entry point virtual address
    pt::uint64_t e_phoff;      // program header table file offset
    pt::uint64_t e_shoff;
    pt::uint32_t e_flags;
    pt::uint16_t e_ehsize;
    pt::uint16_t e_phentsize;
    pt::uint16_t e_phnum;
    pt::uint16_t e_shentsize;
    pt::uint16_t e_shnum;
    pt::uint16_t e_shstrndx;
} __attribute__((packed));

// ELF program header (56 bytes)
struct Elf64_Phdr {
    pt::uint32_t p_type;
    pt::uint32_t p_flags;
    pt::uint64_t p_offset;   // offset in file
    pt::uint64_t p_vaddr;    // virtual address to load at
    pt::uint64_t p_paddr;
    pt::uint64_t p_filesz;   // bytes in file
    pt::uint64_t p_memsz;    // bytes in memory (>= filesz; excess is zeroed)
    pt::uint64_t p_align;
} __attribute__((packed));

// Magic bytes
constexpr pt::uint8_t  ELFMAG0    = 0x7f;
constexpr pt::uint8_t  ELFMAG1    = 'E';
constexpr pt::uint8_t  ELFMAG2    = 'L';
constexpr pt::uint8_t  ELFMAG3    = 'F';

// e_machine
constexpr pt::uint16_t EM_X86_64  = 62;

// p_type
constexpr pt::uint32_t PT_LOAD    = 1;

// Page size constant (4 KiB)
constexpr pt::uint64_t ELF_PAGE_SIZE = 4096;
