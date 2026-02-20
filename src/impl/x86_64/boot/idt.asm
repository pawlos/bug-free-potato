[extern _idt]
idtDescriptor:
    dw 4095
    dq _idt

%macro PUSHALL 0
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
%endmacro

%macro POPALL 0
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rdx
    pop rcx
    pop rbx
    pop rax
%endmacro

bits 64
[extern irq12_handler]
irq12:
    PUSHALL
    call irq12_handler
    POPALL
    iretq
GLOBAL irq12

[extern irq14_handler]
irq14:
    PUSHALL
    call irq14_handler
    POPALL
    iretq
GLOBAL irq14

[extern irq15_handler]
irq15:
    PUSHALL
    call irq15_handler
    POPALL
    iretq
GLOBAL irq15

[extern irq1_handler]
irq1:
    PUSHALL
    call irq1_handler
    POPALL
    iretq
GLOBAL irq1

[extern irq0_handler]
irq0:
    PUSHALL
    call irq0_handler
    POPALL
    iretq
GLOBAL irq0

[extern syscall_handler]
_syscall_stub:
    PUSHALL
    ; registers still hold caller's values after PUSHALL
    mov rdx, rsi    ; arg2: original rsi  (must come first — saves rsi before overwrite)
    mov rsi, rdi    ; arg1: original rdi
    mov rdi, rax    ; nr:   original rax (syscall number)
    call syscall_handler
    POPALL
    iretq
GLOBAL _syscall_stub

[extern isr14_handler]
isr14:
    pop rax
    PUSHALL
    call isr14_handler
    POPALL
    iretq
GLOBAL isr14

[extern isr13_handler]
isr13:
    pop rax
    PUSHALL
    call isr13_handler
    POPALL
    iretq
GLOBAL isr13

[extern isr8_handler]
isr8:
    pop rax
    PUSHALL
    call isr8_handler
    POPALL
    iretq
GLOBAL isr8

[extern isr6_handler]
isr6:
    PUSHALL
    call isr6_handler
    POPALL
    iretq
GLOBAL isr6

[extern isr5_handler]
isr5:
    PUSHALL
    call isr5_handler
    POPALL
    iretq
GLOBAL isr5

[extern isr4_handler]
isr4:
    PUSHALL
    call isr4_handler
    POPALL
    iretq
GLOBAL isr4

[extern isr3_handler]
isr3:
    PUSHALL
    call isr3_handler
    POPALL
    iretq
GLOBAL isr3

[extern isr2_handler]
isr2:
    PUSHALL
    call isr2_handler
    POPALL
    iretq
GLOBAL isr2

[extern isr1_handler]
isr1:
    PUSHALL
    call isr1_handler
    POPALL
    iretq
GLOBAL isr1

[extern isr0_handler]
isr0:
    PUSHALL
    call isr0_handler
    POPALL
    iretq
GLOBAL isr0

[extern isr7_handler]
isr7:
    PUSHALL
    call isr7_handler
    POPALL
    iretq
GLOBAL isr7

[extern isr9_handler]
isr9:
    PUSHALL
    call isr9_handler
    POPALL
    iretq
GLOBAL isr9

[extern isr10_handler]
isr10:
    PUSHALL
    call isr10_handler
    POPALL
    iretq
GLOBAL isr10

[extern isr11_handler]
isr11:
    PUSHALL
    call isr11_handler
    POPALL
    iretq
GLOBAL isr11

[extern isr12_handler]
isr12:
    PUSHALL
    call isr12_handler
    POPALL
    iretq
GLOBAL isr12

[extern isr15_handler]
isr15:
    PUSHALL
    call isr15_handler
    POPALL
    iretq
GLOBAL isr15

[extern isr16_handler]
isr16:
    PUSHALL
    call isr16_handler
    POPALL
    iretq
GLOBAL isr16

[extern isr17_handler]
isr17:
    PUSHALL
    call isr17_handler
    POPALL
    iretq
GLOBAL isr17

[extern isr18_handler]
isr18:
    PUSHALL
    call isr18_handler
    POPALL
    iretq
GLOBAL isr18

[extern isr19_handler]
isr19:
    PUSHALL
    call isr19_handler
    POPALL
    iretq
GLOBAL isr19

[extern isr20_handler]
isr20:
    PUSHALL
    call isr20_handler
    POPALL
    iretq
GLOBAL isr20

[extern isr21_handler]
isr21:
    PUSHALL
    call isr21_handler
    POPALL
    iretq
GLOBAL isr21

[extern isr22_handler]
isr22:
    PUSHALL
    call isr22_handler
    POPALL
    iretq
GLOBAL isr22

[extern isr23_handler]
isr23:
    PUSHALL
    call isr23_handler
    POPALL
    iretq
GLOBAL isr23

[extern isr24_handler]
isr24:
    PUSHALL
    call isr24_handler
    POPALL
    iretq
GLOBAL isr24

[extern isr25_handler]
isr25:
    PUSHALL
    call isr25_handler
    POPALL
    iretq
GLOBAL isr25

[extern isr26_handler]
isr26:
    PUSHALL
    call isr26_handler
    POPALL
    iretq
GLOBAL isr26

[extern isr27_handler]
isr27:
    PUSHALL
    call isr27_handler
    POPALL
    iretq
GLOBAL isr27

[extern isr28_handler]
isr28:
    PUSHALL
    call isr28_handler
    POPALL
    iretq
GLOBAL isr28

[extern isr29_handler]
isr29:
    PUSHALL
    call isr29_handler
    POPALL
    iretq
GLOBAL isr29

[extern isr30_handler]
isr30:
    PUSHALL
    call isr30_handler
    POPALL
    iretq
GLOBAL isr30

[extern isr31_handler]
isr31:
    PUSHALL
    call isr31_handler
    POPALL
    iretq
GLOBAL isr31

LoadIDT:
    lidt [rel idtDescriptor]
    sti
    ret
    GLOBAL LoadIDT