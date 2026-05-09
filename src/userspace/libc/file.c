#include "file.h"
#include "string.h"
#include "stdlib.h"
#include "syscall.h"
#include "stdio.h"
#include <stdarg.h>

/* Define FILE_DEBUG_TRACE to log every fopen attempt to the serial port.
   Useful when porting a new game and you need to see which assets it touches. */
/* #define FILE_DEBUG_TRACE */
#ifdef FILE_DEBUG_TRACE
#define file_trace(...) serial_printf(__VA_ARGS__)
#else
#define file_trace(...) ((void)0)
#endif

/* ── standard stream instances ───────────────────────────────────────────── */

static FILE _stdin_impl  = { -1, _FILE_EOF, -1, 0, 0, {0} };
static FILE _stdout_impl = {  1, _FILE_WRITE, -1, 0, 0, {0} };
static FILE _stderr_impl = {  2, _FILE_WRITE, -1, 0, 0, {0} };

/* ── internal buffered read ──────────────────────────────────────────────── */
/* Reads exactly `n` bytes from `f` into `dst`, using the FILE read-ahead
   buffer to avoid one syscall per byte.  Updates _rbuf_pos/_rbuf_len and
   sets _FILE_EOF on short read.  Returns bytes read. */
static size_t _rbuf_read(FILE *f, unsigned char *dst, size_t n)
{
    size_t done = 0;
    while (done < n) {
        /* serve remaining bytes from the current buffer fill */
        if (f->_rbuf_pos < f->_rbuf_len) {
            size_t avail = (size_t)(f->_rbuf_len - f->_rbuf_pos);
            size_t take  = (n - done < avail) ? (n - done) : avail;
            const unsigned char *src = f->_rbuf + f->_rbuf_pos;
            unsigned char *d = dst + done;
            for (size_t i = 0; i < take; i++) d[i] = src[i];
            f->_rbuf_pos += (int)take;
            done += take;
        } else {
            /* buffer exhausted — refill with a big read */
            long got = sys_read(f->fd, (char *)f->_rbuf, _FILE_RBUF_SIZE);
            if (got <= 0) { f->flags |= _FILE_EOF; break; }
            f->_rbuf_pos = 0;
            f->_rbuf_len = (int)got;
        }
    }
    return done;
}

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
            _file_pool[i]._rbuf_pos = 0;
            _file_pool[i]._rbuf_len = 0;
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
    if (fd < 0) {
        file_trace("[FILE] fopen FAIL: '%s' mode='%s'\n", path ? path : "(null)", mode ? mode : "");
        return (FILE *)0;
    }
#ifdef FILE_DEBUG_TRACE
    /* Skip noisy theme/config spam (.ini/.xml/.ttf/.zip). */
    if (path) {
        const char *p = path;
        while (*p) p++;
        int n = (int)(p - path);
        int is_spam = 0;
        if (n >= 4) {
            const char *e = path + n - 4;
            if ((e[0]=='.' && e[1]=='i' && e[2]=='n' && e[3]=='i') ||
                (e[0]=='.' && e[1]=='x' && e[2]=='m' && e[3]=='l') ||
                (e[0]=='.' && e[1]=='t' && e[2]=='t' && e[3]=='f') ||
                (e[0]=='.' && e[1]=='z' && e[2]=='i' && e[3]=='p'))
                is_spam = 1;
        }
        if (!is_spam)
            file_trace("[FILE] fopen OK: '%s' fd=%d\n", path, fd);
    }
#endif

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

    unsigned char *dst = (unsigned char *)ptr;
    size_t total = size * nmemb;
    size_t done  = 0;

    /* Handle pushed-back character first */
    if (f->unget_char >= 0 && total > 0) {
        dst[done++] = (unsigned char)f->unget_char;
        f->unget_char = -1;
    }

    /* Use the read-ahead buffer (handles both small and large requests) */
    done += _rbuf_read(f, dst + done, total - done);

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
    f->unget_char = -1;  /* invalidate unget buffer */

    /* Convert SEEK_CUR to SEEK_SET to account for read-ahead bytes that
       the fd has already consumed but the caller hasn't seen yet. */
    if (whence == SEEK_CUR) {
        long fd_pos = sys_lseek(f->fd, 0L, SEEK_CUR);
        long buffered = (long)(f->_rbuf_len - f->_rbuf_pos);
        offset = (fd_pos - buffered) + offset;
        whence = SEEK_SET;
    }

    /* Discard the read-ahead buffer — position is changing */
    f->_rbuf_pos = f->_rbuf_len = 0;

    long r = sys_lseek(f->fd, offset, whence);
    if (r < 0) { f->flags |= _FILE_ERR; return -1; }
    f->flags &= ~_FILE_EOF;
    return 0;
}

long ftell(FILE *f)
{
    if (!f || f == stdin || f == stdout || f == stderr) return -1L;
    /* fd is ahead of the user by the unconsumed read-ahead bytes */
    long fd_pos  = sys_lseek(f->fd, 0L, SEEK_CUR);
    long buffered = (long)(f->_rbuf_len - f->_rbuf_pos);
    return fd_pos - buffered;
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
    if (_rbuf_read(f, &c, 1) != 1) return -1;
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
