; setjmp / longjmp for x86_64 SysV ABI
;
; jmp_buf layout (jmp_buf = unsigned long[8]):
;   [0] rbx   [1] rbp   [2] r12   [3] r13
;   [4] r14   [5] r15   [6] rsp   [7] rip
;
; Callee-saved registers per SysV AMD64: rbx, rbp, r12-r15.
; rsp and the return address (at [rsp] on entry) complete the picture.

bits 64
global setjmp
global longjmp

section .text

; int setjmp(jmp_buf env)   -- env is in rdi
setjmp:
    mov  [rdi +  0], rbx
    mov  [rdi +  8], rbp
    mov  [rdi + 16], r12
    mov  [rdi + 24], r13
    mov  [rdi + 32], r14
    mov  [rdi + 40], r15
    ; Save RSP as it will be after the call returns (caller's frame).
    lea  rax, [rsp + 8]
    mov  [rdi + 48], rax
    ; Save the return address (= where the caller will resume).
    mov  rax, [rsp]
    mov  [rdi + 56], rax
    ; Return 0 to indicate "direct" call.
    xor  eax, eax
    ret

; void longjmp(jmp_buf env, int val)   -- env=rdi, val=esi
longjmp:
    ; val = (val == 0) ? 1 : val  — longjmp must not return 0 to setjmp
    mov  eax, esi
    test eax, eax
    jnz  .nonzero
    mov  eax, 1
.nonzero:
    ; Restore callee-saved registers.
    mov  rbx, [rdi +  0]
    mov  rbp, [rdi +  8]
    mov  r12, [rdi + 16]
    mov  r13, [rdi + 24]
    mov  r14, [rdi + 32]
    mov  r15, [rdi + 40]
    ; Restore stack pointer.
    mov  rsp, [rdi + 48]
    ; Jump to the saved return address (replaces the "ret" of setjmp).
    jmp  qword [rdi + 56]
