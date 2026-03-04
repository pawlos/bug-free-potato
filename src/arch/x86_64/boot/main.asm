global start, gdt64
extern long_mode_start

section .boot_text
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

    lgdt [gdt64_pointer]

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
setup_page_tables:
    mov ebx, page_table_l2_0    ; ebx = dest base for PD 0
    mov esi, 0                  ; pdpt_chunk = 0..3
.fill_pdpt_chunks:
    ; for each chunk (0..3), fill 512 entries
    mov ecx, 0
.fill_pd_loop_chunk:
    ; physical address = ((esi<<9) + ecx) << 21
    ; czyli phys = ((esi * 512) + ecx) * 2MiB
    mov eax, esi
    shl eax, 9                  ; eax = esi * 512
    add eax, ecx                ; eax = block_index = esi*512 + ecx
    shl eax, 21                 ; eax = phys = block_index * 2MiB
    or  eax, 0x87               ; present | rw | PS (2MiB) | user
    ; write 64-bit entry: low dword = eax, high dword = 0
    mov [ebx + ecx*8 + 0], eax
    mov dword [ebx + ecx*8 + 4], 0

    inc ecx
    cmp ecx, 512
    jne .fill_pd_loop_chunk

    ; set PDPT entry for this chunk: page_table_l3[esi] = EBX | 0x07
    mov eax, ebx
    or  eax, 0x07
    mov [page_table_l3 + esi*8 + 0], eax
    mov dword [page_table_l3 + esi*8 + 4], 0

    ; advance EBX to next PD (each PD = 4096)
    add ebx, 4096
    inc esi
    cmp esi, 4
    jne .fill_pdpt_chunks

; finally set PML4[0] -> page_table_l3 (PDPT)
    mov eax, page_table_l3
    or  eax, 0x07
    mov [page_table_l4 + 0], eax
    mov dword [page_table_l4 + 4], 0

; PML4[256] -> same PDPT, maps 0xFFFF800000000000 (high half)
; eax still holds: page_table_l3 | 0x03
    mov [page_table_l4 + 256*8 + 0], eax
    mov dword [page_table_l4 + 256*8 + 4], 0
    ret

enable_paging:
    mov eax, page_table_l4
    mov cr3, eax

    mov eax, cr4
    or eax, 1 << 5 ; enable PAE - Physical Address Extension
    mov cr4, eax

    mov ecx, 0xc0000080
    rdmsr
    or eax, 1 << 8      ;set the LM-bit
    wrmsr

    mov eax, cr0
    or eax, 1 << 31 | 1    ;set the PG-bit | PE
    mov cr0, eax

    ret

error:
    mov dword [0xb8000], 0x4f524f45
    mov dword [0xb8004], 0x4f3a4f52
    mov dword [0xb8008], 0x4f204f20
    mov byte  [0xb800a], al
    hlt

section .boot_bss
align 4096
page_table_l4:  resb 4096
align 4096
page_table_l3:  resb 4096
align 4096
page_table_l2_0: resb 4096
align 4096
page_table_l2_1: resb 4096
align 4096
page_table_l2_2: resb 4096
align 4096
page_table_l2_3: resb 4096

stack_bottom:
    resb 4096 * 4
stack_top:


section .boot_rodata
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
    db 00100000b
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
    db 00100000b   ; L=1 (64-bit long mode)
    db 0
.data_segment_user: equ $ - gdt64
    dw 0
    dw 0
    db 0
    db 11110010b
    db 11001111b
    db 0
; TSS descriptor (16 bytes) — filled at runtime by tss_init() in C++.
; Selector = 0x28 (offset 40 = 5 * 8-byte entries above).
; 16-byte TSS descriptor placeholder at GDT offset 0x28.
; tss_init() in C++ locates this via sgdt and fills it at runtime.
    dq 0    ; TSS descriptor low qword
    dq 0    ; TSS descriptor high qword
gdt64_pointer:
    dw $ - gdt64 - 1
    dq gdt64