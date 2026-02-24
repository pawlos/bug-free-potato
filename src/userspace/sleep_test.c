#include "libc/stdio.h"
#include "libc/stdlib.h"
#include "libc/syscall.h"

int main(void)
{
    puts("=== sleep test ===");

    /* Test 1: basic blocking sleep */
    puts("Test 1: sleeping 500 ms...");
    long t0 = sys_get_ticks();
    sys_sleep_ms(500);
    long t1 = sys_get_ticks();
    long elapsed_ms = (t1 - t0) * 20;   /* 50 Hz → 20 ms per tick */
    printf("  elapsed: %ld ms (expected ~500)\n", elapsed_ms);
    if (elapsed_ms >= 480 && elapsed_ms <= 600)
        puts("  PASS");
    else
        puts("  FAIL");

    /* Test 2: short sleep (< 1 tick → rounds up to 1 tick = 20 ms) */
    puts("Test 2: sleeping 1 ms (rounds up to 1 tick)...");
    t0 = sys_get_ticks();
    sys_sleep_ms(1);
    t1 = sys_get_ticks();
    elapsed_ms = (t1 - t0) * 20;
    printf("  elapsed: %ld ms (expected 20-40)\n", elapsed_ms);
    if (elapsed_ms >= 20 && elapsed_ms <= 40)
        puts("  PASS");
    else
        puts("  FAIL");

    /* Test 3: zero sleep returns immediately */
    puts("Test 3: sleeping 0 ms (no-op)...");
    t0 = sys_get_ticks();
    sys_sleep_ms(0);
    t1 = sys_get_ticks();
    elapsed_ms = (t1 - t0) * 20;
    printf("  elapsed: %ld ms (expected 0)\n", elapsed_ms);
    if (elapsed_ms == 0)
        puts("  PASS");
    else
        puts("  FAIL");

    /* Test 4: sleep() — 3-second visible countdown */
    puts("Test 4: 3-second countdown via sleep()...");
    for (int i = 3; i >= 1; i--) {
        printf("  %d...\n", i);
        sleep(1);
    }
    puts("  done");

    /* Test 5: usleep() — 500 ms via microseconds */
    puts("Test 5: usleep(500000)...");
    t0 = sys_get_ticks();
    usleep(500000);
    t1 = sys_get_ticks();
    elapsed_ms = (t1 - t0) * 20;
    printf("  elapsed: %ld ms (expected ~500)\n", elapsed_ms);
    if (elapsed_ms >= 480 && elapsed_ms <= 600)
        puts("  PASS");
    else
        puts("  FAIL");

    puts("=== sleep test complete ===");
    return 0;
}
