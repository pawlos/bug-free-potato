#pragma once
#include "defs.h"

class ElfLoader {
public:
    // Maximum ELF code pages (must match Task::MAX_PRIV_PTS * 512).
    static constexpr pt::size_t MAX_ELF_PAGES = 8 * 512;  // 16 MB

    // Per-page permission flags stored during load(), consumed by create_elf_task.
    // Bit 0 = executable, bit 1 = writable (matches PF_X, PF_W from ELF spec).
    static pt::uint8_t page_flags[MAX_ELF_PAGES];

    // Load ELF segments from a FAT12 file into virtual memory.
    // Returns the entry point address, or 0 on failure.
    // If out_code_size is non-null, it receives the total byte span of all
    // PT_LOAD segments (max(vaddr+memsz) - min(vaddr)).
    // Populates page_flags[] with per-page permissions.
    static pt::uintptr_t load(const char* filename,
                              pt::size_t* out_code_size = nullptr);
};
