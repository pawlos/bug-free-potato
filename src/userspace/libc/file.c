#include "file.h"
#include "string.h"
#include "stdlib.h"
#include "syscall.h"
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
   Keep this low — the kernel allows MAX_FDS=8 per task. */
#define FOPEN_MAX_USER 5

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
    if (mode) {
        for (const char *m = mode; *m; m++) {
            if (*m == 'w') write_mode  = 1;
            if (*m == 'a') append_mode = 1;
        }
    }

    int fd;
    if (write_mode) {
        /* "w": create new or truncate existing */
        fd = sys_create(path);
    } else if (append_mode) {
        /* "a": open existing (keeping content) or create fresh */
        fd = sys_open(path);
        if (fd < 0) fd = sys_create(path);
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
    if (write_mode)  f->flags |= _FILE_WRITE;
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

    if (done < total) {
        long n = sys_read(f->fd, dst + done, total - done);
        if (n <= 0) {
            f->flags |= _FILE_EOF;
        } else {
            done += (size_t)n;
        }
    }

    if (done < total) f->flags |= _FILE_EOF;
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

/* ── POSIX stubs ─────────────────────────────────────────────────────────── */

/* Unbuffered I/O — fflush is a no-op. */
int fflush(FILE *f) { (void)f; return 0; }

/* VFS is read-only; save-game operations fail gracefully. */
int remove(const char *path) { (void)path; return -1; }
int rename(const char *old, const char *new_name) { (void)old; (void)new_name; return -1; }
