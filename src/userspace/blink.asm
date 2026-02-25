bits 64
default rel
section .text
global _start

SYS_EXIT          equ 1
SYS_YIELD         equ 8
SYS_GET_TICKS     equ 9
SYS_GET_TIME      equ 10
SYS_FILL_RECT     equ 11
SYS_DRAW_TEXT     equ 12
SYS_FB_WIDTH      equ 13
SYS_FB_HEIGHT     equ 19
SYS_CREATE_WINDOW equ 24

; Chromeless window flag (must match WF_CHROMELESS in window.h)
WF_CHROMELESS equ 1

; Layout within the window's client area (all coords are window-relative)
;   [  clock (40px)  ][ gap (4px) ][ dot (12px) ]
;   total width = 56px, height = 16px (one glyph row)
WIN_W     equ 56   ; total window width
WIN_H     equ 16   ; total window height (= glyph height)
CLOCK_W   equ 40   ; 5 chars * 8px
GAP       equ 4
DOT_W     equ 12
DOT_H     equ 12
DOT_REL_X equ 44   ; CLOCK_W + GAP, relative to window left
DOT_REL_Y equ 2    ; (WIN_H - DOT_H) / 2, centres dot vertically

_start:
    ; Place window in the top-right corner.
    mov rax, SYS_FB_WIDTH
    int 0x80
    sub rax, WIN_W          ; win_x = fb_width - WIN_W
    mov [win_x], rax

    ; Create a chromeless window — no border, no title bar.
    mov rdi, [win_x]        ; cx = fb_width - WIN_W
    mov rsi, 0              ; cy = 0 (top of screen)
    mov rdx, WIN_W          ; cw
    mov rcx, WIN_H          ; ch
    mov r8,  WF_CHROMELESS  ; flags
    mov rax, SYS_CREATE_WINDOW
    int 0x80
    ; wid is recorded in our task by the kernel; we don't need it here.

.loop:
    ; ── blinking dot ────────────────────────────────────────────────────
    mov rax, SYS_GET_TICKS
    int 0x80
    xor rdx, rdx
    mov rcx, 25
    div rcx                 ; rax = ticks / 25
    and rax, 1              ; blink phase: 0 or 1

    movzx rbx, byte [dot_on]
    cmp rax, rbx
    je .no_dot_change
    mov [dot_on], al

    test al, al
    mov r8, 0x00FF00        ; green (on)
    jnz .do_fill
    mov r8, 0x000000        ; black (off)
.do_fill:
    mov rax, SYS_FILL_RECT
    mov rdi, DOT_REL_X      ; window-relative x
    mov rsi, DOT_REL_Y      ; window-relative y
    mov rdx, DOT_W
    mov rcx, DOT_H
    int 0x80

.no_dot_change:
    ; ── clock (redrawn once per minute) ─────────────────────────────────
    mov rax, SYS_GET_TIME
    int 0x80                ; rax = (hours << 8) | minutes

    mov rbx, rax
    and rbx, 0xFF           ; minutes
    shr rax, 8              ; hours

    movzx rcx, byte [last_minute]
    cmp rbx, rcx
    je .no_time_change
    mov [last_minute], bl

    ; format "HH:MM" into time_buf
    xor rdx, rdx
    mov rcx, 10
    div rcx                 ; rax=hours/10, rdx=hours%10
    add al, '0'
    mov [time_buf], al
    add dl, '0'
    mov [time_buf+1], dl
    mov byte [time_buf+2], ':'

    mov rax, rbx            ; minutes
    xor rdx, rdx
    mov rcx, 10
    div rcx                 ; rax=min/10, rdx=min%10
    add al, '0'
    mov [time_buf+3], al
    add dl, '0'
    mov [time_buf+4], dl
    mov byte [time_buf+5], 0

    mov rax, SYS_DRAW_TEXT
    mov rdi, 0              ; window-relative x
    mov rsi, 0              ; window-relative y
    mov rdx, time_buf
    mov rcx, 0x00FF00       ; green fg
    mov r8,  0x000000       ; black bg
    int 0x80

.no_time_change:
    mov rax, SYS_YIELD
    int 0x80
    jmp .loop

section .data
time_buf:    db "00:00", 0

section .bss
win_x:       resq 1
dot_on:      resb 1
last_minute: resb 1
