bits 64
global _start
extern main
extern exit
extern environ
extern __init_array_start
extern __init_array_end

section .text
_start:
    xor     rbp, rbp        ; mark bottom of call chain
    mov     rdi, [rsp]      ; argc (SysV initial stack)
    lea     rsi, [rsp+8]    ; argv = &stack[1]

    ; envp = &argv[argc+1]  (skip argv pointers + NULL sentinel)
    mov     rcx, rdi        ; rcx = argc
    lea     rax, [rsi + rcx*8 + 8]  ; rax = &argv[argc+1]
    mov     [rel environ], rax

    ; Call C++ global constructors from .init_array
    lea     r12, [rel __init_array_start]
    lea     r13, [rel __init_array_end]
.init_loop:
    cmp     r12, r13
    jge     .init_done
    mov     rax, [r12]
    test    rax, rax
    jz      .init_skip
    call    rax
.init_skip:
    add     r12, 8
    jmp     .init_loop
.init_done:

    call    main
    mov     rdi, rax    ; pass main()'s return value to exit()
    call    exit
.hang:                  ; exit() calls SYS_EXIT so we should never land here
    hlt
    jmp     .hang
