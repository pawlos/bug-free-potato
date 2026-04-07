#include "file.h"
#include "string.h"
#include "stdlib.h"
#include "syscall.h"
#include "stdio.h"
#include <stdarg.h>

/* ── standard stream instances ───────────────────────────────────────────── */

static FILE _stdin_impl  = { -1, _FILE_EOF, -1 };
static FILE _stdout_impl = {  1, _FILE_WRITE, -1 };
static FILE _stderr_impl = {  1, _FILE_WRITE, -1 };

FILE *stdin  = &_stdin_impl;
FILE *stdout = &_stdout_impl;
FILE *stderr = &_stderr_impl;

/* ── file pool ───────────────────────────────────────────────────────────── */
/* Max open FILE* (beyond the 3 predefined streams).
   Keep in sync with kernel Task::MAX_FDS (currently 32). */
#define FOPEN_MAX_USER 24

static FILE  _file_pool[FOPEN_MAX_USER];
static int   _file_used[FOPEN_MAX_USER];  /* 0 = free */

static FILE *pool_alloc(void)
{
    for (int i = 0; i < FOPEN_MAX_USER; i++) {
        if (!_file_used[i]) {
            _file_used[i] = 1;
            _file_pool[i].flags = 0;
            _file_pool[i].unget_char = -1;
            return &_file_pool[i];
        }
    }
    return (FILE *)0;
}

static void pool_free(FILE *f)
{
    for (int i = 0; i < FOPEN_MAX_USER; i++) {
        if (&_file_pool[i] == f) { _file_used[i] = 0; return; }
    }
}

/* ── fopen / fclose ──────────────────────────────────────────────────────── */

FILE *fopen(const char *path, const char *mode)
{
    /* Determine flags from mode string */
    int write_mode  = 0;
    int append_mode = 0;
    int rw_mode     = 0;
    if (mode) {
        for (const char *m = mode; *m; m++) {
            if (*m == 'w') write_mode  = 1;
            if (*m == 'a') append_mode = 1;
            if (*m == '+') rw_mode     = 1;
        }
    }

    int fd;
    if (write_mode && rw_mode) {
        /* "w+": create/truncate, read+write */
        fd = sys_create(path);
    } else if (write_mode) {
        /* "w": create new or truncate existing */
        fd = sys_create(path);
    } else if (append_mode) {
        /* "a": open existing (keeping content) or create fresh */
        fd = sys_open(path);
        if (fd < 0) fd = sys_create(path);
    } else if (rw_mode) {
        /* "r+": open existing for read+write, no truncation */
        fd = sys_open_rw(path);
    } else {
        /* "r": read-only, existing file */
        fd = sys_open(path);
    }
    if (fd < 0) return (FILE *)0;

    FILE *f = pool_alloc();
    if (!f) { sys_close(fd); return (FILE *)0; }

    f->fd         = fd;
    f->flags      = 0;
    f->unget_char = -1;
    if (write_mode || rw_mode)  f->flags |= _FILE_WRITE;
    if (append_mode) {
        f->flags |= _FILE_APPEND | _FILE_WRITE;
        sys_lseek(fd, 0, SEEK_END);   /* position at end of file */
    }
    return f;
}

int fclose(FILE *f)
{
    if (!f || f == stdin || f == stdout || f == stderr) return -1;
    int r = sys_close(f->fd);
    pool_free(f);
    return r;
}

/* ── read / write ────────────────────────────────────────────────────────── */

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *f)
{
    if (!f || (f->flags & _FILE_EOF) || !size || !nmemb) return 0;

    /* Handle pushed-back character first */
    unsigned char *dst = (unsigned char *)ptr;
    size_t total = size * nmemb;
    size_t done  = 0;

    if (f->unget_char >= 0 && total > 0) {
        dst[done++] = (unsigned char)f->unget_char;
        f->unget_char = -1;
    }

    /* Loop until we fill the buffer or hit true EOF.
       sys_read may return partial results at FAT32 cluster boundaries. */
    while (done < total) {
        long n = sys_read(f->fd, dst + done, total - done);
        if (n <= 0) {
            f->flags |= _FILE_EOF;
            break;
        }
        done += (size_t)n;
    }

    return done / size;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *f)
{
    if (!f || !size || !nmemb) return 0;
    int fd = (f == stdout || f == stderr) ? f->fd : f->fd;
    long n = sys_write(fd, ptr, size * nmemb);
    if (n < 0) { f->flags |= _FILE_ERR; return 0; }
    return (size_t)n / size;
}

/* ── seek / tell ─────────────────────────────────────────────────────────── */

