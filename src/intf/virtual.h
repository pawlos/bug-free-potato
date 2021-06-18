#pragma once
#include "defs.h"
#include "boot.h"

struct kMemoryRegion
{
	size_t length;
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
	void *kmalloc(size_t size);
	void kfree(void *);

	VMM(memory_map_entry* mmap[])
	{
		size_t top_size = 0;
		uint64_t addr = NULL;
		for(size_t i = 0; i < MEMORY_ENTRIES_LIMIT; i++)
		{
			auto entry = mmap[i];			
			if (entry == NULL)
				break;
			if (entry->type == 1)
			{				
				if (entry->length > top_size)
				{
					top_size = entry->length;
					addr = entry->base_addr;
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
		firstFreeMemoryRegion->nextChunk = NULL;
		firstFreeMemoryRegion->prevChunk = NULL;
		firstFreeMemoryRegion->nextFreeChunk = NULL;
		firstFreeMemoryRegion->prevFreeChunk = NULL;
		firstFreeMemoryRegion->free = true;
	}
};