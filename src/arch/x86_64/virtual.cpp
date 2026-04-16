#include "virtual.h"

void memset(void* dst, pt::uint64_t value, const pt::size_t size)
{
    const pt::uint8_t fill_byte = (pt::uint8_t)value;

    if (size < 8)
    {
        for (auto* ptr = (pt::uint8_t*)dst; ptr < (pt::uint8_t*)((pt::uint64_t)dst + size); ptr++)
            *ptr = fill_byte;
        return;
    }

    // Build an 8-byte pattern from the single fill byte.
    pt::uint64_t pattern = fill_byte;
    pattern |= pattern << 8;
    pattern |= pattern << 16;
    pattern |= pattern << 32;

    const pt::uint64_t aligned_size = size - (size % 8);

    for (auto* ptr = (pt::uint64_t*)dst; ptr < (pt::uint64_t*)((pt::uint64_t)dst + aligned_size); ptr++)
        *ptr = pattern;

    // Handle remaining 1–7 trailing bytes.
    for (auto* ptr = (pt::uint8_t*)((pt::uint64_t)dst + aligned_size); ptr < (pt::uint8_t*)((pt::uint64_t)dst + size); ptr++)
        *ptr = fill_byte;
}

void* memcpy(void* dest, const void* src, pt::size_t n) {
    pt::uint8_t* d = (pt::uint8_t*)dest;
    const pt::uint8_t* s = (const pt::uint8_t*)src;
    
    while (n--) {
        *d++ = *s++;
    }
    
    return dest;
}

pt::size_t VMM::memsize() {
    pt::size_t total = 0;
    kMemoryRegion* r = firstFreeMemoryRegion;
    while (r != nullptr) {
        total += r->length;
        r = r->nextFreeChunk;
    }
    return total;
}

void* VMM::kmalloc(pt::size_t size)
{
    klog("[VMM] Allocating %d bytes memory.\n", size);
    const pt::uint64_t remainder = size % 8;
    size -= remainder;
    if (remainder != 0) size += 8;

    kMemoryRegion* currentMemorySegment = this->firstFreeMemoryRegion;

    while (true)
    {
        if (currentMemorySegment->length >= size)
        {
            if (currentMemorySegment->length > size + sizeof(kMemoryRegion))
            {
                auto* newMemoryRegion =
                        (kMemoryRegion*)((pt::uint64_t) currentMemorySegment +
                                        sizeof(kMemoryRegion) + size);
                newMemoryRegion->free = true;
                newMemoryRegion->length = (pt::uint64_t)currentMemorySegment->length - (sizeof(kMemoryRegion) + size);
                newMemoryRegion->nextFreeChunk = currentMemorySegment->nextFreeChunk;
                newMemoryRegion->nextChunk = currentMemorySegment->nextChunk;
                newMemoryRegion->prevChunk = currentMemorySegment;
                newMemoryRegion->prevFreeChunk = currentMemorySegment->prevFreeChunk;

                currentMemorySegment->nextFreeChunk = newMemoryRegion;
                currentMemorySegment->nextChunk = newMemoryRegion;
                currentMemorySegment->length = size;
            }
            if (currentMemorySegment == firstFreeMemoryRegion)
            {
                firstFreeMemoryRegion = currentMemorySegment->nextFreeChunk;
            }
            currentMemorySegment->free = false;

            if (currentMemorySegment->prevFreeChunk != nullptr)
                currentMemorySegment->prevFreeChunk->nextFreeChunk = currentMemorySegment->nextFreeChunk;
            if (currentMemorySegment->nextFreeChunk != nullptr)
                currentMemorySegment->nextFreeChunk->prevFreeChunk = currentMemorySegment->prevFreeChunk;

            // Clear free-list links so kfree won't follow stale pointers.
            currentMemorySegment->prevFreeChunk = nullptr;
            currentMemorySegment->nextFreeChunk = nullptr;

            return currentMemorySegment + 1;
        }
        if (currentMemorySegment->nextFreeChunk == nullptr)
        {
            klog("[VMM] Out of memory: requested %d bytes\n", (int)size);
            return nullptr;
        }
        currentMemorySegment = currentMemorySegment->nextFreeChunk;
    }
}

void* VMM::kcalloc(const pt::size_t size)
{
    klog("[VMM] Callocing %d bytes memory.\n", size);
    void* ptr = kmalloc(size);
    if (ptr == nullptr) return nullptr;
    memset(ptr, '\0', size);
    return ptr;
}

