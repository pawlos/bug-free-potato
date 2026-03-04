#pragma once
#include "defs.h"

class ElfLoader {
public:
    // Load ELF segments from a FAT12 file into virtual memory.
    // Returns the entry point address, or 0 on failure.
    // If out_code_size is non-null, it receives the total byte span of all
    // PT_LOAD segments (max(vaddr+memsz) - min(vaddr)).
    static pt::uintptr_t load(const char* filename,
                              pt::size_t* out_code_size = nullptr);
};
