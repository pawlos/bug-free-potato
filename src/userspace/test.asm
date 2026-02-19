; Ring-0 ELF test program: writes "Hello from ELF!\n" to COM1 (port 0x3F8)
; Returns to the kernel trampoline which calls TaskScheduler::task_exit().
bits 64
section .text
global _start

_start:
    mov rsi, msg
    mov rcx, msg_len
.loop:
    mov dx, 0x3F8
    lodsb
    out dx, al
    loop .loop
    ret          ; returns to run_loaded_elf() trampoline -> task_exit()

section .rodata
msg:     db "Hello from ELF!", 10
msg_len  equ $ - msg
