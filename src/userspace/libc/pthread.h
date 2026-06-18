#pragma once
#include <stdint.h>
#include "atomic.h"

/* ── pthread types ──────────────────────────────────────────────────────── */

typedef uint32_t pthread_t;      /* kernel task ID */
typedef int      pthread_attr_t; /* reserved for ABI compat, unused */

/* 3-state mutex: 0=free, 1=held (no waiters), 2=held (waiters sleeping)
 * `owner` is purely diagnostic — set on acquire, cleared on release. */
typedef struct {
    uint32_t state;
    uint32_t owner;
} pthread_mutex_t;

#define PTHREAD_MUTEX_INITIALIZER { 0, 0 }

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

/* ── Extended POSIX surface for host libstdc++ <bits/gthr-default.h> ───────── *
 *
 * The toolchain builds C++ userspace (DevilutionX, ScummVM …) against the HOST
 * libstdc++ headers (the command line is not -nostdinc++).  Any STL header that
 * touches threading — <mutex>, <thread>, <shared_mutex>, and even <sstream> /
 * <memory_resource> via <atomic_wait> — pulls in <bits/gthr-default.h>, which
 * does `#include <pthread.h>` and expects glibc's full pthread surface.
 *
 * Because the build passes `-isystem src/userspace/libc`, THIS header shadows
 * the host <pthread.h>; so we must declare every type/macro/function gthr
 * references or those translation units fail to compile (they did, until a
 * pthread.h was first added to the shim — before that gthr saw the complete
 * host <pthread.h>).  gthr references these as weak symbols, so declarations
 * are enough to compile and link: userspace that never starts a std::thread
 * never calls them.  Keep these as a compile-compat shim, not a full impl.
 */
struct timespec;  /* forward decl — only used through pointers below */

/* Thread-specific data keys (gthr __gthread_key_t) */
typedef uint32_t pthread_key_t;
int   pthread_key_create(pthread_key_t *key, void (*destructor)(void *));
int   pthread_key_delete(pthread_key_t key);
void *pthread_getspecific(pthread_key_t key);
int   pthread_setspecific(pthread_key_t key, const void *value);

/* One-time initialization (gthr __gthread_once_t) */
typedef int pthread_once_t;
#define PTHREAD_ONCE_INIT 0
int pthread_once(pthread_once_t *once_control, void (*init_routine)(void));

/* Read/write lock (host libstdc++ std::shared_mutex) */
typedef struct {
    uint32_t readers;
    uint32_t writer;
} pthread_rwlock_t;
#define PTHREAD_RWLOCK_INITIALIZER { 0, 0 }
int pthread_rwlock_init(pthread_rwlock_t *rw, const void *attr);
int pthread_rwlock_destroy(pthread_rwlock_t *rw);
int pthread_rwlock_rdlock(pthread_rwlock_t *rw);
int pthread_rwlock_tryrdlock(pthread_rwlock_t *rw);
int pthread_rwlock_wrlock(pthread_rwlock_t *rw);
int pthread_rwlock_trywrlock(pthread_rwlock_t *rw);
int pthread_rwlock_timedrdlock(pthread_rwlock_t *rw, const struct timespec *abstime);
int pthread_rwlock_timedwrlock(pthread_rwlock_t *rw, const struct timespec *abstime);
int pthread_rwlock_unlock(pthread_rwlock_t *rw);

/* Recursive-mutex attributes (gthr __gthread_recursive_mutex_t) */
#define PTHREAD_MUTEX_RECURSIVE 1
int pthread_mutexattr_init(pthread_mutexattr_t *a);
int pthread_mutexattr_settype(pthread_mutexattr_t *a, int type);
int pthread_mutexattr_destroy(pthread_mutexattr_t *a);
int pthread_mutex_timedlock(pthread_mutex_t *m, const struct timespec *abstime);
int pthread_mutex_clocklock(pthread_mutex_t *m, int clock,
                            const struct timespec *abstime);
int pthread_rwlock_clockrdlock(pthread_rwlock_t *rw, int clock,
                               const struct timespec *abstime);
int pthread_rwlock_clockwrlock(pthread_rwlock_t *rw, int clock,
                               const struct timespec *abstime);

/* Misc thread ops referenced by gthr */
int pthread_detach(pthread_t tid);
int pthread_equal(pthread_t a, pthread_t b);
int pthread_cond_timedwait(pthread_cond_t *c, pthread_mutex_t *m,
                           const struct timespec *abstime);
int pthread_cond_clockwait(pthread_cond_t *c, pthread_mutex_t *m,
                           int clock, const struct timespec *abstime);
int sched_yield(void);
