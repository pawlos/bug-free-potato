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

section .rodata
_msg_start:     db "[crt0] _start", 10
.len equ $ - _msg_start
_msg_pre_init:  db "[crt0] init_array begin", 10
.len equ $ - _msg_pre_init
_msg_post_init: db "[crt0] init_array done", 10
.len equ $ - _msg_post_init
_msg_pre_main:  db "[crt0] calling main", 10
.len equ $ - _msg_pre_main

section .text
; rdi=buf, rsi=len; clobbers rax
_serial_write:
    push    rax
    mov     eax, 36         ; SYS_WRITE_SERIAL
    int     0x80
    pop     rax
    ret

_start:
    xor     rbp, rbp        ; mark bottom of call chain

    ; Stash argc/argv in callee-saved registers BEFORE any function calls,
    ; so they survive the diagnostic _serial_write calls and init_array.
    mov     r14, [rsp]      ; r14 = argc
    lea     r15, [rsp+8]    ; r15 = argv

    ; envp = &argv[argc+1]  (skip argv pointers + NULL sentinel)
    lea     rax, [r15 + r14*8 + 8]
    mov     [rel environ], rax

    ; Set up FS base for TLS (stack canary at %fs:0x28 used by libstdc++).
    ; Write the self-pointer at _tls_block[0] first so that __errno_location()
    ; (%fs:0 → TC base, TC+12 → errno_val) works for the main thread.
    lea     rdi, [rel _tls_block]
    mov     [rdi], rdi      ; ThreadControl::tls_self = &_tls_block (self-pointer)
    mov     eax, 48         ; SYS_SET_FS_BASE
    int     0x80

    ; DIAG: announce we reached _start
    lea     rdi, [rel _msg_start]
    mov     rsi, _msg_start.len
    call    _serial_write

    ; DIAG
    lea     rdi, [rel _msg_pre_init]
    mov     rsi, _msg_pre_init.len
    call    _serial_write

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

    ; DIAG
    lea     rdi, [rel _msg_post_init]
    mov     rsi, _msg_post_init.len
    call    _serial_write
    lea     rdi, [rel _msg_pre_main]
    mov     rsi, _msg_pre_main.len
    call    _serial_write

    ; Restore argc/argv from callee-saved regs and call main(argc, argv).
    mov     rdi, r14        ; argc
    mov     rsi, r15        ; argv
    call    main
    mov     rdi, rax    ; pass main()'s return value to exit()
    call    exit
.hang:                  ; exit() calls SYS_EXIT so we should never land here
    hlt
    jmp     .hang
