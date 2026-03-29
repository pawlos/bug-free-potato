#ifndef SDL_stdinc_h_
#define SDL_stdinc_h_

#ifdef __cplusplus
extern "C" {
#endif


typedef unsigned char  Uint8;
typedef unsigned short Uint16;
typedef unsigned int   Uint32;
typedef unsigned long long Uint64;
typedef signed char    Sint8;
typedef signed short   Sint16;
typedef signed int     Sint32;
typedef signed long long Sint64;

#ifndef _POTATO_SIZE_T
#define _POTATO_SIZE_T
typedef unsigned long size_t;
#endif

typedef int SDL_bool;
#define SDL_FALSE 0
#define SDL_TRUE  1

#define SDL_INLINE static inline
#define SDL_memset __builtin_memset
#define SDL_memcpy __builtin_memcpy
#define SDL_strlen __builtin_strlen
#define SDL_max(a,b) ((a)>(b)?(a):(b))
#define SDL_min(a,b) ((a)<(b)?(a):(b))
#define SDL_arraysize(a) (sizeof(a)/sizeof((a)[0]))
#define SDL_TABLESIZE(t) SDL_arraysize(t)

/* Compile-time assert used by SDLPoP types.h */
#define SDL_COMPILE_TIME_ASSERT(name, x) \
    typedef int SDL_compile_time_assert_##name[(x) ? 1 : -1]

#define SDL_zero(x) __builtin_memset(&(x), 0, sizeof((x)))
#define SDL_zeroa(x) __builtin_memset((x), 0, sizeof((x)))

/* stdbool compat for C */
#ifndef __cplusplus
#ifndef __bool_true_false_are_defined
#define bool  _Bool
#define true  1
#define false 0
#define __bool_true_false_are_defined 1
#endif
#endif


#ifdef __cplusplus
}
#endif

#endif
