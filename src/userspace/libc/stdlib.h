#pragma once
#include "syscall.h"  /* for size_t */
#include "time.h"     /* time_t     */

void  *malloc(size_t size);
void  *calloc(size_t nmemb, size_t size);
void  *realloc(void *ptr, size_t size);
void   free(void *ptr);
void   exit(int code) __attribute__((noreturn));
int    atexit(void (*func)(void));

/* integer conversion */
char  *itoa(long val,          char *buf, int base);
char  *utoa(unsigned long val, char *buf, int base);
int    atoi(const char *s);
long   atol(const char *s);
long   strtol (const char *s, char **endptr, int base);
unsigned long strtoul(const char *s, char **endptr, int base);

/* misc */
int    abs(int x);
long   labs(long x);
char  *getenv(const char *name);
int    rand(void);
void   srand(unsigned int seed);
void   qsort(void *base, size_t nmemb, size_t size,
             int (*cmp)(const void *, const void *));

double atof(const char *s);
int    system(const char *cmd);
int    mkdir(const char *path, unsigned int mode);
