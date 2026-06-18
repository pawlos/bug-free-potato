#pragma once
/* NOTE: do NOT include syscall.h here at the top.  syscall.h includes
 * <stdint.h> and now uses uint32_t/uint64_t (the pthread syscall wrappers).
 * If a TU reaches <stdint.h> first, the #pragma once would make syscall.h's
 * re-include a no-op and syscall.h would compile before these typedefs exist.
 * syscall.h is pulled in at the BOTTOM purely so `#include <stdint.h>` still
 * transitively provides size_t, the way it did before. */

typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long      uint64_t;

typedef signed char        int8_t;
typedef short              int16_t;
typedef int                int32_t;
typedef long               int64_t;

typedef unsigned long      uintptr_t;
typedef long               intptr_t;

typedef long               intmax_t;
typedef unsigned long      uintmax_t;

typedef unsigned int       uint_fast8_t;
typedef unsigned int       uint_fast16_t;
typedef unsigned int       uint_fast32_t;
typedef unsigned long      uint_fast64_t;
typedef int                int_fast8_t;
typedef int                int_fast16_t;
typedef int                int_fast32_t;
typedef long               int_fast64_t;

typedef signed char        int_least8_t;
typedef short              int_least16_t;
typedef int                int_least32_t;
typedef long               int_least64_t;
typedef unsigned char      uint_least8_t;
typedef unsigned short     uint_least16_t;
typedef unsigned int       uint_least32_t;
typedef unsigned long      uint_least64_t;

#define INT8_MIN    (-128)
#define INT8_MAX    127
#define INT16_MIN   (-32768)
#define INT16_MAX   32767
#define INT32_MIN   (-2147483647-1)
#define INT32_MAX   2147483647
#define INT64_MIN   (-9223372036854775807L-1)
#define INT64_MAX   9223372036854775807L

#define UINT8_MAX   255
#define UINT16_MAX  65535
#define UINT32_MAX  4294967295U
#define UINT64_MAX  18446744073709551615UL

#define SIZE_MAX    UINT64_MAX

#define INT8_C(x)   (x)
#define INT16_C(x)  (x)
#define INT32_C(x)  (x)
#define INT64_C(x)  (x ## L)
#define UINT8_C(x)  (x)
#define UINT16_C(x) (x)
#define UINT32_C(x) (x ## U)
#define UINT64_C(x) (x ## UL)

/* Pulled in last (see note at top): keeps size_t available to code that does
 * `#include <stdint.h>` without re-creating the stdint↔syscall include cycle.
 * By now every uint*_t typedef above is visible to syscall.h. */
#include "syscall.h"
