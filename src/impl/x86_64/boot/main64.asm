global long_mode_start
extern kernel_main

section .text
bits 64
long_mode_start:
	cli
	mov ax, 0x10
	mov ss, ax
	mov ds, ax
	mov es, ax
	mov fs, ax
	mov gs, ax

	pop rsi
	pop rdi
	call kernel_main

	hlt