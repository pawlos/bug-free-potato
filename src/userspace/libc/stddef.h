#pragma once
#include "syscall.h"  /* size_t */
#include "stdint.h"   /* int32_t etc. — needed by system stdlib.h when we shadow stddef.h */

#ifndef NULL
#ifdef __cplusplus
#define NULL 0
#else
#define NULL ((void *)0)
#endif
#endif

typedef long ptrdiff_t;
typedef long double max_align_t;
