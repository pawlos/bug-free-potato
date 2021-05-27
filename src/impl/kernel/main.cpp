#include "print.h"
#include "com.h"
#include "boot.h"
#include "idt.h"

extern const char Logo[];

int toEightByteDivisible(uintptr_t addr) {
    int v = (addr % 8);
    if (v == 0) return 0;
    return 8 - v;    
}

void print_boot_info(TerminalPrinter terminal, boot_info* boot_info)
{
	terminal.print_str("Boot info: \n");
	terminal.print_str("Size: 0x");
	terminal.print_hex(boot_info->size);
	terminal.print_char('\n');
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
				terminal.print_str("Boot command line: ");
				terminal.print_str((const char *)&cmd_line->cmd);
				terminal.print_char('\n');
				break;
			}
			case 2:
			{
				boot_loader_name* name_tag = (boot_loader_name *)ptr;
				terminal.print_str("Boot loader name: ");
				terminal.print_str((const char *)&name_tag->name);
				terminal.print_char('\n');
				break;
			}
			case 4:
			{
				boot_basic_memory* mem = (boot_basic_memory *)ptr;
				terminal.print_str("Basic memory info - Lower: 0x");
				terminal.print_hex(mem->mem_lower);
				terminal.print_str(", Upper: 0x");
				terminal.print_hex(mem->mem_upper);
				terminal.print_char('\n');
				break;
			}
			case 5:
			{
				boot_bios_device* bios = (boot_bios_device *)ptr;
				terminal.print_str("BIOS boot device: 0x");
				terminal.print_hex(bios->biosdev);
				terminal.print_char('\n');
				break;
			}
			case 6:
			{
				boot_memory_map* mmap = (boot_memory_map *)ptr;
				terminal.print_str("Memory map: ");
				terminal.print_str("Entry size: 0x");
				terminal.print_hex(mmap->entry_size);
				terminal.print_str(", Entry version: 0x");
				terminal.print_hex(mmap->entry_version);
				terminal.print_char('\n');
				break;
			}
			case 8:
			{
				boot_framebuffer* framebuffer = (boot_framebuffer *)ptr;
				terminal.print_str("Framebuffer addr: 0x");
				terminal.print_hex(framebuffer->framebuffer_addr);
				terminal.print_char('\n');
				break;
			}
			case 9:
			{
				boot_elf_symbols* elf = (boot_elf_symbols *)ptr;
				terminal.print_str("Elf symbols: Num: 0x");
				terminal.print_hex(elf->num);
				terminal.print_str(", EntSize: 0x");
				terminal.print_hex(elf->entsize);
				terminal.print_char('\n');
				break;
			}
			case 10:
			{
				boot_apm_table* apm_table = (boot_apm_table *)ptr;
				terminal.print_str("APM table: ");
				terminal.print_str("Version: 0x");
				terminal.print_hex(apm_table->version);
				terminal.print_char('\n');
				break;
			}
			case 14:
			{
				boot_acpi* acpi = (boot_acpi *)ptr;
				terminal.print_str("Boot ACPI");
				terminal.print_char('\n');
				break;
			}
			case 21:
			{
				boot_loader_physical_address* addr_tag = (boot_loader_physical_address *)ptr;
				terminal.print_str("Load base address: 0x");
				terminal.print_hex(addr_tag->load_base_addr);
				terminal.print_char('\n');
				break;
			}	
			default:
			{
				if (tag->type != 0)
				{
					terminal.print_str("Unsupported type: 0x");
					terminal.print_hex(tag->type);
					terminal.print_str(", size=0x");
					terminal.print_hex(tag->size);
					terminal.print_char('\n');
				}
				break;
			}
		}
		ptr += tag->size;		
	}
}

extern "C" void kernel_main(boot_info* boot_info) {
	TerminalPrinter terminal;
	terminal.print_clear();
	
	terminal.print_set_color(PRINT_COLOR_YELLOW, PRINT_COLOR_BLACK);
	terminal.print_str(Logo);
	terminal.print_str("\n\n");
	terminal.print_str("Welcome to 64-bit potat OS\n");
	InitializeIDT();
	terminal.print_str("IDT inialiazed...\n");
	print_boot_info(terminal, boot_info);
	for(;;) {
		asm("hlt");
	}
}