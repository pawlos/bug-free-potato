#include "elf.h"
#include "elf_loader.h"
#include "fat12.h"
#include "virtual.h"
#include "kernel.h"

static pt::uintptr_t page_align_down(pt::uintptr_t addr) {
    return addr & ~(ELF_PAGE_SIZE - 1);
}

static pt::uintptr_t page_align_up(pt::uintptr_t addr) {
    return (addr + ELF_PAGE_SIZE - 1) & ~(ELF_PAGE_SIZE - 1);
}

pt::uintptr_t ElfLoader::load(const char* filename) {
    FAT12_File file;
    if (!FAT12::open_file(filename, &file)) {
        klog("[ELF] File not found: %s\n", filename);
        return 0;
    }

    pt::uint32_t file_size = file.file_size;
    if (file_size == 0) {
        klog("[ELF] File is empty: %s\n", filename);
        FAT12::close_file(&file);
        return 0;
    }

    // Read entire file into a kmalloc buffer
    pt::uint8_t* buf = static_cast<pt::uint8_t*>(vmm.kmalloc(file_size));
    if (!buf) {
        klog("[ELF] Failed to allocate buffer for file\n");
        FAT12::close_file(&file);
        return 0;
    }

    pt::uint32_t bytes_read = FAT12::read_file(&file, buf, file_size);
    FAT12::close_file(&file);

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

    klog("[ELF] Loading '%s', entry=0x%x, %d program headers\n",
         filename, ehdr->e_entry, (int)ehdr->e_phnum);

    // Walk program headers and load PT_LOAD segments
    const pt::uint8_t* phdr_base = buf + ehdr->e_phoff;
    for (pt::uint16_t i = 0; i < ehdr->e_phnum; i++) {
        const Elf64_Phdr* phdr = reinterpret_cast<const Elf64_Phdr*>(
            phdr_base + i * ehdr->e_phentsize);

        if (phdr->p_type != PT_LOAD) {
            continue;
        }

        pt::uintptr_t vaddr_start = page_align_down(phdr->p_vaddr);
        pt::uintptr_t vaddr_end   = page_align_up(phdr->p_vaddr + phdr->p_memsz);

        klog("[ELF] PT_LOAD: vaddr=0x%x filesz=%d memsz=%d\n",
             phdr->p_vaddr, (int)phdr->p_filesz, (int)phdr->p_memsz);

        // Map pages for this segment
        for (pt::uintptr_t va = vaddr_start; va < vaddr_end; va += ELF_PAGE_SIZE) {
            pt::uintptr_t frame = vmm.allocate_frame();
            if (frame == 0) {
                klog("[ELF] Out of physical frames\n");
                vmm.kfree(buf);
                return 0;
            }
            vmm.map_page(va, frame, 0x03);  // present + writable
        }

        // Copy file data to virtual address
        pt::uint8_t* dst = reinterpret_cast<pt::uint8_t*>(phdr->p_vaddr);
        const pt::uint8_t* src = buf + phdr->p_offset;
        for (pt::uint64_t b = 0; b < phdr->p_filesz; b++) {
            dst[b] = src[b];
        }

        // Zero BSS (p_memsz - p_filesz bytes)
        for (pt::uint64_t b = phdr->p_filesz; b < phdr->p_memsz; b++) {
            dst[b] = 0;
        }
    }

    pt::uintptr_t entry = ehdr->e_entry;
    vmm.kfree(buf);
    return entry;
}
