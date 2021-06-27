#include "boot.h"

int toEightByteDivisible(uintptr_t addr) {
    int v = (addr % 8);
    if (v == 0) return 0;
    return 8 - v;    
}

boot_framebuffer* BootInfo::get_framebuffer()
{
	if (framebuffer == NULL) kernel_panic("BootInfo not parsed!", BootInfoNotParsed);

	return framebuffer;
}

memory_map_entry** BootInfo::get_memory_maps()
{
	if (memory_entry == NULL) kernel_panic("BootInfo not parsed!", BootInfoNotParsed);

	return memory_entry;
}

void BootInfo::log()
{
	klog("[BOOT] Boot info: Size: %x\n", size);

	if (cmd_line != NULL)
	{
		klog("[BOOT] Boot command line: %s\n", (const char *)&cmd_line->cmd);
	}
	if (loader_name != NULL)
	{
		klog("[BOOT] Boot loader name: %s\n", (const char *)&loader_name->name);
	}
	if (basic_mem != NULL)
	{
		klog("[BOOT] Basic memory info - Lower: %x, Upper: %x\n", basic_mem->mem_lower,
																basic_mem->mem_upper);
	}
	if (bios != NULL)
	{
		klog("[BOOT] BIOS boot device: %x\n", bios->biosdev);
	}
	if (mmap != NULL)
	{
		klog("[BOOT] Memory map - Entry size: %x, Entry version: %x\n", mmap->entry_size, mmap->entry_version);
		for (int i = 0; i<MEMORY_ENTRIES_LIMIT; i++)
		{
			auto entry = memory_entry[i];
			if (entry == NULL) break;
			klog("\t[BT] Memory base: %x, len: %x, type: %x\n", entry->base_addr, entry->length, entry->type);
		}
	}
	if (vbe != NULL)
	{
		klog("[BOOT] VBE info mode %x\n", vbe->vbe_mode);
	}
	if (framebuffer != NULL)
	{
		klog("[BOOT] Framebuffer addr: %x, width: %x, height: %x, bpp: %x, type: %x, pitch: %x\n",
							framebuffer->framebuffer_addr,
							framebuffer->framebuffer_width,
							framebuffer->framebuffer_height,
							framebuffer->framebuffer_bpp,
							framebuffer->framebuffer_type,
							framebuffer->framebuffer_pitch);
	}
	if (elf != NULL)
	{
		klog("[BOOT] Elf symbols: Num: %x, EntSize: %x\n", elf->num, elf->entsize);
	}
	if (apm_table != NULL)
	{
		klog("[BOOT] APM table - Version: %x\n", apm_table->version);
	}
	if (acpi != NULL)
	{
		klog("[BOOT] Boot ACPI\n");
	}
	if (physical != NULL)
	{
		klog("[BOOT] Load base address: %x\n", physical->load_base_addr);
	}
}

void BootInfo::parse(boot_info* boot_info)
{
	uintptr_t start = (uintptr_t)boot_info;
	size = boot_info->size;
	uintptr_t end = start + size;
	uintptr_t ptr = start + 8;

	while (ptr < end)
	{
		int padding_size = toEightByteDivisible(ptr);
		if (padding_size != 0)
		{
			ptr += padding_size;
		}		
		basic_tag* tag = (basic_tag *)ptr;
		switch(tag->type)
		{
			case BOOT_CMDLINE:
			{
				cmd_line = (boot_command_line *)ptr;
				break;
			}
			case BOOT_LOADER_NAME:
			{
				loader_name = (boot_loader_name *)ptr;
				break;
			}
			case BOOT_BASIC_MEM:
			{
				basic_mem = (boot_basic_memory *)ptr;
				break;
			}
			case BOOT_BIOS:
			{
				bios = (boot_bios_device *)ptr;
				break;
			}
			case BOOT_MMAP:
			{
				mmap = (boot_memory_map *)ptr;
				uintptr_t mem_current = (uintptr_t)&mmap->entries;
				const uintptr_t mem_end   = mem_current + mmap->size - 4*sizeof(uint32_t);
				int i = 0;
				for (int i = 0; i < MEMORY_ENTRIES_LIMIT; i++)
				{
					memory_entry[i] = NULL;
				}
				while (mem_current < mem_end)
				{
					if (i >= MEMORY_ENTRIES_LIMIT) kernel_panic("Memory entries limit reached", MemEntriesLimitReached);
					memory_map_entry* entry = (memory_map_entry*)mem_current;
					memory_entry[i] = entry;
					i++;
					mem_current += mmap->entry_size;
				}
				break;
			}
			case BOOT_VBE_INFO:
			{
				vbe = (boot_vbe_info *)ptr;
				break;
			}
			case BOOT_FRAMEBUFFER:
			{
				framebuffer = (boot_framebuffer *)ptr;
				break;
			}
			case BOOT_ELF_SYMBOLS:
			{
				elf = (boot_elf_symbols *)ptr;
				break;
			}
			case BOOT_APM_TABLE:
			{
				apm_table = (boot_apm_table *)ptr;
				break;
			}
			case BOOT_ACPI:
			{
				acpi = (boot_acpi *)ptr;
				break;
			}
			case BOOT_PHYSICAL:
			{
				physical = (boot_loader_physical_address *)ptr;
				break;
			}	
			default:
				klog("[BOOT] No parser for %d", tag->type);
				break;
		}
		ptr += tag->size;
	}
	this->log();
}