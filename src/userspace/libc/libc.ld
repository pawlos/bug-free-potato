OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)
SECTIONS {
    . = 0x400000;
    .text   : { *(.text .text.*)   }
    .rodata : { *(.rodata .rodata.*)  }
    .init_array : {
        PROVIDE_HIDDEN(__init_array_start = .);
        *(.init_array .init_array.* .ctors)
        PROVIDE_HIDDEN(__init_array_end = .);
    }
    .data   : { *(.data .data.*)   }
    .got    : { *(.got .got.plt)   }
    .bss    : { *(.bss .bss.*)     }
}
