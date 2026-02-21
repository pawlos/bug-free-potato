#include "stdio.h"
#include "stdlib.h"   /* itoa, utoa */
#include "string.h"   /* strlen     */
#include "syscall.h"  /* sys_write  */
#include <stdarg.h>

int putchar(int c)
{
    char buf[2] = { (char)c, '\0' };
    sys_write(buf);
    return (unsigned char)c;
}

int puts(const char *s)
{
    sys_write(s);
    sys_write("\n");
    return 0;
}

/* Append at most (cap - *pos - 1) chars of src into buf, updating *pos. */
static void buf_append(char *buf, int cap, int *pos, const char *src)
{
    while (*src && *pos < cap - 1)
        buf[(*pos)++] = *src++;
}

int printf(const char *fmt, ...)
{
    char out[512];
    int  pos = 0;
    char tmp[65];    /* large enough for 64-bit value in any base */

    va_list ap;
    va_start(ap, fmt);

    while (*fmt && pos < (int)sizeof(out) - 1) {
        if (*fmt != '%') {
            out[pos++] = *fmt++;
            continue;
        }
        fmt++;  /* skip '%' */

        /* Optional '+' flag for signed integers */
        int force_sign = 0;
        if (*fmt == '+') { force_sign = 1; fmt++; }

        switch (*fmt++) {
        case 'c':
            out[pos++] = (char)va_arg(ap, int);
            break;

        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            buf_append(out, (int)sizeof(out), &pos, s);
            break;
        }

        case 'd': {
            long v = va_arg(ap, int);
            if (force_sign && v >= 0) out[pos++] = '+';
            buf_append(out, (int)sizeof(out), &pos, itoa(v, tmp, 10));
            break;
        }

        case 'l': {
            /* %ld */
            if (*fmt == 'd') {
                fmt++;
                long v = va_arg(ap, long);
                if (force_sign && v >= 0) out[pos++] = '+';
                buf_append(out, (int)sizeof(out), &pos, itoa(v, tmp, 10));
            } else if (*fmt == 'u') {
                fmt++;
                buf_append(out, (int)sizeof(out), &pos,
                           utoa(va_arg(ap, unsigned long), tmp, 10));
            } else if (*fmt == 'x') {
                fmt++;
                buf_append(out, (int)sizeof(out), &pos,
                           utoa(va_arg(ap, unsigned long), tmp, 16));
            } else {
                out[pos++] = '%';
                out[pos++] = 'l';
            }
            break;
        }

        case 'u':
            buf_append(out, (int)sizeof(out), &pos,
                       utoa((unsigned)va_arg(ap, unsigned int), tmp, 10));
            break;

        case 'x':
            buf_append(out, (int)sizeof(out), &pos,
                       utoa((unsigned)va_arg(ap, unsigned int), tmp, 16));
            break;

        case 'X': {
            /* uppercase hex */
            utoa((unsigned)va_arg(ap, unsigned int), tmp, 16);
            for (char *p = tmp; *p; p++)
                if (*p >= 'a' && *p <= 'f') *p -= 32;
            buf_append(out, (int)sizeof(out), &pos, tmp);
            break;
        }

        case 'p':
            buf_append(out, (int)sizeof(out), &pos, "0x");
            buf_append(out, (int)sizeof(out), &pos,
                       utoa((unsigned long)va_arg(ap, void *), tmp, 16));
            break;

        case '%':
            out[pos++] = '%';
            break;

        default:
            out[pos++] = '%';
            break;
        }
    }

    out[pos] = '\0';
    va_end(ap);
    sys_write(out);
    return pos;
}
