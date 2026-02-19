#pragma once
#include "defs.h"

class ElfLoader {
public:
    // Load ELF segments from a FAT12 file into virtual memory.
    // Returns the entry point address, or 0 on failure.
    static pt::uintptr_t load(const char* filename);
};
