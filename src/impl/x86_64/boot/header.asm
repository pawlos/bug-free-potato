section .multiboot_header
align 8
header_start:
    dd 0xe85250d6                                      ; multiboot
    dd 0                                               ; protected mode i386
    dd header_end - header_start                       ; size
    dd -(0xe85250d6 + 0 + (header_end - header_start)) ; checksum
align 8
info_request_start:
    dw 1
    dw 0
    dd info_request_end - info_request_start
    dd 1
    dd 4
info_request_end:
align 8
console_start:
    dw 4
    dw 0
    dd console_end - console_start
    dd 3
console_end:
align 8
framebuffer_start:
    dw 5
    dw 0
    dd framebuffer_end - framebuffer_start
    dd 1024
    dd 768
    dd 32
framebuffer_end:
align 8
    dw 0            ; end marker
    dw 0
    dd 8
header_end: