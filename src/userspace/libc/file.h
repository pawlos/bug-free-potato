#pragma once
#include "syscall.h"   /* size_t, ssize_t */
#include "stdio.h"     /* vsnprintf       */
#include <stdarg.h>

/* ── FILE type ───────────────────────────────────────────────────────────── */

#define _FILE_EOF    (1 << 0)
#define _FILE_ERR    (1 << 1)
#define _FILE_WRITE  (1 << 2)
#define _FILE_APPEND (1 << 3)

typedef struct PotatoFILE {
    int fd;
    int flags;
    int unget_char;   /* -1 = no pending char, else the pushed-back byte */
} PotatoFILE;

/* In C builds, FILE = PotatoFILE. In C++ builds where the system
   <cstdio> may define FILE as struct _IO_FILE, we still provide
   FILE as PotatoFILE — our fopen/fread etc. are the only implementations
   available in freestanding. Guard against double-typedef from system. */
#ifndef __FILE_defined
#define __FILE_defined
typedef PotatoFILE FILE;
#endif

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
int    fscanf  (FILE *f, const char *fmt, ...);
int    vfscanf (FILE *f, const char *fmt, va_list ap);

int    fflush(FILE *f);           /* no-op (unbuffered) */
int    remove(const char *path);  /* stub — VFS is read-only */
int    rename(const char *old, const char *new_name); /* stub */

/* getc/putc as real functions (C++ <cstdio> needs "using ::getc") */
static inline int getc(FILE *f) { return fgetc(f); }
static inline int putc(int c, FILE *f) { return fputc(c, f); }
#ifndef _POTATO_GETCHAR_DEFINED
#define _POTATO_GETCHAR_DEFINED
static inline int getchar(void) { return fgetc(stdin); }
#endif

int    fgetpos(FILE *stream, fpos_t *pos);
int    fsetpos(FILE *stream, const fpos_t *pos);
FILE  *freopen(const char *pathname, const char *mode, FILE *stream);
FILE  *tmpfile(void);
int    setvbuf(FILE *stream, char *buf, int mode, size_t size);
void   setbuf(FILE *stream, char *buf);
int    fileno(FILE *stream);
