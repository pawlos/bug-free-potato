#include "libc/stdio.h"
#include "libc/string.h"
#include "libc/syscall.h"

int main(void)
{
    puts("[pipe_test] starting");

    int pipefd[2];
    if (sys_pipe(pipefd) != 0) {
        puts("[pipe_test] FAIL: sys_pipe returned error");
        return 1;
    }
    printf("[pipe_test] pipe: rd=%d wr=%d\n", pipefd[0], pipefd[1]);

    long child = sys_fork();
    if (child < 0) {
        puts("[pipe_test] FAIL: fork returned error");
        return 1;
    }

    if (child == 0) {
        /* Child: close read end, write a message, close write end, exit */
        sys_close(pipefd[0]);
        const char *msg = "hello from child via pipe";
        sys_write(pipefd[1], msg, strlen(msg));
        sys_close(pipefd[1]);
        sys_exit(0);
    }

    /* Parent: close write end, read from pipe, print it */
    sys_close(pipefd[1]);

    char buf[64];
    long n = sys_read(pipefd[0], buf, sizeof(buf) - 1);
    sys_close(pipefd[0]);

    int status = -1;
    sys_waitpid(child, &status);

    if (n <= 0) {
        puts("[pipe_test] FAIL: read returned no data");
        return 1;
    }
    buf[n] = '\0';
    printf("[pipe_test] parent read %ld bytes: '%s'\n", n, buf);

    const char *expected = "hello from child via pipe";
    size_t exp_len = strlen(expected);
    int match = (n == (long)exp_len);
    for (size_t i = 0; match && i < exp_len; i++)
        match = (buf[i] == expected[i]);
    if (match) {
        puts("[pipe_test] PASS");
    } else {
        puts("[pipe_test] FAIL: unexpected data");
        return 1;
    }

    return 0;
}
