#pragma once
#include "defs.h"

constexpr pt::uint64_t SYS_WRITE    = 0;  // rdi=null-terminated str ptr
constexpr pt::uint64_t SYS_EXIT     = 1;  // rdi=exit code (ignored for now)
constexpr pt::uint64_t SYS_READ_KEY = 2;  // returns: char (0-255) or (uint64)-1 if no key
