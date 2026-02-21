bits 64
default rel
section .text
global _start

SYS_EXIT      equ 1
SYS_YIELD     equ 8
SYS_GET_TICKS equ 9
SYS_GET_TIME  equ 10
SYS_FILL_RECT equ 11
SYS_DRAW_TEXT equ 12
SYS_FB_WIDTH  equ 13

DOT_W   equ 12
DOT_H   equ 12
CLOCK_W equ 40   ; 5 chars * 8px glyph width
GAP     equ 4

_start:
    ; Query framebuffer width to compute positions
    mov rax, SYS_FB_WIDTH
    int 0x80
    ; dot_x = fb_width - DOT_W
    sub rax, DOT_W
    mov [dot_x], rax
    ; clock_x = dot_x - GAP - CLOCK_W
    sub rax, GAP + CLOCK_W
    mov [clock_x], rax

.loop:
    ; --- blink dot ---
    mov rax, SYS_GET_TICKS
    int 0x80
    xor rdx, rdx
    mov rcx, 25
    div rcx           ; rax = ticks / 25
    and rax, 1        ; should_be_on = (ticks/25) & 1

    movzx rbx, byte [dot_on]
    cmp rax, rbx
    je .no_dot_change
    mov [dot_on], al

    test al, al
    mov r8, 0x00FF00  ; green
    jnz .do_fill
    mov r8, 0x000000  ; black
.do_fill:
    mov rax, SYS_FILL_RECT
    mov rdi, [dot_x]
    mov rsi, 0
    mov rdx, DOT_W
    mov rcx, DOT_H
    int 0x80

.no_dot_change:
    ; --- update clock ---
    mov rax, SYS_GET_TIME
    int 0x80              ; rax = (hours << 8) | minutes

    mov rbx, rax
    and rbx, 0xFF         ; minutes
    shr rax, 8            ; hours

    movzx rcx, byte [last_minute]
    cmp rbx, rcx
    je .no_time_change
    mov [last_minute], bl

    ; format hours into time_buf[0..1]
    xor rdx, rdx
    mov rcx, 10
    div rcx               ; rax = hours/10, rdx = hours%10
    add al, '0'
    mov [time_buf], al
    add dl, '0'
    mov [time_buf+1], dl
    mov byte [time_buf+2], ':'

    ; format minutes into time_buf[3..4]
    mov rax, rbx
    xor rdx, rdx
    mov rcx, 10
    div rcx               ; rax = minutes/10, rdx = minutes%10
    add al, '0'
    mov [time_buf+3], al
    add dl, '0'
    mov [time_buf+4], dl
    mov byte [time_buf+5], 0

    mov rax, SYS_DRAW_TEXT
    mov rdi, [clock_x]
    mov rsi, 0
    mov rdx, time_buf
    mov rcx, 0x00FF00     ; green fg
    mov r8,  0x000000     ; black bg
    int 0x80

.no_time_change:
    mov rax, SYS_YIELD
    int 0x80
    jmp .loop

section .data
time_buf:    db "00:00", 0

section .bss
dot_x:       resq 1
clock_x:     resq 1
dot_on:      resb 1
last_minute: resb 1
