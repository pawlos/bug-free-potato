OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)
SECTIONS {
    . = 0xFFFF800010000000;
    .text   : { *(.text)   }
    .rodata : { *(.rodata) }
    .data   : { *(.data)   }
    .bss    : { *(.bss)    }
}
