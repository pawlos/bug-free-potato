#pragma once
#include "syscall.h"  /* size_t */
#include <stdarg.h>

#define EOF (-1)

typedef long fpos_t;

#ifndef NULL
#define NULL ((void *)0)
#endif

int putchar(int c);
int puts(const char *s);
int printf (const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int sprintf (char *buf,               const char *fmt, ...) __attribute__((format(printf, 2, 3)));
int snprintf(char *buf, size_t size,  const char *fmt, ...) __attribute__((format(printf, 3, 4)));
int vprintf (const char *fmt, va_list ap);
int vsprintf (char *buf,              const char *fmt, va_list ap);
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);

int sscanf (const char *str, const char *fmt, ...);
int vsscanf(const char *str, const char *fmt, va_list ap);
int scanf  (const char *fmt, ...);
int vscanf (const char *fmt, va_list ap);

int    remove(const char *filename);
int    rename(const char *oldpath, const char *newpath);
void   perror(const char *s);
char  *tmpnam(char *s);

#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2
#define BUFSIZ 1024
#define FILENAME_MAX 256
#define FOPEN_MAX 16
#define TMP_MAX 256
#define L_tmpnam 20

/* Pull in FILE*, fopen, fclose, fread, fprintf etc. — must be last to
   avoid circular-include ordering issues (file.h includes stdio.h). */
#include "file.h"
