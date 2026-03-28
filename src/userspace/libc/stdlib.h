#pragma once
#include "syscall.h"  /* for size_t */
#include "time.h"     /* time_t     */

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

#ifndef NULL
#define NULL ((void *)0)
#endif

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
extern char **environ;
int    rand(void);
void   srand(unsigned int seed);
void   qsort(void *base, size_t nmemb, size_t size,
             int (*cmp)(const void *, const void *));

/* alloca — stack allocation */
#define alloca __builtin_alloca

double atof  (const char *s);
double strtod(const char *s, char **endptr);
float  strtof(const char *s, char **endptr);
int    system(const char *cmd);
int    mkdir(const char *path, unsigned int mode);

/* sleep functions — guarded so they don't conflict with system <unistd.h>
   declarations when Doom source files pull in system headers. */
#ifndef _UNISTD_H
unsigned int sleep(unsigned int seconds);
int          usleep(unsigned int usec);
#endif