int fseek(FILE *f, long offset, int whence)
{
    if (!f || f == stdin) return -1;
    if (f == stdout || f == stderr) return -1;
    f->unget_char = -1;  /* invalidate unget buffer on seek */
    long r = sys_lseek(f->fd, offset, whence);
    if (r < 0) { f->flags |= _FILE_ERR; return -1; }
    f->flags &= ~_FILE_EOF;
    return 0;
}

long ftell(FILE *f)
{
    if (!f || f == stdin || f == stdout || f == stderr) return -1L;
    return sys_lseek(f->fd, 0L, SEEK_CUR);
}

void rewind(FILE *f)
{
    if (f) { fseek(f, 0L, SEEK_SET); f->flags &= ~(_FILE_EOF | _FILE_ERR); }
}

/* ── status ──────────────────────────────────────────────────────────────── */

int feof  (FILE *f) { return f && (f->flags & _FILE_EOF) ? 1 : 0; }
int ferror(FILE *f) { return f && (f->flags & _FILE_ERR) ? 1 : 0; }
void clearerr(FILE *f) { if (f) f->flags &= ~(_FILE_EOF | _FILE_ERR); }

/* ── character I/O ───────────────────────────────────────────────────────── */

int fputc(int c, FILE *f)
{
    if (!f) return -1;
    char ch = (char)c;
    long n = sys_write(f->fd, &ch, 1);
    if (n < 0) { f->flags |= _FILE_ERR; return -1; }
    return (unsigned char)c;
}

int fgetc(FILE *f)
{
    if (!f) return -1;
    if (f->unget_char >= 0) {
        int c = f->unget_char;
        f->unget_char = -1;
        return c;
    }
    if (f->flags & _FILE_EOF) return -1;
    if (f == stdin) return -1;  /* no real stdin */
    unsigned char c;
    long n = sys_read(f->fd, &c, 1);
    if (n <= 0) { f->flags |= _FILE_EOF; return -1; }
    return (int)c;
}

int ungetc(int c, FILE *f)
{
    if (!f || c == -1) return -1;
    f->unget_char = (unsigned char)c;
    f->flags &= ~_FILE_EOF;
    return (unsigned char)c;
}

char *fgets(char *s, int n, FILE *f)
{
    if (!s || n <= 0 || !f) return (char *)0;
    int i = 0;
    while (i < n - 1) {
        int c = fgetc(f);
        if (c < 0) break;
        s[i++] = (char)c;
        if (c == '\n') break;
    }
    if (i == 0) return (char *)0;
    s[i] = '\0';
    return s;
}

int fputs(const char *s, FILE *f)
{
    if (!s || !f) return -1;
    size_t len = strlen(s);
    long n = sys_write(f->fd, s, len);
    return (n < 0) ? -1 : (int)n;
}

/* ── formatted output ────────────────────────────────────────────────────── */

int vfprintf(FILE *f, const char *fmt, va_list ap)
{
    char buf[1024];
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    if (n > (int)(sizeof(buf) - 1)) n = (int)(sizeof(buf) - 1);
    sys_write(f->fd, buf, (size_t)n);
    return n;
}

int fprintf(FILE *f, const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    int r = vfprintf(f, fmt, ap);
    va_end(ap); return r;
}

/* ── fscanf / vfscanf ────────────────────────────────────────────────────── */

