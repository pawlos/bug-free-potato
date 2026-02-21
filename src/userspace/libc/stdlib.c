#include "stdlib.h"
#include "syscall.h"

void *malloc(size_t size)
{
    return sys_mmap(size);
}

void free(void *ptr)
{
    sys_munmap(ptr);
}

void exit(int code)
{
    sys_exit(code);
    __builtin_unreachable();
}

char *utoa(unsigned long val, char *buf, int base)
{
    static const char digits[] = "0123456789abcdef";
    char tmp[65];
    int  i = 0;

    if (val == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return buf;
    }
    while (val) {
        tmp[i++] = digits[val % (unsigned)base];
        val /= (unsigned)base;
    }
    int j = 0;
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = '\0';
    return buf;
}

char *itoa(long val, char *buf, int base)
{
    if (base == 10 && val < 0) {
        buf[0] = '-';
        utoa((unsigned long)-val, buf + 1, base);
        return buf;
    }
    return utoa((unsigned long)val, buf, base);
}
