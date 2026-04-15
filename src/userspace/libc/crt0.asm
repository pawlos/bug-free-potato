bits 64
global _start
extern main
extern exit
extern environ
extern __init_array_start
extern __init_array_end

section .bss
alignb 64
_tls_block: resb 256      ; minimal TLS area (stack canary at offset 0x28)

section .text
_start:
    xor     rbp, rbp        ; mark bottom of call chain

    ; Set up FS base for TLS (stack canary at %fs:0x28 used by libstdc++).
    ; Write the self-pointer at _tls_block[0] first so that __errno_location()
    ; (%fs:0 → TC base, TC+12 → errno_val) works for the main thread.
    lea     rdi, [rel _tls_block]
    mov     [rdi], rdi      ; ThreadControl::tls_self = &_tls_block (self-pointer)
    mov     eax, 48         ; SYS_SET_FS_BASE
    int     0x80

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
