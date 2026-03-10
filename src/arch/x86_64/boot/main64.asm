global long_mode_start
extern kernel_main, _syscall_stub
extern _bss_start, _end

section .boot_text
bits 64
long_mode_start:
    ; do we need to remap page tables here for 64-bit?
	cli
	call enable_sse
	call enable_syscalls
	mov ax, 0x10
	mov ss, ax
	mov ds, ax
	mov es, ax
	mov fs, ax
	mov gs, ax

	; Zero the .bss section (NOLOAD — not zeroed by GRUB)
	mov rdi, _bss_start
	mov rcx, _end
	sub rcx, rdi
	shr rcx, 3            ; count in qwords
	xor rax, rax
	rep stosq

	pop rdi
	; rdi - pointer to boot_info; rsi - pointer to l4_page_table
	mov rax, kernel_main   ; MOVABS RAX, imm64 — full 64-bit absolute address
	call rax

	hlt


enable_sse:
    ; Clear CR0.EM (bit 2) — no FPU emulation
    ; Set   CR0.MP (bit 1) — monitor coprocessor
    mov rax, cr0
    and rax, ~(1 << 2)
    or  rax, (1 << 1)
    mov cr0, rax

    ; Set CR4.OSFXSR (bit 9)  — OS supports FXSAVE/FXRSTOR
    ; Set CR4.OSXMMEXCPT (bit 10) — OS handles SSE exceptions
    mov rax, cr4
    or  rax, (3 << 9)
    mov cr4, rax

    ; Initialise x87 FPU to a clean state
    fninit
    ret

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