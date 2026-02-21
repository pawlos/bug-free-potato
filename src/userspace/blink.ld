OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)
SECTIONS {
    . = 0xFFFF800018000000;  /* physical 0x18000000 = 384MB, safely within 512MB RAM */
    .text   : { *(.text)   }
    .rodata : { *(.rodata) }
    .data   : { *(.data)   }
    .bss    : { *(.bss)    }
}
