[extern _idt]
idtDescriptor:
	dw 4095
	dq _idt

%macro PUSHALL 0
	push rax
	push rcx
	push rdx
	push r8
	push r9
	push r10
	push r11
%endmacro

%macro POPALL 0
	pop r11
	pop r10
	pop r9
	pop r8
	pop rdx
	pop rcx
	pop rax
%endmacro

bits 64
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