#pragma once
#include "defs.h"

constexpr pt::uint64_t SYS_WRITE    = 0;  // rdi=fd, rsi=buf ptr, rdx=count; fd=1→stdout
constexpr pt::uint64_t SYS_EXIT     = 1;  // rdi=exit code (ignored for now)
constexpr pt::uint64_t SYS_READ_KEY = 2;  // returns: char (0-255) or (uint64)-1 if no key
constexpr pt::uint64_t SYS_OPEN     = 3;  // rdi=filename ptr; returns fd (0-7) or (uint64)-1
constexpr pt::uint64_t SYS_READ     = 4;  // rdi=fd, rsi=buf ptr, rdx=count; returns bytes read or (uint64)-1
constexpr pt::uint64_t SYS_CLOSE    = 5;  // rdi=fd; returns 0 or (uint64)-1
constexpr pt::uint64_t SYS_MMAP     = 6;  // rdi=size; returns virt addr or (uint64)-1
constexpr pt::uint64_t SYS_MUNMAP   = 7;  // rdi=ptr; returns 0 or (uint64)-1
constexpr pt::uint64_t SYS_YIELD    = 8;  // cooperative yield
constexpr pt::uint64_t SYS_GET_TICKS = 9; // returns current tick count
constexpr pt::uint64_t SYS_GET_TIME  = 10; // returns (hours<<8)|minutes
constexpr pt::uint64_t SYS_FILL_RECT = 11; // rdi=x, rsi=y, rdx=w, rcx=h, r8=0xRRGGBB
constexpr pt::uint64_t SYS_DRAW_TEXT = 12; // rdi=x, rsi=y, rdx=str_ptr, rcx=fg, r8=bg
constexpr pt::uint64_t SYS_FB_WIDTH  = 13; // returns framebuffer width
constexpr pt::uint64_t SYS_FORK     = 14; // clone current task; returns child id (parent) or 0 (child)
constexpr pt::uint64_t SYS_EXEC     = 15; // rdi=filename ptr; replace image; returns 0 or -1
constexpr pt::uint64_t SYS_WAITPID  = 16; // rdi=child_id, rsi=exit_code_ptr; returns 0 or -1
constexpr pt::uint64_t SYS_PIPE        = 17; // rdi=int[2] ptr; fills [0]=rd_fd [1]=wr_fd; returns 0 or -1
constexpr pt::uint64_t SYS_LSEEK       = 18; // rdi=fd, rsi=offset, rdx=whence; returns new pos or -1
constexpr pt::uint64_t SYS_FB_HEIGHT   = 19; // returns framebuffer height in pixels
constexpr pt::uint64_t SYS_DRAW_PIXELS   = 20; // rdi=buf, rsi=x, rdx=y, rcx=w, r8=h — blit pixel buffer
// Returns (scancode | 0x100) if pressed, scancode if released, (uint64)-1 if queue empty.
constexpr pt::uint64_t SYS_GET_KEY_EVENT = 21; // no args
