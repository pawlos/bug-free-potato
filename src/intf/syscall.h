#pragma once
#include "defs.h"

constexpr pt::uint64_t SYS_WRITE = 0;  // rdi=null-terminated str ptr
constexpr pt::uint64_t SYS_EXIT  = 1;  // rdi=exit code (ignored for now)
