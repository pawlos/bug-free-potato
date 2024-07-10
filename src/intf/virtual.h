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

class VMM
{
private:
	kMemoryRegion* firstFreeMemoryRegion;

public:
	void *kmalloc(pt::size_t size);
	void *kcalloc(pt::size_t size);
	void kfree(void *);

	static VMM* Instance() {
		return &vmm;
	}

	VMM(memory_map_entry* mmap[])
	{
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
					top_size = entry->length - (0x200000 - entry->base_addr);
					addr = 0x200000;//entry->base_addr;
				}
			}
		}
		if (!addr)
		{
			kernel_panic("Unable to find suitable memory region!", NoSuitableRegion);
		}
		klog("[VMM] Selected memory region %x, size: %x\n", addr, top_size);
		firstFreeMemoryRegion = (kMemoryRegion *)addr;
		firstFreeMemoryRegion->length = top_size - sizeof(kMemoryRegion);
		firstFreeMemoryRegion->nextChunk = nullptr;
		firstFreeMemoryRegion->prevChunk = nullptr;
		firstFreeMemoryRegion->nextFreeChunk = nullptr;
		firstFreeMemoryRegion->prevFreeChunk = nullptr;
		firstFreeMemoryRegion->free = true;
	}
};