void* VMM::krealloc(void *ptr, const pt::size_t old_size, const pt::size_t new_size)
{
    if (ptr == nullptr) {
        return kmalloc(new_size);
    }

    if (new_size == 0) {
        kfree(ptr);
        return nullptr;
    }

    // Allocate new block
    void* new_ptr = kmalloc(new_size);
    if (new_ptr == nullptr) {
        return nullptr;
    }

    // Copy data (copy the smaller of old_size and new_size)
    pt::size_t copy_size = old_size < new_size ? old_size : new_size;
    memcpy(new_ptr, ptr, copy_size);

    // Free old block
    kfree(ptr);

    klog("[VMM] Realloced %d -> %d bytes\n", old_size, new_size);
    return new_ptr;
}

void combineFreeSegments(kMemoryRegion* a, kMemoryRegion* b)
{
    if (a == nullptr) return;
    if (b == nullptr) return;

    if (a < b)
    {
        a->length += b->length + sizeof(kMemoryRegion);
        a->nextChunk = b->nextChunk;
        a->nextFreeChunk = b->nextFreeChunk;
        if (b->nextChunk != nullptr) {
            b->nextChunk->prevChunk = a;
        }
        if (b->nextFreeChunk != nullptr) {
            b->nextFreeChunk->prevFreeChunk = a;
        }
    }
    else
    {
        b->length += a->length + sizeof(kMemoryRegion);
        b->nextChunk = a->nextChunk;
        b->nextFreeChunk = a->nextFreeChunk;
        if (a->nextChunk != nullptr) {
            a->nextChunk->prevChunk = b;
        }
        if (a->nextFreeChunk != nullptr) {
            a->nextFreeChunk->prevFreeChunk = b;
        }
    }
}

void VMM::kfree(void *address)
{
    if (address == nullptr) {
        return;
    }

    kMemoryRegion* seg = static_cast<kMemoryRegion *>(address) - 1;
    seg->free = true;

    klog("[VMM] Freeing bytes memory at %p.\n", address);

    // Insert seg into the address-ordered free list.
    // prevFreeChunk/nextFreeChunk were cleared to null at allocation time,
    // so we never follow stale pointers here.
    kMemoryRegion* prev = nullptr;
    kMemoryRegion* next = firstFreeMemoryRegion;
    while (next != nullptr && next < seg) {
        prev = next;
        next = next->nextFreeChunk;
    }

    seg->prevFreeChunk = prev;
    seg->nextFreeChunk = next;
    if (prev != nullptr) prev->nextFreeChunk = seg;
    else firstFreeMemoryRegion = seg;
    if (next != nullptr) next->prevFreeChunk = seg;

    // Coalesce with physically adjacent next block if it is free.
    if (seg->nextChunk != nullptr) {
        seg->nextChunk->prevChunk = seg;
        if (seg->nextChunk->free)
            combineFreeSegments(seg, seg->nextChunk);
    }
    // Coalesce with physically adjacent prev block if it is free.
    if (seg->prevChunk != nullptr) {
        seg->prevChunk->nextChunk = seg;
        if (seg->prevChunk->free)
            combineFreeSegments(seg, seg->prevChunk);
    }
}

void VMM::map_page(pt::uintptr_t virt, pt::uintptr_t phys, pt::uint64_t flags)
{
    // Extract indices from virtual address
    pt::size_t l4_idx = (virt >> 39) & 0x1FF;
    pt::size_t l3_idx = (virt >> 30) & 0x1FF;
    pt::size_t l2_idx = (virt >> 21) & 0x1FF;

    // Get or allocate L3 table
    if (pageTables->l3_pages[l4_idx] == nullptr)
    {
        // Allocate new L3 table
        pt::uintptr_t l3_frame = allocate_frame();
        PageTableL3* l3_table = (PageTableL3*)l3_frame;  // Identity mapping

        // Zero the table
        for (int i = 0; i < 1024; i++)
        {
            l3_table[i].l2PageTable = nullptr;
            l3_table[i].flags = 0;
        }

        pageTables->l3_pages[l4_idx] = l3_table;
        klog("[VMM] Allocated L3 table at phys %x for virt %x\n", l3_frame, virt);
    }

    PageTableL3* l3_table = pageTables->l3_pages[l4_idx];

    // Get or allocate L2 table
    if (l3_table[l3_idx].l2PageTable == nullptr)
    {
        // Allocate new L2 table
        pt::uintptr_t l2_frame = allocate_frame();
        PageTableL2* l2_table = (PageTableL2*)l2_frame;  // Identity mapping

        // Zero the table
        for (int i = 0; i < 512; i++)  // L2 has 512 entries (4K pages)
        {
            l2_table[i].address = 0;
        }

        l3_table[l3_idx].l2PageTable = l2_table;
        l3_table[l3_idx].flags = flags | 0x01;  // Present + inherit flags
        klog("[VMM] Allocated L2 table at phys %x for virt %x\n", l2_frame, virt);
    }

    // Set the page table entry
    PageTableL2* l2_table = l3_table[l3_idx].l2PageTable;
    l2_table[l2_idx].address = phys | flags | 0x01;

    klog("[VMM] Mapped virt %x to phys %x\n", virt, phys);
}