int vfscanf(FILE *f, const char *fmt, va_list ap)
{
    if (!f || !fmt) return EOF;
    const char *p = fmt;
    int count = 0;

    while (*p) {
        /* Format whitespace: skip any run of whitespace in input */
        if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
            int c;
            do { c = fgetc(f); } while (c == ' ' || c == '\t' || c == '\n' || c == '\r');
            if (c != EOF) ungetc(c, f);
            p++;
            continue;
        }

        if (*p == '%') {
            p++;
            /* Optional '*' (suppress assignment) */
            int suppress = 0;
            if (*p == '*') { suppress = 1; p++; }

            /* Optional width */
            int width = 0;
            while (*p >= '0' && *p <= '9') { width = width * 10 + (*p - '0'); p++; }

            /* Scanset: %[...] or %[^...] */
            if (*p == '[') {
                p++;
                int negate = 0;
                if (*p == '^') { negate = 1; p++; }
                /* Build scanset bitmap (256 bits) */
                unsigned char set[32];
                for (int i = 0; i < 32; i++) set[i] = 0;
                /* ']' as first char (or after '^') is literal */
                if (*p == ']') { set[(unsigned char)']' / 8] |= (1 << (']' % 8)); p++; }
                while (*p && *p != ']') {
                    unsigned char ch = (unsigned char)*p;
                    set[ch / 8] |= (1 << (ch % 8));
                    p++;
                }
                if (*p == ']') p++;
                /* Read matching chars */
                char *s = suppress ? (char *)0 : va_arg(ap, char *);
                int i = 0;
                int max = width > 0 ? width : 0x7FFFFFFF;
                int c = fgetc(f);
                while (c != EOF && i < max) {
                    int in_set = (set[(unsigned char)c / 8] >> (c % 8)) & 1;
                    if (negate ? in_set : !in_set) break;
                    if (s) s[i] = (char)c;
                    i++;
                    c = fgetc(f);
                }
                if (c != EOF) ungetc(c, f);
                if (i == 0) return count ? count : EOF;
                if (s) { s[i] = '\0'; count++; }
                continue;
            }

            if (*p == 'd' || *p == 'i') {
                /* Read decimal integer */
                int c;
                do { c = fgetc(f); } while (c == ' ' || c == '\t' || c == '\n' || c == '\r');
                if (c == EOF) return count ? count : EOF;
                int neg = 0;
                if (c == '-') { neg = 1; c = fgetc(f); }
                else if (c == '+') c = fgetc(f);
                if (c < '0' || c > '9') { ungetc(c, f); return count ? count : EOF; }
                int n = 0;
                while (c >= '0' && c <= '9') { n = n * 10 + (c - '0'); c = fgetc(f); }
                if (c != EOF) ungetc(c, f);
                if (!suppress) { *va_arg(ap, int *) = neg ? -n : n; count++; }
            } else if (*p == 'u') {
                int c;
                do { c = fgetc(f); } while (c == ' ' || c == '\t' || c == '\n' || c == '\r');
                if (c == EOF) return count ? count : EOF;
                unsigned int n = 0;
                while (c >= '0' && c <= '9') { n = n * 10 + (unsigned)(c - '0'); c = fgetc(f); }
                if (c != EOF) ungetc(c, f);
                if (!suppress) { *va_arg(ap, unsigned int *) = n; count++; }
            } else if (*p == 'f' || *p == 'g' || *p == 'e') {
                /* Read float */
                char buf[48]; int i = 0;
                int c;
                do { c = fgetc(f); } while (c == ' ' || c == '\t' || c == '\n' || c == '\r');
                if (c == EOF) return count ? count : EOF;
                if ((c == '-' || c == '+') && i < 46) { buf[i++] = c; c = fgetc(f); }
                while (((c >= '0' && c <= '9') || c == '.') && i < 46) { buf[i++] = c; c = fgetc(f); }
                if ((c == 'e' || c == 'E') && i < 45) {
                    buf[i++] = c; c = fgetc(f);
                    if ((c == '+' || c == '-') && i < 45) { buf[i++] = c; c = fgetc(f); }
                    while (c >= '0' && c <= '9' && i < 46) { buf[i++] = c; c = fgetc(f); }
                }
                if (c != EOF) ungetc(c, f);
                buf[i] = '\0';
                if (!suppress) { *va_arg(ap, float *) = (float)strtod(buf, (char **)0); count++; }
            } else if (*p == 's') {
                /* Read whitespace-delimited string */
                int c;
                do { c = fgetc(f); } while (c == ' ' || c == '\t' || c == '\n' || c == '\r');
                if (c == EOF) return count ? count : EOF;
                char *s = suppress ? (char *)0 : va_arg(ap, char *);
                int i = 0;
                while (c != EOF && c != ' ' && c != '\t' && c != '\n' && c != '\r') {
                    if (s) s[i] = c;
                    i++; c = fgetc(f);
                }
                if (c != EOF) ungetc(c, f);
                if (s) { s[i] = '\0'; count++; }
            } else if (*p == 'c') {
                int c = fgetc(f);
                if (c == EOF) return count ? count : EOF;
                if (!suppress) { *va_arg(ap, char *) = (char)c; count++; }
            }
            p++;
            continue;
        }

        /* Literal character match */
        int c = fgetc(f);
        if (c == EOF) return count ? count : EOF;
        if (c != (unsigned char)*p) { ungetc(c, f); return count; }
        p++;
    }
    return count;
}

int fscanf(FILE *f, const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    int r = vfscanf(f, fmt, ap);
    va_end(ap); return r;
}

/* ── POSIX stubs ─────────────────────────────────────────────────────────── */

/* Unbuffered I/O — fflush is a no-op. */
int fflush(FILE *f) { (void)f; return 0; }

int remove(const char *path) { return (int)sys_remove(path); }
int rename(const char *old, const char *new_name) { (void)old; (void)new_name; return -1; }
