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

	.idt BLOCK(0x1000) : ALIGN(0x1000)
	{
		_idt = .;
		.  = . + 0x1000;
	}

	.rodata : ALIGN(0x1000)
	{
		*(.rodata)
	}
}
