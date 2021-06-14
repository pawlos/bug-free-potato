#pragma once
#include "defs.h"
#include "boot.h"

class VMM
{
private:
	uint64_t memory_region;
	size_t memory_size;

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
			kernel_panic("Unable to find suitable memory region!", 252);
		}
		memory_region = addr;
		memory_size = top_size;
		klog("VMM: Selected memory region %x, size: %x", memory_region, memory_size);
	}
};