#include "libc/stdio.h"
#include "libc/string.h"
#include "libc/syscall.h"

int main(void)
{
    puts("[fork_test] starting");

    /* ── Test 1: fork + waitpid ─────────────────────────────────────────── */
    long child = sys_fork();

    if (child < 0) {
        puts("[fork_test] FAIL: fork returned error");
        return 1;
    }

    if (child == 0) {
        /* Child process: rax=0 from fork */
        puts("[fork_test] child: running");
        sys_exit(42);
    }

    /* Parent continues here with child = child task ID */
    printf("[fork_test] parent: forked child pid=%ld\n", child);

    int code = -1;
    long r = sys_waitpid(child, &code);
    /* Immediate raw write — proves iretq returned to user space correctly */
    {
        const char *msg = "[fork_test] back in user after waitpid\n";
        sys_write(1, msg, strlen(msg));
    }
    if (r != 0) {
        puts("[fork_test] FAIL: waitpid returned error");
        return 1;
    }
    printf("[fork_test] parent: child exited with code %d (expected 42)\n", code);
    if (code != 42)
        puts("[fork_test] FAIL: wrong exit code");
    else
        puts("[fork_test] PASS: fork+waitpid");

    /* ── Test 2: exec ────────────────────────────────────────────────────── */
    puts("[fork_test] exec-ing HELLO.ELF ...");
    sys_exec("HELLO.ELF", 0, (const char* const*)0);

    /* Should never reach here if exec succeeded */
    puts("[fork_test] FAIL: exec returned");
    return 1;
}
