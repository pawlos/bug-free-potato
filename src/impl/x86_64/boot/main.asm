global start, gdt64
extern long_mode_start

section .text
bits 32
start:
	mov esp, stack_top
	;save GRUB multiboot info
	push 0
	push ebx
	call check_multiboot
	call check_cpuid
	call check_long_mode

	call setup_page_tables
	call enable_paging

	lgdt [gdt64.pointer]

	;we will read page tables in the kernel_main
	mov eax, page_table_l4
	mov esi, eax
	jmp gdt64.code_segment_kernel:long_mode_start

	hlt


check_multiboot:
	cmp eax, 0x36d76289
	jne .no_multiboot
	ret
.no_multiboot:
	mov al, "M"
	jmp error

check_cpuid:
	pushfd
	pop eax
	mov ecx, eax
	xor eax, 1 << 21
	push eax
	popfd
	pushfd
	pop eax
	push ecx
	popfd
	cmp eax, ecx
	je .no_cpuid
	ret

.no_cpuid:
	mov al, "C"
	jmp error

check_long_mode:
	mov eax, 0x80000000
	cpuid
	cmp eax, 0x80000001
	jb .no_long_mode

	mov eax, 0x80000001
	cpuid
	test edx, 1 << 29
	jz .no_long_mode
	ret

.no_long_mode:
	mov al, "L"
	jmp error


; we are setting up the page tables here
; PT04 - 1024 entries; 4B
; PT03 - 1024 entries; 4B
; PT02 - 1024 entries; 8B
setup_page_tables:
	mov eax, page_table_l3
	or eax, 0b11 ; present, writable
	mov [page_table_l4], eax

	mov esi, page_table_l2
	mov ebx, page_table_l3
	mov edi, 0
	mov ecx, 0
.loop_l3:
	push ecx
	push esi
	or esi, 0b11; present, writable
	mov [ebx], esi
	pop esi
	mov ecx, 0
.loop_l2:
	push ecx
	mov eax, 0x200000
	add ecx, edi
	mul ecx
	or eax, 0b10000011
	pop ecx
	mov [esi + ecx*8], eax
	inc ecx
	cmp ecx, 512
	jne .loop_l2
	add edi, 512
	add ebx, 8

	add esi, 512 * 8
	pop ecx
	inc ecx
	cmp ecx, 4
	jne .loop_l3
	ret

enable_paging:
	mov eax, page_table_l4
	mov cr3, eax

	mov eax, cr4
	or eax, 1 << 5
	mov cr4, eax

	mov ecx, 0xc0000080
	rdmsr
	or eax, 1 << 8      ;set the LM-bit
	wrmsr

	mov eax, cr0
	or eax, 1 << 31     ;set the PG-bit
	mov cr0, eax

	ret

error:
	mov dword [0xb8000], 0x4f524f45
	mov dword [0xb8004], 0x4f3a4f52
	mov dword [0xb8008], 0x4f204f20
	mov byte  [0xb800a], al
	hlt

section .bss
align 4096
page_table_l4:
	resb 1024 * 4
page_table_l3:
	resb 1024 * 4
page_table_l2:
	resb 1024 * 4 * 4

stack_bottom:
	resb 4096 * 4
stack_top:


section .rodata
gdt64:
.null_segment: equ $ - gdt64
	dw 0xffff
	dw 0
	db 0
	db 0
	db 1
	db 0
.code_segment_kernel: equ $ - gdt64
	dw 0
	dw 0
	db 0
	db 10011010b
	db 10101111b
	db 0
.data_segment_kernel: equ $ - gdt64
	dw 0
	dw 0
	db 0
	db 10010010b
	db 0
	db 0
.code_segment_user: equ $ - gdt64
	dw 0
	dw 0
	db 0
	db 11111010b
	db 11001111b
	db 0
.data_segment_user: equ $ - gdt64
	dw 0
	dw 0
	db 0
	db 11110010b
	db 11001111b
	db 0
.pointer:
	dw $ - gdt64 - 1
	dq gdt64