#include "elf.h"
#include "elf_loader.h"
#include "fs/vfs.h"
#include "virtual.h"
#include "kernel.h"

// Storage for per-page permission flags (populated by load, consumed by create_elf_task).
pt::uint8_t ElfLoader::page_flags[MAX_ELF_PAGES];

pt::uintptr_t ElfLoader::load(const char* filename, pt::size_t* out_code_size) {
    File file;
    if (!VFS::open_file(filename, &file)) {
        klog("[ELF] File not found: %s\n", filename);
        return 0;
    }

    pt::uint32_t file_size = file.file_size;
    if (file_size == 0) {
        klog("[ELF] File is empty: %s\n", filename);
        VFS::close_file(&file);
        return 0;
    }

    // Read entire file into a kmalloc buffer
    pt::uint8_t* buf = static_cast<pt::uint8_t*>(vmm.kmalloc(file_size));
    if (!buf) {
        klog("[ELF] Failed to allocate buffer for file\n");
        VFS::close_file(&file);
        return 0;
    }

    pt::uint32_t bytes_read = VFS::read_file(&file, buf, file_size);
    VFS::close_file(&file);

    if (bytes_read < sizeof(Elf64_Ehdr)) {
        klog("[ELF] File too small to be a valid ELF\n");
        vmm.kfree(buf);
        return 0;
    }

    // Validate ELF magic
    const Elf64_Ehdr* ehdr = reinterpret_cast<const Elf64_Ehdr*>(buf);
    if (ehdr->e_ident[0] != ELFMAG0 || ehdr->e_ident[1] != ELFMAG1 ||
        ehdr->e_ident[2] != ELFMAG2 || ehdr->e_ident[3] != ELFMAG3) {
        klog("[ELF] Invalid ELF magic\n");
        vmm.kfree(buf);
        return 0;
    }

    if (ehdr->e_machine != EM_X86_64) {
        klog("[ELF] Not an x86_64 ELF (machine=%d)\n", (int)ehdr->e_machine);
        vmm.kfree(buf);
        return 0;
    }

    klog("[ELF] Loading '%s', entry=%x, %d program headers\n",
         filename, ehdr->e_entry, (int)ehdr->e_phnum);

    // Constants for staging area.
    static constexpr pt::uintptr_t ELF_STAGING_VA   = 0xFFFF800018000000ULL;
    static constexpr pt::uintptr_t USER_CODE_BASE_  = 0x400000ULL;
    static constexpr pt::size_t    MAX_STAGING_BYTES = 8ULL * 512 * 4096; // 16 MB

    // Zero the entire staging area and page_flags before loading so that
    // gaps between PT_LOAD segments don't carry stale data.
    memset(reinterpret_cast<void*>(ELF_STAGING_VA), 0, MAX_STAGING_BYTES);
    memset(page_flags, 0, sizeof(page_flags));

    // Walk program headers and load PT_LOAD segments.
    // Track VA span for out_code_size.
    const pt::uint8_t* phdr_base = buf + ehdr->e_phoff;
    pt::uintptr_t va_min = ~(pt::uintptr_t)0;
    pt::uintptr_t va_max = 0;

    for (pt::uint16_t i = 0; i < ehdr->e_phnum; i++) {
        const Elf64_Phdr* phdr = reinterpret_cast<const Elf64_Phdr*>(
            phdr_base + i * ehdr->e_phentsize);

        if (phdr->p_type != PT_LOAD) {
            continue;
        }

        klog("[ELF] PT_LOAD: vaddr=%x filesz=%d memsz=%d flags=%x\n",
             phdr->p_vaddr, (int)phdr->p_filesz, (int)phdr->p_memsz,
             (unsigned)phdr->p_flags);

        if (phdr->p_vaddr < va_min) va_min = phdr->p_vaddr;
        pt::uintptr_t seg_end = phdr->p_vaddr + phdr->p_memsz;
        if (seg_end > va_max) va_max = seg_end;

        // ELF has p_vaddr = USER_CODE_BASE + offset.  Write to the shared staging
        // area at ELF_STAGING_VA using the offset from USER_CODE_BASE so that
        // create_elf_task can later copy frames from ELF_STAGING_VA + i*4096.
        pt::uint8_t* dst = reinterpret_cast<pt::uint8_t*>(
            ELF_STAGING_VA + (phdr->p_vaddr - USER_CODE_BASE_));
        const pt::uint8_t* src = buf + phdr->p_offset;
        for (pt::uint64_t b = 0; b < phdr->p_filesz; b++) {
            dst[b] = src[b];
        }

        // Zero BSS (p_memsz - p_filesz bytes)
        for (pt::uint64_t b = phdr->p_filesz; b < phdr->p_memsz; b++) {
            dst[b] = 0;
        }

        // Record per-page permission flags.  A page touched by multiple
        // segments gets the union of their flags (conservative).
        pt::uintptr_t seg_page_start = (phdr->p_vaddr - USER_CODE_BASE_) / 4096;
        pt::uintptr_t seg_page_end   = (seg_end - USER_CODE_BASE_ + 4095) / 4096;
        pt::uint8_t flags = (pt::uint8_t)(phdr->p_flags & 0x07); // PF_X|PF_W|PF_R
        for (pt::uintptr_t pg = seg_page_start; pg < seg_page_end && pg < MAX_ELF_PAGES; pg++) {
            page_flags[pg] |= flags;
        }
    }

    if (out_code_size) {
        *out_code_size = (va_max > va_min) ? (pt::size_t)(va_max - va_min) : 0;
    }

    pt::uintptr_t entry = ehdr->e_entry;
    vmm.kfree(buf);
    return entry;
}
