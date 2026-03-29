#pragma once
#include "syscall.h"  /* for size_t */

#ifdef __cplusplus
extern "C" {
#endif


size_t strlen (const char *s);
char  *strcpy (char *dst, const char *src);
char  *strncpy(char *dst, const char *src, size_t n);
char  *strcat (char *dst, const char *src);
char  *strncat(char *dst, const char *src, size_t n);
int    strcmp (const char *a, const char *b);
int    strncmp(const char *a, const char *b, size_t n);
char  *strchr (const char *s, int c);
char  *strrchr(const char *s, int c);
char  *strstr (const char *haystack, const char *needle);
char  *strdup (const char *s);

void  *memcpy (void *dst, const void *src, size_t n);
void  *memset (void *dst, int c, size_t n);
void  *memmove(void *dst, const void *src, size_t n);
int    memcmp (const void *a, const void *b, size_t n);
void  *memchr (const void *s, int c, size_t n);

char  *strtok (char *s, const char *delim);
char  *strtok_r(char *s, const char *delim, char **saveptr);

int    strcasecmp (const char *a, const char *b);
int    strncasecmp(const char *a, const char *b, size_t n);

size_t strnlen(const char *s, size_t maxlen);
char  *strerror(int errnum);

size_t strcspn(const char *s, const char *reject);
size_t strspn(const char *s, const char *accept);
char  *strpbrk(const char *s, const char *accept);

/* strcoll: locale-unaware, just use strcmp */
static inline int strcoll(const char *a, const char *b) { return strcmp(a, b); }
static inline size_t strxfrm(char *dst, const char *src, size_t n) {
    size_t len = strlen(src);
    if (dst && n > 0) { strncpy(dst, src, n); if (n > 0) dst[n-1] = '\0'; }
    return len;
}

#ifdef __cplusplus
}
#endif
