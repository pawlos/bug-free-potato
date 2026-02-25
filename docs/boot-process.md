# Boot Process

The kernel boots via **GRUB with a Multiboot2 header**. Control passes through three
distinct phases before `kernel_main` is called.

## Phase 1 — 32-bit protected mode (main.asm)

`src/impl/x86_64/boot/main.asm` is the first code to run after GRUB hands off control.

### Entry checks

```asm
; Verify Multiboot2 magic value in EAX
cmp eax, 0x36d76289
jne .no_multiboot

; Check CPUID availability by toggling EFLAGS.ID
; Check long mode via CPUID 0x80000001 leaf, bit 29
```

If either check fails, the boot halts with an error message on the VGA text display.

### Paging setup (before enabling long mode)

Four 2MB-page page directories (PDs) are pre-filled in BSS, covering the first 4 GB:

```asm
; Each entry: (block_index × 2MB) | 0x87
;   0x87 = Present | Read/Write | Page Size (2MB) | User
; 4 PDs × 512 entries = 2048 × 2MB = 4 GB total

fill_pd:
    ; Fill entries 0..511 in each of the 4 PD tables
    mov [page_table_l2 + eax * 8], ebx
    ...

; Wire PDs into PDPT
mov dword [page_table_l3 + 0],  page_table_l2_0 | 0x03
mov dword [page_table_l3 + 8],  page_table_l2_1 | 0x03
mov dword [page_table_l3 + 16], page_table_l2_2 | 0x03
mov dword [page_table_l3 + 24], page_table_l2_3 | 0x03

; Wire PDPT into PML4 twice:
;   PML4[0]   → low-half  (VA 0x0000_0000_0000_0000 – 0x0000_0007_FFFF_FFFF)
;   PML4[256] → high-half (VA 0xFFFF_8000_0000_0000 – 0xFFFF_8007_FFFF_FFFF)
mov dword [page_table_l4 + 0],   page_table_l3 | 0x03
mov dword [page_table_l4 + 2048], page_table_l3 | 0x03
```

The **same PDPT** appears at both entries, so low-half and high-half VAs resolve to
identical physical addresses (identity mapping through 4 GB).

### Transition to 64-bit long mode

```asm
mov cr3, page_table_l4      ; load PML4

; CR4: enable PAE
or eax, 1 << 5
mov cr4, eax

; EFER MSR (0xC0000080): set Long Mode Enable (bit 8)
rdmsr                        ; read EFER
or eax, 1 << 8
wrmsr                        ; write back

; CR0: set Protected Mode (bit 0) + Paging (bit 31)
or eax, (1 << 31) | (1 << 0)
mov cr0, eax

; Far jump to 64-bit code segment
jmp gdt64.code:long_mode_start
```

---

## Phase 2 — 64-bit entry (boot.cpp)

`src/impl/x86_64/boot/boot.cpp` is the C++ `kernel_main` called from the 64-bit asm stub.

### boot_info parsing

The Multiboot2 info pointer (saved from EBX in asm) is passed to `BootInfo`:

```cpp
BootInfo boot_info_obj(boot_info);
```

`BootInfo` iterates the 8-byte-aligned tag list and extracts:

| Tag type | What is stored |
|---|---|
| `BOOT_MMAP` | Array of `memory_map_entry*` (base, length, type) |
| `BOOT_FRAMEBUFFER` | Framebuffer address, width, height, pitch, BPP |
| `BOOT_CMDLINE` | Kernel command line string |
| `BOOT_ACPI` | RSDP pointer for ACPI shutdown/reboot |

Panics with `BootInfoNotParsed` if the framebuffer tag is absent.

### Memory map and frame allocator

```cpp
VMM vmm(boot_info_obj.get_memory_maps());
```

`VMM::initialize_frame_allocator(mmap[])`:
1. Finds the largest **available** (type 1) memory region above 1 MB.
2. Allocates a **bitmap** (1 bit per 4 KB frame) inside that region.
3. Marks the first 16 MB of physical memory as reserved (BIOS, GRUB tables, staging area).

### Initial heap

The `VMM` constructor seeds the heap with a single large `kMemoryRegion`:

```cpp
struct kMemoryRegion {
    pt::size_t   size;          // usable bytes in this chunk
    kMemoryRegion* next_free;   // free-list forward pointer
    kMemoryRegion* prev_free;   // free-list back pointer
    kMemoryRegion* next_alloc;  // alloc-list forward pointer
    kMemoryRegion* prev_alloc;  // alloc-list back pointer
    bool          is_free;
};
```

The first region covers a fixed range above the kernel BSS, inside the already-mapped
identity range.

### Device and subsystem initialization order

```cpp
// 1. Framebuffer and terminal
Framebuffer fb(framebuffer_tag);
FbTerm      fbterm(&fb, font_data);

// 2. Interrupt descriptor table
IDT::initialize();       // sets up 32 exception + 5 IRQ + 2 syscall gates, loads IDTR

// 3. PS/2 keyboard and mouse
keyboard_init();
mouse_init();

// 4. PCI bus scan
PCI::enumerate();

// 5. IDE disk
ide.initialize();

// 6. AC97 audio
ac97.initialize();

// 7. FAT filesystem
fat12::mount();   // tries FAT12 first
fat32::mount();   // then FAT32

// 8. Task scheduler
TaskScheduler::initialize();   // creates kernel task 0, captures FPU template

// 9. Shell (runs in kernel task 0)
Shell::run();
```

---

## Phase 3 — Kernel execution

After `Shell::run()` the kernel enters its interactive loop. Timer interrupts (IRQ 0)
drive the preemptive scheduler; user tasks are spawned with `exec <name>.elf` from the
shell.

### Virtual address map after boot

```
0x0000_0000_0000_0000 – 0x0000_0007_FFFF_FFFF   low-half identity map (4 GB, 2MB pages)
0xFFFF_8000_0000_0000 – 0xFFFF_8007_FFFF_FFFF   high-half kernel alias (same physical)
0xFFFF_8000_1800_0000                            ELF staging area (1 × 2MB huge page)
```

The kernel itself is linked at `0xFFFF800000100000` (high-half).
Physical memory starts at 1 MB (0x100000).

---

## Key constants

| Symbol | Value | Meaning |
|---|---|---|
| `KERNEL_OFFSET` | `0xFFFF800000000000` | High-half base VA |
| `ELF_STAGING_VA` | `0xFFFF800018000000` | ELF load buffer |
| `ELF_STAGING_PA` | `0x18000000` | Corresponding PA (384 MB) |
| PML4 index for kernel | 256 | `PML4[256]` → kernel PDPT |

---

## GDT layout (64-bit)

| Selector | Type | DPL | Notes |
|---|---|---|---|
| `0x08` | Code | 0 | Kernel code (used for long-mode far jump) |
| `0x10` | Data | 0 | Kernel data / stack |
| `0x18` | Code | 3 | User code (ring-3 `iretq` CS) |
| `0x20` | Data | 3 | User data (ring-3 `iretq` SS) |
| TSS | System | 0 | 64-bit TSS (RSP0 updated per task switch) |

The TSS `RSP0` field is updated on every context switch to a user-mode task; the CPU
reads it on the ring-3 → ring-0 transition to know where to set the kernel stack.
