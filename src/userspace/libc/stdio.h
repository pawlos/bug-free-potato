#pragma once
#include "syscall.h"  /* size_t */
#include <stdarg.h>

#define EOF (-1)

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

/* Pull in FILE*, fopen, fclose, fread, fprintf etc. — must be last to
   avoid circular-include ordering issues (file.h includes stdio.h). */
#include "file.h"
