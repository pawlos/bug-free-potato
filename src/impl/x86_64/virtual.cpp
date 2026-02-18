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
    return this->firstFreeMemoryRegion->length;
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
            if (currentMemorySegment->prevChunk != nullptr)
                currentMemorySegment->prevChunk->nextFreeChunk = currentMemorySegment->nextChunk;
            if (currentMemorySegment->nextChunk != nullptr)
                currentMemorySegment->nextChunk->prevFreeChunk = currentMemorySegment->prevChunk;

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
        if (b->nextChunk != nullptr) {
            b->nextChunk->prevFreeChunk = a;
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
        if (a->nextChunk != nullptr) {
            a->nextChunk->prevFreeChunk = b;
        }
        if (a->nextFreeChunk != nullptr) {
            a->nextFreeChunk->prevFreeChunk = b;
        }
    }
}

void VMM::kfree(void *address)
{
    if (address == nullptr) {
        kernel_panic("Address should not be null", NullRefNotExpected);
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
        klog("[VMM] L3 table not present at index %d, cannot map page\n", l4_idx);
        return;
    }

    PageTableL3* l3_table = pageTables->l3_pages[l4_idx];

    // Get L2 table
    if (l3_table[l3_idx].l2PageTable == nullptr)
    {
        klog("[VMM] L2 table not present at L3[%d], cannot map page\n", l3_idx);
        return;
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
