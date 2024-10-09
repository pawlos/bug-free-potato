#include "boot.h"
#include "kernel.h"

int toEightByteDivisible(const pt::uintptr_t addr) {
    int v = (addr % 8);
    if (v == 0) return 0;
    return 8 - v;    
}

boot_framebuffer* BootInfo::get_framebuffer() const {
	if (framebuffer == nullptr) kernel_panic("BootInfo not parsed!", BootInfoNotParsed);

	return framebuffer;
}

memory_map_entry** BootInfo::get_memory_maps()
{
	if (memory_entry == nullptr) kernel_panic("BootInfo not parsed!", BootInfoNotParsed);

	return memory_entry;
}

void BootInfo::log()
{
	klog("[BOOT] Boot info: Size: %x\n", size);

	if (cmd_line != nullptr)
	{
		klog("[BOOT] Boot command line: %s\n", reinterpret_cast<const char *>(&cmd_line->cmd));
	}
	if (loader_name != nullptr)
	{
		klog("[BOOT] Boot loader name: %s\n", reinterpret_cast<const char *>(&loader_name->name));
	}
	if (basic_mem != nullptr)
	{
		klog("[BOOT] Basic memory info - Lower: %x, Upper: %x\n", basic_mem->mem_lower,
																basic_mem->mem_upper);
	}
	if (bios != nullptr)
	{
		klog("[BOOT] BIOS boot device: %x\n", bios->biosdev);
	}
	if (mmap != nullptr)
	{
		klog("[BOOT] Memory map - Entry size: %x, Entry version: %x\n", mmap->entry_size, mmap->entry_version);
		for (auto entry : memory_entry)
		{
			if (entry == nullptr) break;
			klog("\t[BT] Memory base: %x, len: %x, type: %x\n", entry->base_addr, entry->length, entry->type);
		}
	}
	if (vbe != nullptr)
	{
		klog("[BOOT] VBE info mode %x\n", vbe->vbe_mode);
	}
	if (framebuffer != nullptr)
	{
		klog("[BOOT] Framebuffer addr: %x, width: %x, height: %x, bpp: %x, type: %x, pitch: %x\n",
							framebuffer->framebuffer_addr,
							framebuffer->framebuffer_width,
							framebuffer->framebuffer_height,
							framebuffer->framebuffer_bpp,
							framebuffer->framebuffer_type,
							framebuffer->framebuffer_pitch);
	}
	if (elf != nullptr)
	{
		klog("[BOOT] Elf symbols: Num: %x, EntSize: %x\n", elf->num, elf->entsize);
	}
	if (apm_table != nullptr)
	{
		klog("[BOOT] APM table - Version: %x\n", apm_table->version);
	}
	if (acpi != nullptr)
	{
		klog("[BOOT] Boot ACPI\n");
	}
	if (physical != nullptr)
	{
		klog("[BOOT] Load base address: %x\n", physical->load_base_addr);
	}
}

void BootInfo::parse(boot_info* boot_info)
{
	const auto start = reinterpret_cast<pt::uintptr_t>(boot_info);
	this->size = boot_info->size;
	const pt::uintptr_t end = start + size;
	pt::uintptr_t ptr = start + 8;

	while (ptr < end)
	{
		const int padding_size = toEightByteDivisible(ptr);
		if (padding_size != 0)
		{
			ptr += padding_size;
		}
		const auto* tag = reinterpret_cast<basic_tag *>(ptr);
		switch(tag->type)
		{
			case BOOT_CMDLINE:
			{
				cmd_line = reinterpret_cast<boot_command_line *>(ptr);
				break;
			}
			case BOOT_LOADER_NAME:
			{
				loader_name = reinterpret_cast<boot_loader_name *>(ptr);
				break;
			}
			case BOOT_BASIC_MEM:
			{
				basic_mem = reinterpret_cast<boot_basic_memory *>(ptr);
				break;
			}
			case BOOT_BIOS:
			{
				bios = reinterpret_cast<boot_bios_device *>(ptr);
				break;
			}
			case BOOT_MMAP:
			{
				mmap = reinterpret_cast<boot_memory_map *>(ptr);
				auto mem_current = reinterpret_cast<pt::uintptr_t>(&mmap->entries);
				const pt::uintptr_t mem_end   = mem_current + mmap->size - 4*sizeof(pt::uint32_t);
				for (auto & map_entry : memory_entry)
				{
					map_entry = nullptr;
				}
				int i = 0;
				while (mem_current < mem_end)
				{
					if (i >= MEMORY_ENTRIES_LIMIT) kernel_panic("Memory entries limit reached", MemEntriesLimitReached);
					auto* entry = reinterpret_cast<memory_map_entry *>(mem_current);
					memory_entry[i] = entry;
					i++;
					mem_current += mmap->entry_size;
				}
				break;
			}
			case BOOT_VBE_INFO:
			{
				vbe = reinterpret_cast<boot_vbe_info *>(ptr);
				break;
			}
			case BOOT_FRAMEBUFFER:
			{
				framebuffer = reinterpret_cast<boot_framebuffer *>(ptr);
				break;
			}
			case BOOT_ELF_SYMBOLS:
			{
				elf = reinterpret_cast<boot_elf_symbols *>(ptr);
				break;
			}
			case BOOT_APM_TABLE:
			{
				apm_table = reinterpret_cast<boot_apm_table *>(ptr);
				break;
			}
			case BOOT_ACPI:
			{
				acpi = reinterpret_cast<boot_acpi *>(ptr);
				break;
			}
			case BOOT_PHYSICAL:
			{
				physical = reinterpret_cast<boot_loader_physical_address *>(ptr);
				break;
			}	
			default:
				klog("[BOOT] No parser for %d\n", tag->type);
				break;
		}
		ptr += tag->size;
	}
	this->log();
}