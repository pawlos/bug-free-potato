#ifndef SDL_atomic_h_
#define SDL_atomic_h_

#include "SDL_stdinc.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { int value; } SDL_atomic_t;
typedef int SDL_SpinLock;

/* x86_64 LOCK CMPXCHG / XCHG / XADD; AT&T inline-asm (userspace). */

static inline SDL_bool SDL_AtomicCAS(SDL_atomic_t *a, int oldval, int newval)
{
    int old;
    __asm__ volatile("lock cmpxchg %2, %1"
                     : "=a"(old), "+m"(a->value)
                     : "r"(newval), "0"(oldval)
                     : "memory");
    return old == oldval ? SDL_TRUE : SDL_FALSE;
}

static inline int SDL_AtomicSet(SDL_atomic_t *a, int v)
{
    int old = v;
    __asm__ volatile("xchg %0, %1"
                     : "+r"(old), "+m"(a->value)
                     :
                     : "memory");
    return old;
}

static inline int SDL_AtomicGet(SDL_atomic_t *a)
{
    int v;
    __asm__ volatile("" ::: "memory");
    v = a->value;
    __asm__ volatile("" ::: "memory");
    return v;
}

static inline int SDL_AtomicAdd(SDL_atomic_t *a, int v)
{
    int old = v;
    __asm__ volatile("lock xadd %0, %1"
                     : "+r"(old), "+m"(a->value)
                     :
                     : "memory");
    return old;
}

static inline SDL_bool SDL_AtomicIncRef(SDL_atomic_t *a) { return SDL_AtomicAdd(a, 1) == 0 ? SDL_FALSE : SDL_TRUE; }
static inline SDL_bool SDL_AtomicDecRef(SDL_atomic_t *a) { return SDL_AtomicAdd(a, -1) == 1 ? SDL_TRUE : SDL_FALSE; }

static inline SDL_bool SDL_AtomicCASPtr(void **ptr, void *oldval, void *newval)
{
    void *old;
    __asm__ volatile("lock cmpxchgq %2, %1"
                     : "=a"(old), "+m"(*ptr)
                     : "r"(newval), "0"(oldval)
                     : "memory");
    return old == oldval ? SDL_TRUE : SDL_FALSE;
}

static inline void* SDL_AtomicSetPtr(void **ptr, void *v)
{
    void *old = v;
    __asm__ volatile("xchg %0, %1"
                     : "+r"(old), "+m"(*ptr)
                     :
                     : "memory");
    return old;
}

static inline void* SDL_AtomicGetPtr(void **ptr)
{
    void *v;
    __asm__ volatile("" ::: "memory");
    v = *ptr;
    __asm__ volatile("" ::: "memory");
    return v;
}

static inline void SDL_AtomicLock(SDL_SpinLock *lock)
{
    int one = 1, old;
    do {
        __asm__ volatile("xchg %0, %1"
                         : "+r"(one), "+m"(*lock) : : "memory");
        old = one;
        one = 1;
    } while (old != 0);
}

static inline SDL_bool SDL_AtomicTryLock(SDL_SpinLock *lock)
{
    int one = 1;
    __asm__ volatile("xchg %0, %1"
                     : "+r"(one), "+m"(*lock) : : "memory");
    return one == 0 ? SDL_TRUE : SDL_FALSE;
}

static inline void SDL_AtomicUnlock(SDL_SpinLock *lock)
{
    __asm__ volatile("" ::: "memory");
    *lock = 0;
}

#define SDL_CompilerBarrier()       __asm__ volatile("" ::: "memory")
#define SDL_MemoryBarrierAcquire()  __asm__ volatile("" ::: "memory")
#define SDL_MemoryBarrierRelease()  __asm__ volatile("" ::: "memory")

#ifdef __cplusplus
}
#endif

#endif
