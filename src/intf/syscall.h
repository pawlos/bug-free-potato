#pragma once
#include "defs.h"

constexpr pt::uint64_t SYS_WRITE    = 0;  // rdi=null-terminated str ptr
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
