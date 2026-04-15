#pragma once
#include <stdint.h>
#include "atomic.h"

/* ── pthread types ──────────────────────────────────────────────────────── */

typedef uint32_t pthread_t;      /* kernel task ID */
typedef int      pthread_attr_t; /* reserved for ABI compat, unused */

/* 3-state mutex: 0=free, 1=held (no waiters), 2=held (waiters sleeping) */
typedef struct {
    uint32_t state;
} pthread_mutex_t;

#define PTHREAD_MUTEX_INITIALIZER { 0 }

typedef int pthread_mutexattr_t; /* unused */

/* Sequence-counter condvar.
 * seq is incremented by signal/broadcast; waiters sleep on it via futex. */
typedef struct {
    uint32_t seq;      /* monotonically increasing wakeup counter */
    uint32_t waiters;  /* number of threads inside pthread_cond_wait */
} pthread_cond_t;

#define PTHREAD_COND_INITIALIZER { 0, 0 }

typedef int pthread_condattr_t; /* unused */

/* ── Thread-local storage control block ────────────────────────────────── *
 *
 * Field offsets are FIXED — errno.h and GCC's stack-protector assume:
 *   %fs:0    → self-pointer (pthread_self() / __errno_location())
 *   %fs:12   → per-thread errno
 *   %fs:0x28 → stack-canary slot (GCC -fstack-protector)
 *
 * ThreadControl lives at the low end of each thread's mmap'd stack.
 * The kernel stores its address as FS_BASE and restores it on every switch.
 */
typedef struct {
    uint64_t tls_self;          /* offset  0 (0x00): self-pointer */
    uint32_t tid;               /* offset  8 (0x08): kernel task ID */
    int      errno_val;         /* offset 12 (0x0C): per-thread errno */
    uint32_t detached;          /* offset 16 (0x10): reserved */
    uint32_t _pad;              /* offset 20 (0x14): alignment pad */
    uint64_t stack_base;        /* offset 24 (0x18): mmap base VA (for cleanup) */
    uint64_t stack_size;        /* offset 32 (0x20): mmap size */
    uint64_t stack_canary;      /* offset 40 (0x28): GCC stack-protector slot */
    void *(*start_fn)(void *);  /* offset 48 (0x30): thread entry (trampoline arg) */
    void    *real_arg;          /* offset 56 (0x38): argument for start_fn */
} ThreadControl;

/* ── API ─────────────────────────────────────────────────────────────────── */

/* Thread lifecycle */
int       pthread_create(pthread_t *tid, const pthread_attr_t *attr,
                         void *(*start_fn)(void *), void *arg);
int       pthread_join(pthread_t tid, void **retval);
void      pthread_exit(void *retval) __attribute__((noreturn));
pthread_t pthread_self(void);

/* Mutex */
int pthread_mutex_init(pthread_mutex_t *m, const pthread_mutexattr_t *a);
int pthread_mutex_destroy(pthread_mutex_t *m);
int pthread_mutex_lock(pthread_mutex_t *m);
int pthread_mutex_trylock(pthread_mutex_t *m);
int pthread_mutex_unlock(pthread_mutex_t *m);

/* Condition variable */
int pthread_cond_init(pthread_cond_t *c, const pthread_condattr_t *a);
int pthread_cond_destroy(pthread_cond_t *c);
int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m);
int pthread_cond_signal(pthread_cond_t *c);
int pthread_cond_broadcast(pthread_cond_t *c);
