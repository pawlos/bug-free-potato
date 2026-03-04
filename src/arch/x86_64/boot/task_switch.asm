section .text
bits 64

global task_switch

; void task_switch(TaskContext* old_context, void* new_context)
; rdi = old_context pointer
; rsi = new_context pointer
task_switch:
    ; Save current context to old_context (rdi)
    mov [rdi + 0],   rax      ; rax offset 0
    mov [rdi + 8],   rbx      ; rbx offset 8
    mov [rdi + 16],  rcx      ; rcx offset 16
    mov [rdi + 24],  rdx      ; rdx offset 24
    mov [rdi + 32],  rsi      ; rsi offset 32 (note: this is the original rsi value)
    mov [rdi + 40],  rdi      ; rdi offset 40 (note: this is the original rdi value)
    mov [rdi + 48],  rbp      ; rbp offset 48
    mov [rdi + 56],  r8       ; r8 offset 56
    mov [rdi + 64],  r9       ; r9 offset 64
    mov [rdi + 72],  r10      ; r10 offset 72
    mov [rdi + 80],  r11      ; r11 offset 80
    mov [rdi + 88],  r12      ; r12 offset 88
    mov [rdi + 96],  r13      ; r13 offset 96
    mov [rdi + 104], r14      ; r14 offset 104
    mov [rdi + 112], r15      ; r15 offset 112

    ; Save RSP as it was before 'call task_switch' pushed the return address
    mov rax, rsp
    add rax, 8
    mov [rdi + 120], rax      ; rsp offset 120

    ; Save current RIP (return address from call, sitting at [rsp])
    mov rax, [rsp]
    mov [rdi + 128], rax      ; rip offset 128

    ; Save RFLAGS
    pushfq
    pop rax
    mov [rdi + 136], rax      ; rflags offset 136

    ; Load new context from new_context (rsi)
    mov rax, [rsi + 0]        ; rax
    mov rbx, [rsi + 8]        ; rbx
    mov rcx, [rsi + 16]       ; rcx
    mov rdx, [rsi + 24]       ; rdx
    mov r8,  [rsi + 56]       ; r8
    mov r9,  [rsi + 64]       ; r9
    mov r10, [rsi + 72]       ; r10
    mov r11, [rsi + 80]       ; r11
    mov r12, [rsi + 88]       ; r12
    mov r13, [rsi + 96]       ; r13
    mov r14, [rsi + 104]      ; r14
    mov r15, [rsi + 112]      ; r15
    mov rbp, [rsi + 48]       ; rbp

    ; Load new RSP and RIP
    mov rsp, [rsi + 120]      ; rsp
    mov r8,  [rsi + 128]      ; rip to r8 (temporary)

    ; Load new RFLAGS
    mov r9,  [rsi + 136]      ; rflags to r9 (temporary)
    push r9
    popfq                      ; restore rflags

    ; Load remaining registers (rdi, rsi, rdx)
    mov rdi, [rsi + 40]       ; rdi
    mov rsi, [rsi + 32]       ; rsi

    ; Jump to new task's RIP
    jmp r8
