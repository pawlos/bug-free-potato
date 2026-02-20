bits 64
section .text
global _start

SYS_WRITE equ 0
SYS_EXIT  equ 1

_start:
    mov rax, SYS_WRITE
    mov rdi, msg
    int 0x80

    mov rax, SYS_EXIT
    mov rdi, 0
    int 0x80

section .rodata
msg: db "Hello from ELF via syscall!", 10, 0
