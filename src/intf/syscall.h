#pragma once
#include "defs.h"

constexpr pt::uint64_t SYS_WRITE    = 0;  // rdi=null-terminated str ptr
constexpr pt::uint64_t SYS_EXIT     = 1;  // rdi=exit code (ignored for now)
constexpr pt::uint64_t SYS_READ_KEY = 2;  // returns: char (0-255) or (uint64)-1 if no key
constexpr pt::uint64_t SYS_OPEN     = 3;  // rdi=filename ptr; returns fd (0-7) or (uint64)-1
constexpr pt::uint64_t SYS_READ     = 4;  // rdi=fd, rsi=buf ptr, rdx=count; returns bytes read or (uint64)-1
constexpr pt::uint64_t SYS_CLOSE    = 5;  // rdi=fd; returns 0 or (uint64)-1
