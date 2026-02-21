#include "libc/stdio.h"
#include "libc/stdlib.h"
#include "libc/string.h"
#include "libc/syscall.h"

int main(void)
{
    puts("Hello from C userspace!");

    printf("2 + 2 = %d\n", 4);
    printf("hex:  0x%x\n", 0xDEAD);
    printf("long: %ld\n", 1234567890L);

    /* malloc / free round-trip */
    char *buf = malloc(64);
    if (buf) {
        strcpy(buf, "heap allocation works");
        printf("malloc: %s\n", buf);
        free(buf);
    }

    /* ticks via syscall */
    printf("ticks: %ld\n", sys_get_ticks());

    puts("bye!");
    return 0;
}
