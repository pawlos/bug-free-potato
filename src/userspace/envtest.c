#include "libc/stdio.h"
#include "libc/stdlib.h"

int main(void)
{
    printf("Environment variables:\n");
    if (environ) {
        for (char **ep = environ; *ep; ep++)
            printf("  %s\n", *ep);
    } else {
        printf("  (environ is NULL)\n");
    }

    const char *home = getenv("HOME");
    printf("getenv(HOME) = %s\n", home ? home : "(null)");

    const char *path = getenv("PATH");
    printf("getenv(PATH) = %s\n", path ? path : "(null)");

    const char *none = getenv("NONEXIST");
    printf("getenv(NONEXIST) = %s\n", none ? none : "(null)");

    return 0;
}
