#pragma once
#include <stdint.h>

/* x86_64 lock-free atomic helpers for mutex/pthread implementation.
 * Userspace is compiled without -masm=intel, so both constraints and
 * instruction text follow GCC's default AT&T inline-asm convention. */

/* Compare-and-swap: atomically, if *ptr == expected set *ptr = desired.
 * Returns the OLD value of *ptr.
 * If old == expected, the swap occurred. */
static inline uint32_t atomic_cmpxchg(uint32_t *ptr, uint32_t expected,
                                       uint32_t desired)
{
    uint32_t old;
    __asm__ volatile(
        "lock cmpxchg %2, %1"
        : "=a"(old), "+m"(*ptr)
        : "r"(desired), "0"(expected)
        : "memory"
    );
    return old;
}

/* Atomic exchange: set *ptr = val, return the old value. */
static inline uint32_t atomic_xchg(uint32_t *ptr, uint32_t val)
{
    /* xchg with a memory operand always has an implicit lock prefix on x86;
     * no explicit lock prefix is needed or written here. */
    __asm__ volatile(
        "xchg %0, %1"
        : "+r"(val), "+m"(*ptr)
        :
        : "memory"
    );
    return val;
}

/* Atomic fetch-and-add: atomically *ptr += val, return OLD value. */
static inline uint32_t atomic_fetch_add(uint32_t *ptr, uint32_t val)
{
    uint32_t old = val;
    __asm__ volatile(
        "lock xadd %0, %1"
        : "+r"(old), "+m"(*ptr)
        :
        : "memory"
    );
    return old;   /* xadd stores the original *ptr in old */
}

/* Atomic fetch-and-subtract: atomically *ptr -= val, return OLD value. */
static inline uint32_t atomic_fetch_sub(uint32_t *ptr, uint32_t val)
{
    /* fetch_sub(x) == fetch_add(-x) */
    return atomic_fetch_add(ptr, (uint32_t)(-(int32_t)val));
}
