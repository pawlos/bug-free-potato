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
    call syscall_handler
    POPALL
    iretq
GLOBAL _syscall_stub

[extern isr14_handler]
isr14:
    PUSHALL
    call isr14_handler
    POPALL
    iretq
GLOBAL isr14

[extern isr8_handler]
isr8:
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

LoadIDT:
    lidt [idtDescriptor]
    sti
    ret
    GLOBAL LoadIDT