void VMM::unmap_page(pt::uintptr_t virt)
{
    // Extract indices from virtual address
    pt::size_t l4_idx = (virt >> 39) & 0x1FF;
    pt::size_t l3_idx = (virt >> 30) & 0x1FF;
    pt::size_t l2_idx = (virt >> 21) & 0x1FF;

    if (pageTables->l3_pages[l4_idx] == nullptr)
    {
        klog("[VMM] L3 table not found for virt %x\n", virt);
        return;
    }

    PageTableL3* l3_table = pageTables->l3_pages[l4_idx];

    if (l3_table[l3_idx].l2PageTable == nullptr)
    {
        klog("[VMM] L2 table not found for virt %x\n", virt);
        return;
    }

    // Clear the page table entry
    PageTableL2* l2_table = l3_table[l3_idx].l2PageTable;
    l2_table[l2_idx].address = 0;

    // Flush TLB entry using invlpg instruction
    asm volatile("invlpg [%0]" : : "r"(virt));

    klog("[VMM] Unmapped virt %x\n", virt);
}

pt::uintptr_t VMM::virt_to_phys_walk(pt::uintptr_t virt) const
{
    // Extract indices from virtual address
    pt::size_t l4_idx = (virt >> 39) & 0x1FF;
    pt::size_t l3_idx = (virt >> 30) & 0x1FF;
    pt::size_t l2_idx = (virt >> 21) & 0x1FF;
    pt::size_t offset = virt & 0xFFF;

    if (pageTables->l3_pages[l4_idx] == nullptr)
    {
        return 0;
    }

    PageTableL3* l3_table = pageTables->l3_pages[l4_idx];

    if (l3_table[l3_idx].l2PageTable == nullptr)
    {
        return 0;
    }

    PageTableL2* l2_table = l3_table[l3_idx].l2PageTable;
    pt::uintptr_t phys = l2_table[l2_idx].address & ~0xFFF;

    if (phys == 0)
    {
        return 0;
    }

    return phys | offset;
}

void VMM::initialize_frame_allocator(memory_map_entry* mmap[])
{
    if (frame_allocator_ready) return;

    // Calculate total memory
    pt::size_t total_memory = 0;
    for (pt::size_t i = 0; i < MEMORY_ENTRIES_LIMIT; i++)
    {
        if (mmap[i] == nullptr) break;
        if (mmap[i]->type == 1)  // Free memory
        {
            total_memory += mmap[i]->length;
        }
    }

    // Calculate bitmap size (1 bit per 4KB frame)
    pt::size_t num_frames = total_memory / 4096;
    frame_bitmap_size = (num_frames + 7) / 8;

    // Allocate bitmap from heap
    frame_bitmap = (pt::uint8_t*)kmalloc(frame_bitmap_size);
    if (frame_bitmap == nullptr)
    {
        kernel_panic("Failed to allocate frame bitmap", NotAbleToAllocateMemory);
    }

    // Initialize bitmap - all frames free
    for (pt::size_t i = 0; i < frame_bitmap_size; i++) {
        frame_bitmap[i] = 0;
    }

    // Mark the first 16MB as reserved so that allocate_frame() never returns
    // a physical address that overlaps the kernel binary or the heap.
    // The kernel binary starts at 1MB; the heap follows immediately and can
    // grow to several MB, so 16MB gives plenty of headroom.
    pt::size_t kernel_frames = (16 * 1024 * 1024) / 4096;
    for (pt::size_t i = 0; i < kernel_frames; i++)
    {
        pt::size_t byte_idx = i / 8;
        pt::uint8_t bit_idx = i % 8;
        if (byte_idx < frame_bitmap_size)
        {
            frame_bitmap[byte_idx] |= (1 << bit_idx);
        }
    }

    // Reserve the ELF staging area (16MB at PA 0x18000000) so that
    // allocate_frame() never returns frames from this region.
    // create_elf_task copies code pages FROM the staging area while
    // simultaneously calling allocate_frame() for destination frames;
    // without this reservation, allocated frames can land in the staging
    // range and corrupt the source data mid-copy.
    constexpr pt::uintptr_t ELF_STAGING_PHYS = 0x18000000;
    constexpr pt::size_t    ELF_STAGING_SIZE  = 16 * 1024 * 1024;
    pt::size_t staging_start = ELF_STAGING_PHYS / 4096;
    pt::size_t staging_end   = (ELF_STAGING_PHYS + ELF_STAGING_SIZE) / 4096;
    for (pt::size_t i = staging_start; i < staging_end; i++)
    {
        pt::size_t byte_idx = i / 8;
        pt::uint8_t bit_idx = i % 8;
        if (byte_idx < frame_bitmap_size)
        {
            frame_bitmap[byte_idx] |= (1 << bit_idx);
        }
    }

    frame_allocator_ready = true;
    klog("[VMM] Frame allocator ready: %d frames, %d bytes bitmap\n", num_frames, frame_bitmap_size);
}

