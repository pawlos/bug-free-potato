/*
 * xxd — hex dump utility for potatOS
 *
 * Usage: xxd <filename> [length]
 * Displays a classic hex dump: offset, hex pairs, ASCII.
 * Default length is 256 bytes.
 */

#include "libc/stdio.h"
#include "libc/stdlib.h"
#include "libc/syscall.h"

static int parse_uint(const char *s)
{
    int v = 0;
    while (*s >= '0' && *s <= '9')
        v = v * 10 + (*s++ - '0');
    return v;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        puts("Usage: xxd <filename> [length]");
        return 1;
    }

    const char *filename = argv[1];
    int length = 256;
    if (argc >= 3)
        length = parse_uint(argv[2]);
    if (length <= 0)
        length = 256;

    int fd = sys_open(filename);
    if (fd < 0) {
        printf("xxd: cannot open '%s'\n", filename);
        return 1;
    }

    unsigned char buf[512];
    int total = 0;
    int offset = 0;
    const char hex[] = "0123456789abcdef";

    while (total < length) {
        int want = sizeof(buf);
        if (want > length - total)
            want = length - total;

        long n = sys_read(fd, buf, (unsigned long)want);
        if (n <= 0)
            break;

        for (int pos = 0; pos < (int)n; pos += 16) {
            /* offset */
            printf("%08x: ", offset + pos);

            /* hex pairs */
            for (int i = 0; i < 16; i++) {
                if (pos + i < (int)n) {
                    unsigned char b = buf[pos + i];
                    putchar(hex[b >> 4]);
                    putchar(hex[b & 0xF]);
                } else {
                    putchar(' ');
                    putchar(' ');
                }
                if (i % 2 == 1)
                    putchar(' ');
            }

            /* ASCII */
            putchar(' ');
            for (int i = 0; i < 16 && pos + i < (int)n; i++) {
                unsigned char b = buf[pos + i];
                putchar((b >= 0x20 && b <= 0x7E) ? (char)b : '.');
            }
            putchar('\n');
        }

        offset += (int)n;
        total += (int)n;
    }

    sys_close(fd);
    return 0;
}
