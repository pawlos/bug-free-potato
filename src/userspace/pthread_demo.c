/* pthread_demo.c — exercises mutex, condvar, and basic thread lifecycle. */

#include <libc/pthread.h>
#include <libc/syscall.h>
#include <libc/stdio.h>

/* Write a formatted string to both stdout and COM1 serial (for easy capture). */
static void kprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void kprintf(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    printf("%s", buf);
    if (n > 0)
        sys_write_serial(buf, (size_t)n);
}

/* ── Test 1: mutex-protected counter ──────────────────────────────────── */

#define NTHREADS  4
#define INCREMENTS 500

static pthread_mutex_t g_counter_mutex = PTHREAD_MUTEX_INITIALIZER;
static volatile int    g_counter = 0;

static void *counter_worker(void *arg)
{
    (void)arg;
    for (int i = 0; i < INCREMENTS; i++) {
        pthread_mutex_lock(&g_counter_mutex);
        g_counter++;
        pthread_mutex_unlock(&g_counter_mutex);
    }
    return (void *)0;
}

static void test_mutex_counter(void)
{
    kprintf("[test1] mutex counter: %d threads x %d increments\n",
            NTHREADS, INCREMENTS);

    g_counter = 0;
    pthread_t tids[NTHREADS];
    for (int i = 0; i < NTHREADS; i++)
        pthread_create(&tids[i], (void *)0, counter_worker, (void *)0);

    for (int i = 0; i < NTHREADS; i++)
        pthread_join(tids[i], (void *)0);

    int expected = NTHREADS * INCREMENTS;
    if (g_counter == expected)
        kprintf("[test1] PASS  counter=%d (expected %d)\n", g_counter, expected);
    else
        kprintf("[test1] FAIL  counter=%d (expected %d)\n", g_counter, expected);
}

/* ── Test 2: producer / consumer via condvar ──────────────────────────── */

#define ITEMS 8

static pthread_mutex_t g_prod_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_prod_ready = PTHREAD_COND_INITIALIZER;
static pthread_cond_t  g_prod_space = PTHREAD_COND_INITIALIZER;

/* Single-slot mailbox */
static volatile int g_item     = -1;
static volatile int g_item_ready = 0;
static volatile int g_sum_produced = 0;
static volatile int g_sum_consumed = 0;

static void *producer(void *arg)
{
    (void)arg;
    for (int i = 0; i < ITEMS; i++) {
        pthread_mutex_lock(&g_prod_mutex);
        while (g_item_ready)
            pthread_cond_wait(&g_prod_space, &g_prod_mutex);
        g_item       = i * 10;
        g_item_ready = 1;
        g_sum_produced += g_item;
        pthread_cond_signal(&g_prod_ready);
        pthread_mutex_unlock(&g_prod_mutex);
    }
    return (void *)0;
}

static void *consumer(void *arg)
{
    (void)arg;
    for (int i = 0; i < ITEMS; i++) {
        pthread_mutex_lock(&g_prod_mutex);
        while (!g_item_ready)
            pthread_cond_wait(&g_prod_ready, &g_prod_mutex);
        g_sum_consumed += g_item;
        g_item_ready = 0;
        pthread_cond_signal(&g_prod_space);
        pthread_mutex_unlock(&g_prod_mutex);
    }
    return (void *)0;
}

static void test_producer_consumer(void)
{
    kprintf("[test2] producer/consumer condvar: %d items\n", ITEMS);

    g_item        = -1;
    g_item_ready  = 0;
    g_sum_produced = 0;
    g_sum_consumed = 0;

    pthread_t prod_tid, cons_tid;
    pthread_create(&prod_tid, (void *)0, producer, (void *)0);
    pthread_create(&cons_tid, (void *)0, consumer, (void *)0);

    pthread_join(prod_tid, (void *)0);
    pthread_join(cons_tid, (void *)0);

    if (g_sum_produced == g_sum_consumed && g_sum_produced > 0)
        kprintf("[test2] PASS  sum=%d\n", g_sum_consumed);
    else
        kprintf("[test2] FAIL  produced=%d consumed=%d\n",
                g_sum_produced, g_sum_consumed);
}

/* ── Test 3: parallel sum (return value via pthread_join) ─────────────── */

#define SUM_THREADS 4
#define SUM_N       100   /* sum 1..100 */

static void *partial_sum(void *arg)
{
    long id    = (long)arg;
    long chunk = SUM_N / SUM_THREADS;
    long start = id * chunk + 1;
    long end   = (id == SUM_THREADS - 1) ? SUM_N : start + chunk - 1;
    long sum   = 0;
    for (long i = start; i <= end; i++)
        sum += i;
    kprintf("[test3]   thread id=%ld range=[%ld..%ld] sum=%ld\n",
            id, start, end, sum);
    return (void *)sum;
}

static void test_parallel_sum(void)
{
    kprintf("[test3] parallel sum 1..%d across %d threads\n", SUM_N, SUM_THREADS);

    pthread_t tids[SUM_THREADS];
    for (long i = 0; i < SUM_THREADS; i++) {
        kprintf("[test3]   creating thread %ld with arg=%ld\n", i, i);
        pthread_create(&tids[i], (void *)0, partial_sum, (void *)i);
        kprintf("[test3]   created tid=%u\n", tids[i]);
    }

    long total = 0;
    for (int i = 0; i < SUM_THREADS; i++) {
        void *part;
        pthread_join(tids[i], &part);
        kprintf("[test3]   join[%d] tid=%u -> part=%ld\n", i, tids[i], (long)part);
        total += (long)part;
    }

    long expected = (long)SUM_N * (SUM_N + 1) / 2;  /* Gauss formula */
    if (total == expected)
        kprintf("[test3] PASS  sum=%ld (expected %ld)\n", total, expected);
    else
        kprintf("[test3] FAIL  sum=%ld (expected %ld)\n", total, expected);
}

/* ── main ────────────────────────────────────────────────────────────────── */

int main(void)
{
    kprintf("=== pthread demo ===\n");

    test_mutex_counter();
    test_producer_consumer();
    test_parallel_sum();

    kprintf("=== done ===\n");
    return 0;
}
