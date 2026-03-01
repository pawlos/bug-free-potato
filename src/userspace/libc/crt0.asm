bits 64
global _start
extern main
extern exit

section .text
_start:
    xor     rbp, rbp        ; mark bottom of call chain
    mov     rdi, [rsp]      ; argc (SysV initial stack)
    lea     rsi, [rsp+8]    ; argv = &stack[1]
    call    main
    mov     rdi, rax    ; pass main()'s return value to exit()
    call    exit
.hang:                  ; exit() calls SYS_EXIT so we should never land here
    hlt
    jmp     .hang