pt::uintptr_t VMM::allocate_frame()
{
    // Frame allocator is initialized eagerly in the VMM ctor, so this flag
    // must be true by the time any caller reaches us.
    if (!frame_allocator_ready || frame_bitmap == nullptr)
    {
        kernel_panic("Frame allocator not initialized", NotAbleToAllocateMemory);
    }

    // Disable interrupts for the bitmap scan-and-set.  Without this,
    // the timer can preempt between testing a bit and setting it;
    // another task's SYS_MMAP → allocate_frame() would then see the
    // same bit as free and return the same physical frame — causing
    // two tasks to silently share a page and corrupt each other's data.
    // pushfq/popfq preserves the previous IF state so this is safe to
    // call from both syscall context (IF=0) and kernel context (IF=1).
    pt::uint64_t saved_flags;
    asm volatile("pushfq; pop %0; cli" : "=r"(saved_flags) :: "memory");

    // Find first free frame
    for (pt::size_t byte_idx = 0; byte_idx < frame_bitmap_size; byte_idx++)
    {
        if (frame_bitmap[byte_idx] != 0xFF)
        {
            // Found byte with free bits
            for (pt::uint8_t bit_idx = 0; bit_idx < 8; bit_idx++)
            {
                if (!(frame_bitmap[byte_idx] & (1 << bit_idx)))
                {
                    // Mark frame as used
                    frame_bitmap[byte_idx] |= (1 << bit_idx);
                    pt::size_t frame_num = byte_idx * 8 + bit_idx;
                    asm volatile("push %0; popfq" : : "r"(saved_flags) : "memory");
                    return frame_num * 4096;
                }
            }
        }
    }

    asm volatile("push %0; popfq" : : "r"(saved_flags) : "memory");
    kernel_panic("No free physical frames", NotAbleToAllocateMemory);
    return 0;
}

void VMM::free_frame(pt::uintptr_t frame)
{
    if (!frame_allocator_ready || frame_bitmap == nullptr) return;

    pt::size_t frame_num = frame / 4096;
    pt::size_t byte_idx = frame_num / 8;
    pt::uint8_t bit_idx = frame_num % 8;

    if (byte_idx < frame_bitmap_size)
    {
        pt::uint64_t saved_flags;
        asm volatile("pushfq; pop %0; cli" : "=r"(saved_flags) :: "memory");
        frame_bitmap[byte_idx] &= ~(1 << bit_idx);
        asm volatile("push %0; popfq" : : "r"(saved_flags) : "memory");
    }
}

pt::size_t VMM::get_total_mem() const {
    if (!frame_allocator_ready || !frame_bitmap) return 0;
    return frame_bitmap_size * 8 * 4096;
}

pt::size_t VMM::get_free_mem() const {
    if (!frame_allocator_ready || !frame_bitmap) return 0;
    pt::size_t free_frames = 0;
    for (pt::size_t i = 0; i < frame_bitmap_size; i++) {
        pt::uint8_t byte = frame_bitmap[i];
        for (int b = 0; b < 8; b++)
            if (!(byte & (1 << b))) free_frames++;
    }
    return free_frames * 4096;
}
