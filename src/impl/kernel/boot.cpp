#include "boot.h"

int toEightByteDivisible(uintptr_t addr) {
    int v = (addr % 8);
    if (v == 0) return 0;
    return 8 - v;    
}

boot_framebuffer* BootInfo::get_framebuffer()
{
	if (framebuffer == NULL) kernel_panic("BootInfo not parsed!", 253);

	return framebuffer;
}

void BootInfo::print(ComDevice* debug)
{
	debug->print_str("Boot info: \n");
	debug->print_str("Size: %x\n", size);

	if (cmd_line != NULL)
	{
		debug->print_str("Boot command line: %s\n", (const char *)&cmd_line->cmd);
	}
	if (loader_name != NULL)
	{
		debug->print_str("Boot loader name: %s\n", (const char *)&loader_name->name);
	}
	if (basic_mem != NULL)
	{
		debug->print_str("Basic memory info - Lower: %x, Upper: %x\n", basic_mem->mem_lower,
																	   basic_mem->mem_upper);
	}
	if (bios != NULL)
	{
		debug->print_str("BIOS boot device: %x\n", bios->biosdev);
	}
	if (mmap != NULL)
	{
		debug->print_str("Memory map - Entry size: %x, Entry version: %x\n", mmap->entry_size, mmap->entry_version);
		for (int i = 0; i<MEMORY_ENTRIES_LIMIT; i++)
		{
			auto entry = memory_entry[i];
			if (entry == NULL) break;
			debug->print_str("\tMemory base: %x, len: %x, type: %x\n", entry->base_addr, entry->length, entry->type);
		}
	}
	if (vbe != NULL)
	{
		debug->print_str("VBE info mode %x\n", vbe->vbe_mode);
	}
	if (framebuffer != NULL)
	{
		debug->print_str("Framebuffer addr: %x, width: %x, height: %x, bpp: %x, type: %x, pitch: %x\n",
							framebuffer->framebuffer_addr,
							framebuffer->framebuffer_width,
							framebuffer->framebuffer_height,
							framebuffer->framebuffer_bpp,
							framebuffer->framebuffer_type,
							framebuffer->framebuffer_pitch);
	}
	if (elf != NULL)
	{
		debug->print_str("Elf symbols: Num: %x, EntSize: %x\n", elf->num, elf->entsize);
	}
	if (apm_table != NULL)
	{
		debug->print_str("APM table - Version: %x\n", apm_table->version);
	}
	if (acpi != NULL)
	{
		debug->print_str("Boot ACPI\n");
	}
	if (physical != NULL)
	{
		debug->print_str("Load base address: %x\n", physical->load_base_addr);
	}
}

void BootInfo::print(TerminalPrinter* terminal)
{
	terminal->print_str("Boot info: \n");
	terminal->print_str("Size: %x\n", size);

	if (cmd_line != NULL)
	{
		terminal->print_str("Boot command line: %s\n", (const char *)&cmd_line->cmd);
	}
	if (loader_name != NULL)
	{
		terminal->print_str("Boot loader name: %s\n", (const char *)&loader_name->name);
	}
	if (basic_mem != NULL)
	{
		terminal->print_str("Basic memory info - Lower: %x, Upper: %x\n", basic_mem->mem_lower,
																		  basic_mem->mem_upper);
	}
	if (bios != NULL)
	{
		terminal->print_str("BIOS boot device: %x\n", bios->biosdev);
	}
	if (mmap != NULL)
	{
		terminal->print_str("Memory map - Entry size: %x, Entry version: %x\n", mmap->entry_size, mmap->entry_version);
		for (int i = 0; i<MEMORY_ENTRIES_LIMIT; i++)
		{
			auto entry = memory_entry[i];
			if (entry == NULL) break;
			terminal->print_str("\tMemory base: %x, len: %x, type: %x\n", entry->base_addr, entry->length, entry->type);
		}
	}
	if (vbe != NULL)
	{
		terminal->print_str("VBE info mode %x\n", vbe->vbe_mode);
	}
	if (framebuffer != NULL)
	{
		terminal->print_str("Framebuffer addr: %x, width: %x, height: %x, bpp: %x\n",
							framebuffer->framebuffer_addr,
							framebuffer->framebuffer_width,
							framebuffer->framebuffer_height,
							framebuffer->framebuffer_bpp);
	}
	if (elf != NULL)
	{
		terminal->print_str("Elf symbols: Num: %x, EntSize: %x\n", elf->num, elf->entsize);
	}
	if (apm_table != NULL)
	{
		terminal->print_str("APM table - Version: %x\n", apm_table->version);
	}
	if (acpi != NULL)
	{
		terminal->print_str("Boot ACPI\n");
	}
	if (physical != NULL)
	{
		terminal->print_str("Load base address: %x\n", physical->load_base_addr);
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
			case 1:
			{
				cmd_line = (boot_command_line *)ptr;
				break;
			}
			case 2:
			{
				loader_name = (boot_loader_name *)ptr;
				break;
			}
			case 4:
			{
				basic_mem = (boot_basic_memory *)ptr;
				break;
			}
			case 5:
			{
				bios = (boot_bios_device *)ptr;
				break;
			}
			case 6:
			{
				mmap = (boot_memory_map *)ptr;
				uintptr_t mem_current = (uintptr_t)&mmap->entries;
				const uintptr_t mem_end   = mem_current + mmap->size - 4*sizeof(uint32_t);
				int i = 0;
				while (mem_current < mem_end)
				{
					if (i >= MEMORY_ENTRIES_LIMIT) kernel_panic("Memory entries limit reached", 254);
					memory_map_entry* entry = (memory_map_entry*)mem_current;
					memory_entry[i] = entry;
					i++;
					mem_current += mmap->entry_size;
				}
				break;
			}
			case 7:
			{
				vbe = (boot_vbe_info *)ptr;
				break;
			}
			case 8:
			{
				framebuffer = (boot_framebuffer *)ptr;
				break;
			}
			case 9:
			{
				elf = (boot_elf_symbols *)ptr;
				break;
			}
			case 10:
			{
				apm_table = (boot_apm_table *)ptr;
				break;
			}
			case 14:
			{
				acpi = (boot_acpi *)ptr;
				break;
			}
			case 21:
			{
				physical = (boot_loader_physical_address *)ptr;
				break;
			}	
			default:
				break;
		}
		ptr += tag->size;
	}
}