global long_mode_start
extern kernel_main, _syscall_stub

section .text
bits 64
long_mode_start:
	cli
	call enable_syscalls
	mov ax, 0x10
	mov ss, ax
	mov ds, ax
	mov es, ax
	mov fs, ax
	mov gs, ax

	pop rdi
	; rdi - pointer to boot_info; rsi - pointer to l4_page_table
	call kernel_main

	hlt


enable_syscalls:
	mov rcx, 0xc0000080
	rdmsr
	or rax, 1 << 0
	wrmsr

	mov rcx, 0xc0000082
	rdmsr
	mov rax, dword _syscall_stub
	wrmsr

	ret