#pragma once
#include "defs.h"
#include "boot.h"
#include "kernel.h"

class VMM;

extern VMM vmm;

struct kMemoryRegion
{
	pt::size_t length;
	kMemoryRegion* nextFreeChunk;
	kMemoryRegion* prevFreeChunk;
	kMemoryRegion* nextChunk;
	kMemoryRegion* prevChunk;
	bool free;
};


// 4k Pages - offset into page bits 11-0 of the address
struct PageTableL2 {
	pt::uintptr_t address;
};

// PageTables - bits 21-12 of the address
struct PageTableL3 {
	PageTableL2* l2PageTable;
	pt::uintptr_t flags;
};

// PageDirectory - bits 31-22 of the address
struct PageTableL4 {
	PageTableL3* l3_pages[1024];
	pt::uint8_t flags;
};

class VMM
{
private:
	kMemoryRegion* firstFreeMemoryRegion;
	PageTableL4* pageTables;

public:
	void *kmalloc(pt::size_t size);
	void *kcalloc(pt::size_t size);
	void kfree(void *);
	void map_address(pt::uintptr_t addr);

	pt::size_t memsize();

	static VMM* Instance() {
		return &vmm;
	}

	[[nodiscard]] PageTableL4* GetPageTableL3() const { return pageTables; }

	VMM() = default;

	VMM(memory_map_entry* mmap[], void *l4_page_address, const long address = 0x200000)
	{
		this->pageTables = static_cast<PageTableL4 *>(l4_page_address);
		pt::size_t top_size = 0;
		pt::uint64_t addr = 0;
		for(pt::size_t i = 0; i < MEMORY_ENTRIES_LIMIT; i++)
		{
			const auto entry = mmap[i];
			if (entry == nullptr)
				break;
			if (entry->type == 1)
			{
				if (entry->length > top_size)
				{
					top_size = entry->length - (address - entry->base_addr);
					addr = address;
				}
			}
		}
		if (!addr)
		{
			kernel_panic("Unable to find suitable memory region!", NoSuitableRegion);
		}
		klog("[VMM] Selected memory region %x, size: %x\n", addr, top_size);
		firstFreeMemoryRegion = reinterpret_cast<kMemoryRegion *>(addr);
		firstFreeMemoryRegion->length = top_size - sizeof(kMemoryRegion);
		firstFreeMemoryRegion->nextChunk = nullptr;
		firstFreeMemoryRegion->prevChunk = nullptr;
		firstFreeMemoryRegion->nextFreeChunk = nullptr;
		firstFreeMemoryRegion->prevFreeChunk = nullptr;
		firstFreeMemoryRegion->free = true;

		pageTables = static_cast<PageTableL4*>(kcalloc(sizeof(PageTableL4*)));

	}
};