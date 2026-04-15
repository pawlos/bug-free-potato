#include "pthread.h"
#include "syscall.h"
#include "errno.h"

/* ── internal constants ─────────────────────────────────────────────────── */

#define PTHREAD_STACK_SIZE  (1024UL * 1024UL)  /* 1 MB per thread */
#define MAX_PTHREAD_THREADS 32                  /* matches kernel MAX_TASKS */

/* ── per-thread stack registry ──────────────────────────────────────────── *
 * Indexed by (tid % MAX_PTHREAD_THREADS).  pthread_create fills these in;
 * pthread_join drains them after the thread exits so the stack can be freed.
 *
 * Note: if the kernel recycles a TID before pthread_join() is called for
 * the previous owner, a new pthread_create() will overwrite the slot,
 * leaking the old thread's stack.  With MAX_TASKS=32 this is benign in
 * practice — always join your threads.
 */
static void     *g_thread_stack_base[MAX_PTHREAD_THREADS];
static uint64_t  g_thread_stack_size[MAX_PTHREAD_THREADS];

/* ── thread trampoline ──────────────────────────────────────────────────── *
 * Called by the kernel as the thread's first RIP.
 * RDI = (ThreadControl *) tc — the same pointer used for FS_BASE.
 * Reads start_fn/real_arg from the TC, calls the user function, then exits.
 */
static void thread_trampoline(void *tc_ptr)
{
    ThreadControl *tc = (ThreadControl *)tc_ptr;
    void *(*start_fn)(void *) = tc->start_fn;
    void *real_arg            = tc->real_arg;
    void *result              = start_fn(real_arg);
    pthread_exit(result);
}

/* ── pthread_create ─────────────────────────────────────────────────────── */

int pthread_create(pthread_t *tid_out, const pthread_attr_t *attr,
                   void *(*start_fn)(void *), void *arg)
{
    (void)attr;

    /* Allocate the thread's stack. */
    void *stack_base = sys_mmap(PTHREAD_STACK_SIZE);
    if (!stack_base || (long)stack_base == -1)
        return ENOMEM;

    /* ThreadControl lives at the lowest address of the stack region.
     * The stack grows downward from the top, so this area is safe. */
    ThreadControl *tc  = (ThreadControl *)stack_base;
    tc->tls_self    = (uint64_t)(unsigned long)tc; /* %fs:0 self-pointer */
    tc->tid         = 0;                        /* updated below */
    tc->errno_val   = 0;
    tc->detached    = 0;
    tc->_pad        = 0;
    tc->stack_base  = (uint64_t)(unsigned long)stack_base;
    tc->stack_size  = PTHREAD_STACK_SIZE;
    tc->stack_canary = 0;
    tc->start_fn    = start_fn;
    tc->real_arg    = arg;

    /* Initial RSP for the new thread = one byte past the top of the region. */
    void *stack_top = (char *)stack_base + PTHREAD_STACK_SIZE;

    /* Spawn the kernel thread.
     * entry    = thread_trampoline (first RIP)
     * stack_ptr = stack_top        (initial RSP)
     * arg      = tc                (placed in RDI on first dispatch)
     * tls_base = tc                (written to FS_BASE by the kernel) */
    uint32_t new_tid = sys_thread_create((void *)thread_trampoline,
                                          stack_top,
                                          (void *)tc,
                                          (void *)tc);
    if (new_tid == (uint32_t)-1) {
        sys_munmap(stack_base, PTHREAD_STACK_SIZE);
        return EAGAIN;
    }

    /* Register stack for deferred cleanup in pthread_join(). */
    uint32_t idx = new_tid % MAX_PTHREAD_THREADS;
    g_thread_stack_base[idx] = stack_base;
    g_thread_stack_size[idx] = PTHREAD_STACK_SIZE;

    if (tid_out)
        *tid_out = (pthread_t)new_tid;

    return 0;
}

/* ── pthread_join ───────────────────────────────────────────────────────── */

int pthread_join(pthread_t tid, void **retval)
{
    uint64_t result = sys_thread_join((uint32_t)tid);
    if (result == (uint64_t)-1)
        return ESRCH;

    if (retval)
        *retval = (void *)result;

    /* Free the joined thread's stack now that the kernel slot is dead. */
    uint32_t idx = (uint32_t)tid % MAX_PTHREAD_THREADS;
    if (g_thread_stack_base[idx]) {
        sys_munmap(g_thread_stack_base[idx], (size_t)g_thread_stack_size[idx]);
        g_thread_stack_base[idx] = (void *)0;
        g_thread_stack_size[idx] = 0;
    }

    return 0;
}

