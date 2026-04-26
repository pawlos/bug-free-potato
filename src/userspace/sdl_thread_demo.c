/*
 * sdl_thread_demo.c — exercises SDL_Thread / SDL_mutex / SDL_cond / SDL_sem.
 *
 * Layer 1 self-test for the SDL2 shim. Run with `exec BIN/SDL_THREAD_DEMO.ELF`.
 * Output goes to stdout and COM1 (so the host serial console captures it).
 */

#include "libc/SDL2/SDL.h"
#include "libc/syscall.h"
#include "libc/stdio.h"
#include <stdarg.h>

static void kprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void kprintf(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    printf("%s", buf);
    if (n > 0) sys_write_serial(buf, (size_t)n);
}

/* ── Test 1: SDL_CreateThread / SDL_WaitThread retval ─────────────────── */

static int square_worker(void *data)
{
    int n = (int)(long)data;
    return n * n;
}

static void test_thread_retval(void)
{
    kprintf("[t1] SDL_CreateThread / SDL_WaitThread retval\n");
    SDL_Thread *t = SDL_CreateThread(square_worker, "sq", (void*)(long)7);
    if (!t) { kprintf("[t1] FAIL: SDL_CreateThread returned NULL\n"); return; }
    int rc = -1;
    SDL_WaitThread(t, &rc);
    if (rc == 49) kprintf("[t1] PASS  rc=%d\n", rc);
    else          kprintf("[t1] FAIL  rc=%d (expected 49)\n", rc);
}

/* ── Test 2: SDL_mutex protected counter ──────────────────────────────── */

#define T2_THREADS 4
#define T2_INCS    500
static SDL_mutex *g_m;
static volatile int g_counter;

static int counter_worker(void *arg)
{
    (void)arg;
    for (int i = 0; i < T2_INCS; i++) {
        SDL_LockMutex(g_m);
        g_counter++;
        SDL_UnlockMutex(g_m);
    }
    return 0;
}

static void test_mutex(void)
{
    kprintf("[t2] SDL_mutex counter: %d threads x %d increments\n",
            T2_THREADS, T2_INCS);
    g_m = SDL_CreateMutex();
    if (!g_m) { kprintf("[t2] FAIL SDL_CreateMutex returned NULL\n"); return; }
    g_counter = 0;
    SDL_Thread *th[T2_THREADS];
    for (int i = 0; i < T2_THREADS; i++)
        th[i] = SDL_CreateThread(counter_worker, "cw", (void*)0);
    for (int i = 0; i < T2_THREADS; i++)
        SDL_WaitThread(th[i], (int*)0);
    int expected = T2_THREADS * T2_INCS;
    if (g_counter == expected) kprintf("[t2] PASS  counter=%d\n", g_counter);
    else                       kprintf("[t2] FAIL  counter=%d (expected %d)\n",
                                       g_counter, expected);
    SDL_DestroyMutex(g_m);
}

/* ── Test 3: SDL_sem rendezvous ────────────────────────────────────────── */

static SDL_sem *g_sem;
static volatile int g_after_sem;

static int sem_waiter(void *arg)
{
    (void)arg;
    SDL_SemWait(g_sem);
    g_after_sem = 1;
    return 0;
}

static void test_semaphore(void)
{
    kprintf("[t3] SDL_sem rendezvous\n");
    g_sem = SDL_CreateSemaphore(0);
    if (!g_sem) { kprintf("[t3] FAIL SDL_CreateSemaphore returned NULL\n"); return; }
    g_after_sem = 0;
    SDL_Thread *t = SDL_CreateThread(sem_waiter, "sw", (void*)0);
    /* Give the waiter time to block. */
    SDL_Delay(50);
    if (g_after_sem) {
        kprintf("[t3] FAIL waiter didn't block on empty sem\n");
    } else {
        SDL_SemPost(g_sem);
        SDL_WaitThread(t, (int*)0);
        if (g_after_sem) kprintf("[t3] PASS\n");
        else             kprintf("[t3] FAIL waiter never advanced\n");
    }
    SDL_DestroySemaphore(g_sem);
}

/* ── Test 4: SDL_cond producer/consumer ───────────────────────────────── */

#define T4_ITEMS 8
static SDL_mutex *g_pmu;
static SDL_cond  *g_cready, *g_cspace;
static volatile int g_pitem, g_pready, g_pproduced, g_pconsumed;

static int t4_producer(void *arg)
{
    (void)arg;
    for (int i = 0; i < T4_ITEMS; i++) {
        SDL_LockMutex(g_pmu);
        while (g_pready) SDL_CondWait(g_cspace, g_pmu);
        g_pitem = (i + 1) * 10;
        g_pready = 1;
        g_pproduced += g_pitem;
        SDL_CondSignal(g_cready);
        SDL_UnlockMutex(g_pmu);
    }
    return 0;
}

static int t4_consumer(void *arg)
{
    (void)arg;
    for (int i = 0; i < T4_ITEMS; i++) {
        SDL_LockMutex(g_pmu);
        while (!g_pready) SDL_CondWait(g_cready, g_pmu);
        g_pconsumed += g_pitem;
        g_pready = 0;
        SDL_CondSignal(g_cspace);
        SDL_UnlockMutex(g_pmu);
    }
    return 0;
}

static void test_cond(void)
{
    kprintf("[t4] SDL_cond producer/consumer: %d items\n", T4_ITEMS);
    g_pmu    = SDL_CreateMutex();
    g_cready = SDL_CreateCond();
    g_cspace = SDL_CreateCond();
    g_pitem = 0; g_pready = 0; g_pproduced = 0; g_pconsumed = 0;
    SDL_Thread *p = SDL_CreateThread(t4_producer, "p", (void*)0);
    SDL_Thread *c = SDL_CreateThread(t4_consumer, "c", (void*)0);
    SDL_WaitThread(p, (int*)0);
    SDL_WaitThread(c, (int*)0);
    if (g_pproduced == g_pconsumed && g_pproduced > 0)
        kprintf("[t4] PASS  sum=%d\n", g_pconsumed);
    else
        kprintf("[t4] FAIL  produced=%d consumed=%d\n", g_pproduced, g_pconsumed);
    SDL_DestroyCond(g_cspace);
    SDL_DestroyCond(g_cready);
    SDL_DestroyMutex(g_pmu);
}

/* ── Test 5: SDL_atomic ───────────────────────────────────────────────── */

static SDL_atomic_t g_atomic;

static int atomic_worker(void *arg)
{
    int n = (int)(long)arg;
    for (int i = 0; i < n; i++)
        SDL_AtomicAdd(&g_atomic, 1);
    return 0;
}

static void test_atomic(void)
{
    kprintf("[t5] SDL_atomic: 4 threads x 1000 adds\n");
    SDL_AtomicSet(&g_atomic, 0);
    SDL_Thread *th[4];
    for (int i = 0; i < 4; i++)
        th[i] = SDL_CreateThread(atomic_worker, "a", (void*)1000L);
    for (int i = 0; i < 4; i++) SDL_WaitThread(th[i], (int*)0);
    int got = SDL_AtomicGet(&g_atomic);
    if (got == 4000) kprintf("[t5] PASS  got=%d\n", got);
    else             kprintf("[t5] FAIL  got=%d (expected 4000)\n", got);
}

int main(void)
{
    kprintf("=== sdl_thread_demo ===\n");
    test_thread_retval();
    test_mutex();
    test_semaphore();
    test_cond();
    test_atomic();
    kprintf("=== done ===\n");
    return 0;
}
