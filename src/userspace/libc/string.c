#include "string.h"

size_t strlen(const char *s)
{
    size_t n = 0;
    while (*s++) n++;
    return n;
}

char *strcpy(char *dst, const char *src)
{
    char *d = dst;
    while ((*d++ = *src++));
    return dst;
}

char *strncpy(char *dst, const char *src, size_t n)
{
    size_t i = 0;
    for (; i < n && src[i]; i++) dst[i] = src[i];
    for (; i < n; i++) dst[i] = '\0';
    return dst;
}

char *strcat(char *dst, const char *src)
{
    char *d = dst + strlen(dst);
    while ((*d++ = *src++));
    return dst;
}

char *strncat(char *dst, const char *src, size_t n)
{
    char *d = dst + strlen(dst);
    while (n-- && *src) *d++ = *src++;
    *d = '\0';
    return dst;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i])
            return (unsigned char)a[i] - (unsigned char)b[i];
        if (!a[i]) break;
    }
    return 0;
}

char *strchr(const char *s, int c)
{
    for (; *s; s++) if ((unsigned char)*s == (unsigned char)c) return (char *)s;
    if (c == '\0') return (char *)s;
    return (char *)0;
}

char *strrchr(const char *s, int c)
{
    const char *last = (char *)0;
    for (; *s; s++) if ((unsigned char)*s == (unsigned char)c) last = s;
    if (c == '\0') return (char *)s;
    return (char *)last;
}

char *strstr(const char *haystack, const char *needle)
{
    if (!*needle) return (char *)haystack;
    size_t nlen = strlen(needle);
    for (; *haystack; haystack++)
        if (*haystack == needle[0] && strncmp(haystack, needle, nlen) == 0)
            return (char *)haystack;
    return (char *)0;
}

char *strdup(const char *s)
{
    /* Requires malloc — defined in stdlib.c */
    extern void *malloc(size_t);
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *p = (const unsigned char *)a;
    const unsigned char *q = (const unsigned char *)b;
    while (n--) {
        if (*p != *q) return (int)*p - (int)*q;
        p++; q++;
    }
    return 0;
}

void *memchr(const void *s, int c, size_t n)
{
    const unsigned char *p = (const unsigned char *)s;
    while (n--) { if (*p == (unsigned char)c) return (void *)p; p++; }
    return (void *)0;
}

void *memcpy(void *dst, const void *src, size_t n)
{
    char *d = dst;
    const char *s = src;
    while (n--) *d++ = *s++;
    return dst;
}

void *memset(void *dst, int c, size_t n)
{
    char *d = dst;
    while (n--) *d++ = (char)c;
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    char *d = dst;
    const char *s = src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n; s += n;
        while (n--) *--d = *--s;
    }
    return dst;
}

char *strtok_r(char *s, const char *delim, char **saveptr)
{
    if (!s) s = *saveptr;
    /* skip leading delimiters */
    while (*s && strchr(delim, *s)) s++;
    if (!*s) { *saveptr = s; return (char *)0; }
    char *tok = s;
    while (*s && !strchr(delim, *s)) s++;
    if (*s) { *s++ = '\0'; }
    *saveptr = s;
    return tok;
}

static char *strtok_ptr;
char *strtok(char *s, const char *delim)
{
    return strtok_r(s, delim, &strtok_ptr);
}

static int to_lower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

int strcasecmp(const char *a, const char *b)
{
    while (*a && *b) {
        int d = to_lower((unsigned char)*a) - to_lower((unsigned char)*b);
        if (d) return d;
        a++; b++;
    }
    return to_lower((unsigned char)*a) - to_lower((unsigned char)*b);
}

int strncasecmp(const char *a, const char *b, size_t n)
{
    while (n-- && *a && *b) {
        int d = to_lower((unsigned char)*a) - to_lower((unsigned char)*b);
        if (d) return d;
        a++; b++;
    }
    return n == (size_t)-1 ? 0 :
           to_lower((unsigned char)*a) - to_lower((unsigned char)*b);
}
