#include "libc/stdio.h"
#include "libc/stdlib.h"
#include "libc/syscall.h"

int main(void)
{
    long my_pid = sys_getpid();
    printf("my PID = %ld\n", my_pid);

    long child = sys_fork();
    if (child == 0) {
        long cpid = sys_getpid();
        printf("child: my PID = %ld\n", cpid);
        return 0;
    } else if (child > 0) {
        int status = 0;
        sys_waitpid(child, &status);
        printf("parent: PID = %ld, child %ld exited with %d\n",
               my_pid, child, status);
    } else {
        printf("fork failed\n");
    }

    return 0;
}