/* ── pthread_exit ───────────────────────────────────────────────────────── */

void pthread_exit(void *retval)
{
    sys_thread_exit(retval);
    /* sys_thread_exit does not return; this silences the compiler. */
    for (;;) sys_yield();
}

/* ── pthread_self ───────────────────────────────────────────────────────── */

pthread_t pthread_self(void)
{
    return (pthread_t)(uint32_t)sys_getpid();
}

/* ── mutex ──────────────────────────────────────────────────────────────── *
 *
 * 3-state design (Drepper / glibc-style):
 *   0 = unlocked
 *   1 = locked, no waiters   → unlock can skip futex_wake
 *   2 = locked, with waiters → unlock must futex_wake
 *
 * Lock fast path: CAS(0 → 1) — no syscall on uncontended acquire.
 * Unlock fast path: XCHG(→ 0), skip wake if old state was 1.
 */

int pthread_mutex_init(pthread_mutex_t *m, const pthread_mutexattr_t *a)
{
    (void)a;
    m->state = 0;
    return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *m)
{
    (void)m;
    return 0;
}

int pthread_mutex_lock(pthread_mutex_t *m)
{
    /* Attempt uncontended acquire (0 → 1). */
    if (atomic_cmpxchg(&m->state, 0, 1) == 0)
        return 0;

    /* Contended: mark waiters present (state→2) then sleep until state==0.
     * A single XCHG per loop iteration; the while condition retries on spurious
     * or competitive wake without a redundant exchange. */
    uint32_t s = atomic_xchg(&m->state, 2);
    while (s != 0) {
        sys_futex(FUTEX_WAIT, &m->state, 2);
        s = atomic_xchg(&m->state, 2);
    }

    return 0;
}

int pthread_mutex_trylock(pthread_mutex_t *m)
{
    return (atomic_cmpxchg(&m->state, 0, 1) == 0) ? 0 : EBUSY;
}

int pthread_mutex_unlock(pthread_mutex_t *m)
{
    /* Atomically clear the mutex; remember whether waiters were sleeping. */
    uint32_t old = atomic_xchg(&m->state, 0);
    if (old == 2)
        sys_futex(FUTEX_WAKE, &m->state, 1);
    return 0;
}

/* ── condition variable ─────────────────────────────────────────────────── *
 *
 * Sequence-counter design:
 *   cond->seq is incremented by signal/broadcast.
 *   Waiters snapshot seq while holding the mutex, release the mutex, then
 *   call FUTEX_WAIT(seq, snapshot).  If a signal arrived between the snapshot
 *   and the wait, the kernel sees seq != snapshot and returns immediately —
 *   no wakeup is ever lost.
 */

int pthread_cond_init(pthread_cond_t *c, const pthread_condattr_t *a)
{
    (void)a;
    c->seq     = 0;
    c->waiters = 0;
    return 0;
}

int pthread_cond_destroy(pthread_cond_t *c)
{
    (void)c;
    return 0;
}

int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m)
{
    /* Snapshot seq while we still hold the mutex (prevents lost wakeup). */
    uint32_t seq = c->seq;
    atomic_fetch_add(&c->waiters, 1);

    pthread_mutex_unlock(m);

    /* Sleep until seq changes.  Returns immediately if signal already fired. */
    sys_futex(FUTEX_WAIT, &c->seq, seq);

    atomic_fetch_sub(&c->waiters, 1);
    pthread_mutex_lock(m);
    return 0;
}

int pthread_cond_signal(pthread_cond_t *c)
{
    atomic_fetch_add(&c->seq, 1);
    if (c->waiters > 0)
        sys_futex(FUTEX_WAKE, &c->seq, 1);
    return 0;
}

int pthread_cond_broadcast(pthread_cond_t *c)
{
    atomic_fetch_add(&c->seq, 1);
    if (c->waiters > 0)
        /* Wake all waiters — MAX_PTHREAD_THREADS is an upper bound. */
        sys_futex(FUTEX_WAKE, &c->seq, MAX_PTHREAD_THREADS);
    return 0;
}
