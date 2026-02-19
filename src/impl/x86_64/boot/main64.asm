global long_mode_start
extern kernel_main, _syscall_stub

section .boot_text
bits 64
long_mode_start:
    ; do we need to remap page tables here for 64-bit?
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
	mov rax, kernel_main   ; MOVABS RAX, imm64 — full 64-bit absolute address
	call rax

	hlt


enable_syscalls:
	mov rcx, 0xc0000080
	rdmsr
	or rax, 1 << 0
	wrmsr

	mov rcx, 0xc0000082
	mov rax, _syscall_stub     ; MOVABS: full 64-bit absolute address
	mov rdx, rax
	shr rdx, 32                ; EDX:EAX = full 64-bit address for WRMSR
	wrmsr

	ret