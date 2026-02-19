KERNEL_OFFSET = 0xFFFF800000000000;

ENTRY(start)

SECTIONS
{
    . = 1M;

    /* Boot section: VMA = LMA = 1MB. Contains the multiboot header and ALL boot
       assembly (main.asm + main64.asm). GRUB jumps to 'start' here. */
    .boot :
    {
        KEEP(*(.multiboot_header))
        *(.boot_text)
        *(.boot_rodata)
    }

    .boot_bss (NOLOAD) :
    {
        *(.boot_bss)
    }

    /* Jump the VMA counter into the high half.
       LMA stays at current physical position (≈1MB + small .boot overhead). */
    . += KERNEL_OFFSET;

    /* All C++ sections: high VMA, LMA = VMA - KERNEL_OFFSET */
    .text : AT(ADDR(.text) - KERNEL_OFFSET)
    {
        *(.text)
    }

    .idt ALIGN(0x1000) : AT(ADDR(.idt) - KERNEL_OFFSET)
    {
        _idt = .;
        . = . + 0x1000;
    }

    .rodata ALIGN(0x1000) : AT(ADDR(.rodata) - KERNEL_OFFSET)
    {
        *(.rodata)
    }

    .data ALIGN(0x1000) : AT(ADDR(.data) - KERNEL_OFFSET)
    {
        *(.data)
    }

    .bss ALIGN(0x1000) (NOLOAD) :
    {
        *(.bss)
        *(COMMON)
    }
}
