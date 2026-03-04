# Memory Management

Two subsystems work together: a **physical frame allocator** (bitmap-based) and a
**kernel heap** (linked-list allocator with coalescing). Both live in `VMM`
(`src/include/virtual.h`, `src/arch/x86_64/virtual.cpp`).

---

## Physical frame allocator

### Data structure

A flat **bit array** where each bit represents one 4 KB physical frame:

```
bit = 0  →  frame is free
bit = 1  →  frame is in use
```

The bitmap itself is placed at the start of the largest available memory region found
in the Multiboot2 memory map.

### Initialization

```cpp
void VMM::initialize_frame_allocator(memory_map_entry* mmap[]) {
    // 1. Find largest type-1 (usable) region above 1 MB
    // 2. Place bitmap at region base
    // 3. Mark all frames as free (memset 0)
    // 4. Mark first 16 MB as reserved:
    //      0x000000 – 0x100000  BIOS / GRUB low memory
    //      0x100000 – 0x18FFFFF kernel image + early heap
    //      0x1800000 – 0x19FFFFF ELF staging area (one 2 MB huge page)
}
```

### Allocation

```cpp
pt::uintptr_t VMM::allocate_frame() {
    // Scan bitmap from start; find first 0 bit; set it; return frame_index * 4096
    // Panics with NotAbleToAllocateMemory if bitmap is exhausted
}
```

Returns a **physical address** aligned to 4 KB.

### Release

```cpp
void VMM::free_frame(pt::uintptr_t frame) {
    // frame / 4096 → bit index; clear bit
}
```

---

## Kernel heap

### Chunk header

Every allocation (free or allocated) is preceded by a `kMemoryRegion`:

```cpp
struct kMemoryRegion {
    pt::size_t     size;        // usable bytes following this header
    kMemoryRegion* next_free;   // free list: next free chunk
    kMemoryRegion* prev_free;   // free list: previous free chunk
    kMemoryRegion* next_alloc;  // alloc list: next allocated chunk
    kMemoryRegion* prev_alloc;  // alloc list: previous allocated chunk
    bool           is_free;
};
```

Two doubly-linked lists are maintained: one of **free** chunks, one of **allocated**
chunks. They are kept in address order to enable O(n) coalescing.

### kmalloc

```cpp
void* VMM::kmalloc(pt::size_t size) {
    // 1. Round size up to 8-byte alignment
    // 2. Walk free list; find first chunk where chunk->size >= size
    // 3. If chunk->size > size + sizeof(kMemoryRegion) + minimum:
    //        split: create new free chunk from remainder
    // 4. Remove from free list; insert into alloc list
    // 5. Clear next_free / prev_free (prevent dangling pointers on free)
    // 6. Return pointer just past the header
}
```

### kcalloc

```cpp
void* VMM::kcalloc(pt::size_t size) {
    void* p = kmalloc(size);
    memset(p, 0, size);
    return p;
}
```

### krealloc

```cpp
void* VMM::krealloc(void* ptr, pt::size_t old_size, pt::size_t new_size) {
    void* p = kmalloc(new_size);
    memcpy(p, ptr, old_size < new_size ? old_size : new_size);
    kfree(ptr);
    return p;
}
```

### kfree

```cpp
void VMM::kfree(void* address) {
    kMemoryRegion* chunk = (kMemoryRegion*)address - 1;
    // Remove from alloc list
    // Re-insert into free list in address order
    chunk->is_free = true;
    // Coalesce with neighbours
    combineFreeSegments(prev_free_neighbour, chunk);
    combineFreeSegments(chunk, next_free_neighbour);
}
```

### Coalescing

```cpp
void combineFreeSegments(kMemoryRegion* a, kMemoryRegion* b) {
    // If a and b are adjacent in memory (a + sizeof(*a) + a->size == b):
    //   a->size += sizeof(kMemoryRegion) + b->size
    //   remove b from free list
}
```

Adjacent free chunks are merged immediately on every `kfree`, keeping the list compact.

---

## Page table management

### Structure (4-level paging)

```
PML4  (512 entries × 8 bytes = 4 KB)
 └── PDPT  (512 entries × 8 bytes = 4 KB)
      └── PD  (512 entries × 8 bytes = 4 KB)
           └── PT  (512 entries × 8 bytes = 4 KB)
                └── 4 KB physical page
```

Boot pages use 2 MB huge pages (PS bit set in PD entries); per-task ELF code uses
4 KB pages in a private PT.

### map_page

```cpp
void VMM::map_page(pt::uintptr_t virt, pt::uintptr_t phys, pt::uint64_t flags) {
    // Extract indices from VA:
    //   L4 = bits 47:39
    //   L3 = bits 38:30
    //   L2 = bits 29:21
    //   L1 = bits 20:12
    // Walk tables; allocate missing levels from the frame allocator
    // Write PTE: phys | flags
}
```

### unmap_page

```cpp
void VMM::unmap_page(pt::uintptr_t virt) {
    // Walk to PTE; clear it; invlpg virt (flush TLB entry)
}
```

### virt_to_phys_walk

```cpp
pt::uintptr_t VMM::virt_to_phys_walk(pt::uintptr_t virt) {
    // Walk all 4 levels; return (PT entry & ~0xFFF) | (virt & 0xFFF)
    // Returns 0 if any level is not present
}
```

### virt_to_phys (static, fast path)

```cpp
static pt::uintptr_t virt_to_phys(void* virt) {
    // If VA >= KERNEL_OFFSET: subtract KERNEL_OFFSET (high-half kernel)
    // Otherwise: return as-is (already physical for identity-mapped range)
}
```

This works because low-half identity mapping and high-half alias both resolve to the
same physical address, so the arithmetic is exact without a full table walk.

---

## Per-task address spaces

Each ELF task gets its own **private page tables** for the high-half code region:

```
kernel PML4[256] → boot PDPT  (shared, read-only kernel mappings)
  ↓ replaced for ELF tasks:
task  PML4[256] → private PDPT
                    PDPT[0]  → private PD
                               PD[192] → private PT
                                          PT[0..n] → private code frames
```

`PD[192]` corresponds to VA `0xFFFF800018000000` (the ELF staging area base). Each
entry in the private PT maps one 4 KB code frame copied from the ELF binary.

The kernel half (`PML4[0]`, which maps the low 4 GB identity range used for kernel
data/stack) is **shared** across all tasks — only the ELF code region is private.

### Frame lifecycle for ELF tasks

```
create_elf_task():
    allocate_frame() × 3         → private PDPT, PD, PT
    allocate_frame() × n_pages   → one frame per ELF load page
    memcpy from staging VA       → copy ELF content into frames
    task_exit() / exec():
    free_frame() × (3 + n_pages) → release all private frames
```

---

## Address space summary

| VA range | Content | Shared? |
|---|---|---|
| `0x0000_0000_0000_0000 – 0x0000_0007_FFFF_FFFF` | Identity map (all physical RAM) | Yes |
| `0xFFFF_8000_0000_0000 – 0xFFFF_8007_FFFF_FFFF` | High-half kernel alias | Yes (kernel PML4[256]) |
| `0xFFFF_8000_1800_0000` | ELF staging area | Replaced per ELF task |
| Heap | `vmm.kmalloc()` from high-half VA | Kernel-only |

---

## Limits

| Resource | Limit |
|---|---|
| Physical RAM supported | 4 GB (identity-mapped with 2 MB pages) |
| Heap | Fixed region; no growth mechanism currently |
| Max concurrent ELF tasks | 15 (MAX_TASKS=16 minus kernel task 0) |
| Frame bitmap | Sized at init from largest usable memory region |
