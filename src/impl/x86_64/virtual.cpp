#include "virtual.h"

void memset(void* dst, pt::uint64_t value, const pt::size_t size)
{
    if (size < 8)
    {
        const auto* valuePtr = (pt::uint8_t*)&value;
        for (auto* ptr = (pt::uint8_t*)dst; ptr < (pt::uint8_t*)((pt::uint64_t)dst + size); ptr++)
        {
            *ptr = *valuePtr;
            valuePtr++;
        }

        return;
    }

    const pt::uint64_t proceedingBytes = size % 8;
    const pt::uint64_t newnum = size - proceedingBytes;

    for (auto* ptr = (pt::uint64_t*)dst; ptr < (pt::uint64_t*)((pt::uint64_t)dst + size); ptr++)
    {
        *ptr = value;
    }

    auto* valPtr = (pt::uint8_t*)&value;
    for (auto* ptr = (pt::uint8_t*)((pt::uint64_t)dst+newnum); ptr < (pt::uint8_t*)((pt::uint64_t)dst + size); ptr++)
    {
        *ptr = *valPtr;
        valPtr++;
    }
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

            return currentMemorySegment + 1;
        }
        if (currentMemorySegment->nextFreeChunk == nullptr)
        {
            kernel_panic("Not able to allocated more memory.", NotAbleToAllocateMemory);
        }
        currentMemorySegment = currentMemorySegment->nextFreeChunk;
    }
}

void* VMM::kcalloc(const pt::size_t size)
{
    klog("[VMM] Callocing %d bytes memory.\n", size);
    void* ptr = kmalloc(size);
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

    kMemoryRegion* currentMemorySegment = static_cast<kMemoryRegion *>(address) - 1;
    currentMemorySegment->free = true;


    klog("[VMM] Freeing bytes memory at %p.\n", address);
    if (currentMemorySegment < firstFreeMemoryRegion)
    {
        firstFreeMemoryRegion = currentMemorySegment;
    }

    if (currentMemorySegment->nextFreeChunk != nullptr)
    {
        if (currentMemorySegment->nextFreeChunk->prevFreeChunk < currentMemorySegment)
        {
            currentMemorySegment->nextFreeChunk->prevFreeChunk = currentMemorySegment;
        }
    }
    if (currentMemorySegment->prevFreeChunk != nullptr)
    {
        if (currentMemorySegment->prevFreeChunk->nextFreeChunk > currentMemorySegment)
        {
            currentMemorySegment->prevFreeChunk->nextFreeChunk = currentMemorySegment;
        }
    }
    if (currentMemorySegment->nextChunk != nullptr)
    {
        currentMemorySegment->nextChunk->prevChunk = currentMemorySegment;
        if (currentMemorySegment->nextChunk->free)
            combineFreeSegments(currentMemorySegment, currentMemorySegment->nextChunk);
    }
    if (currentMemorySegment->prevChunk != nullptr)
    {
        currentMemorySegment->prevChunk->nextChunk = currentMemorySegment;
        if (currentMemorySegment->prevChunk->free)
            combineFreeSegments(currentMemorySegment, currentMemorySegment->prevChunk);
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

    frame_allocator_ready = true;
    klog("[VMM] Frame allocator ready: %d frames, %d bytes bitmap\n", num_frames, frame_bitmap_size);
}

pt::uintptr_t VMM::allocate_frame()
{
    // Lazy initialize if needed
    if (!frame_allocator_ready)
    {
        initialize_frame_allocator(cached_mmap);
    }

    if (!frame_allocator_ready || frame_bitmap == nullptr)
    {
        kernel_panic("Frame allocator initialization failed", NotAbleToAllocateMemory);
    }

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
                    return frame_num * 4096;
                }
            }
        }
    }

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
        frame_bitmap[byte_idx] &= ~(1 << bit_idx);
    }
}
