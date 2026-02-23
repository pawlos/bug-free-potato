#pragma once
#include "syscall.h"   /* size_t, ssize_t */
#include "stdio.h"     /* vsnprintf       */
#include <stdarg.h>

/* ── FILE type ───────────────────────────────────────────────────────────── */

#define _FILE_EOF    (1 << 0)
#define _FILE_ERR    (1 << 1)
#define _FILE_WRITE  (1 << 2)
#define _FILE_APPEND (1 << 3)

typedef struct {
    int fd;
    int flags;
    int unget_char;   /* -1 = no pending char, else the pushed-back byte */
} FILE;

/* Standard streams */
extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

/* SEEK constants */
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* ── API ─────────────────────────────────────────────────────────────────── */

FILE  *fopen (const char *path, const char *mode);
int    fclose(FILE *f);
size_t fread (void *ptr, size_t size, size_t nmemb, FILE *f);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *f);
int    fseek (FILE *f, long offset, int whence);
long   ftell (FILE *f);
void   rewind(FILE *f);
int    feof  (FILE *f);
int    ferror(FILE *f);
void   clearerr(FILE *f);

int    fputc (int c, FILE *f);
int    fgetc (FILE *f);
int    ungetc(int c, FILE *f);
char  *fgets (char *s, int n, FILE *f);
int    fputs (const char *s, FILE *f);

int    fprintf (FILE *f, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
int    vfprintf(FILE *f, const char *fmt, va_list ap);

/* Convenience macros */
#define getc(f)     fgetc(f)
#define putc(c, f)  fputc(c, f)
