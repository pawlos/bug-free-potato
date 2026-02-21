bits 64
global _start
extern main
extern exit

section .text
_start:
    xor     rbp, rbp    ; clear frame pointer (marks bottom of call chain)
    xor     rdi, rdi    ; argc = 0  (no argv support yet)
    xor     rsi, rsi    ; argv = NULL
    call    main
    mov     rdi, rax    ; pass main()'s return value to exit()
    call    exit
.hang:                  ; exit() calls SYS_EXIT so we should never land here
    hlt
    jmp     .hang
