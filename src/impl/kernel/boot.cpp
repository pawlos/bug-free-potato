#include "boot.h"

int toEightByteDivisible(uintptr_t addr) {
    int v = (addr % 8);
    if (v == 0) return 0;
    return 8 - v;    
}

void BootInfo::parse(boot_info* boot_info)
{
	m_terminal->print_str("Boot info: \n");
	m_terminal->print_str("Size: %x\n", boot_info->size);
	uintptr_t start = (uintptr_t)boot_info;
	uintptr_t end = start + boot_info->size;
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
				boot_command_line* cmd_line = (boot_command_line *)ptr;
				m_terminal->print_str("Boot command line: %s\n", (const char *)&cmd_line->cmd);
				break;
			}
			case 2:
			{
				boot_loader_name* name_tag = (boot_loader_name *)ptr;
				m_terminal->print_str("Boot loader name: %s\n", (const char *)&name_tag->name);
				break;
			}
			case 4:
			{
				boot_basic_memory* mem = (boot_basic_memory *)ptr;
				m_terminal->print_str("Basic memory info - Lower: %x, Upper: %x\n", mem->mem_lower, mem->mem_upper);

				break;
			}
			case 5:
			{
				boot_bios_device* bios = (boot_bios_device *)ptr;
				m_terminal->print_str("BIOS boot device: %x\n", bios->biosdev);
				break;
			}
			case 6:
			{
				boot_memory_map* mmap = (boot_memory_map *)ptr;
				m_terminal->print_str("Memory map - Entry size: %x, Entry version: %x\n", mmap->entry_size, mmap->entry_version);
				break;
			}
			case 8:
			{
				boot_framebuffer* framebuffer = (boot_framebuffer *)ptr;
				m_terminal->print_str("Framebuffer addr: %x\n", framebuffer->framebuffer_addr);
				break;
			}
			case 9:
			{
				boot_elf_symbols* elf = (boot_elf_symbols *)ptr;
				m_terminal->print_str("Elf symbols: Num: %x, EntSize: %x\n", elf->num, elf->entsize);
				break;
			}
			case 10:
			{
				boot_apm_table* apm_table = (boot_apm_table *)ptr;
				m_terminal->print_str("APM table - Version: %x\n", apm_table->version);
				break;
			}
			case 14:
			{
				boot_acpi* acpi = (boot_acpi *)ptr;
				m_terminal->print_str("Boot ACPI\n");
				break;
			}
			case 21:
			{
				boot_loader_physical_address* addr_tag = (boot_loader_physical_address *)ptr;
				m_terminal->print_str("Load base address: %x\n", addr_tag->load_base_addr);
				break;
			}	
			default:
			{
				if (tag->type != 0)
				{
					m_terminal->print_str("Unsupported type = %x, size = %x\n", tag->type, tag->size);
				}
				break;
			}
		}
		ptr += tag->size;		
	}
}