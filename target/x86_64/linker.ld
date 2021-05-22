ENTRY(start)

SECTIONS
{
	. = 1M;

	.boot :
	{
		KEEP(*(.multiboot_header))
	}

	.text :
	{
		*(.text)
	}

	.rodata : ALIGN(0x1000)
	{
		*(.rodata)
	}
}
