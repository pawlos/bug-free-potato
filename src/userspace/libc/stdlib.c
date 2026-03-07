#include "stdlib.h"
#include "string.h"
#include "syscall.h"
#include "time.h"
#include <stdarg.h>

/* ── heap allocator ──────────────────────────────────────────────────────── *
 * Free-list allocator backed by sys_mmap.  Each allocation is preceded by a
 * block_hdr.  On free, adjacent free blocks in the list are coalesced.
 * sys_mmap/sys_munmap calls always request HEAP_CHUNK-sized slabs.
 * ─────────────────────────────────────────────────────────────────────── */

#define HEAP_ALIGN  16UL
#define HEAP_CHUNK  (64UL * 1024UL)    /* 64 KB per slab (keep kernel heap pressure low) */
#define ALIGN_UP(n,a) (((n) + (a) - 1UL) & ~((a) - 1UL))

typedef struct block_hdr {
    size_t           size;  /* usable bytes after this header */
    int              used;
    struct block_hdr *next;
} block_hdr;

#define HDR_SIZE  ALIGN_UP(sizeof(block_hdr), HEAP_ALIGN)

static block_hdr *heap_list = (block_hdr *)0;

/* Track raw slab pointers and sizes so exit() can return them to the kernel. */
#define MAX_SLABS 512
static void  *slab_ptrs[MAX_SLABS];
static size_t slab_sizes[MAX_SLABS];
static int    slab_count = 0;

static block_hdr *heap_grow(size_t need)
{
    size_t chunk = HEAP_CHUNK;
    if (need + HDR_SIZE > chunk)
        chunk = ALIGN_UP(need + HDR_SIZE, HEAP_CHUNK);
    void *raw = sys_mmap(chunk);
    if (!raw || (long)raw == -1L) return (block_hdr *)0;
    /* Record slab for cleanup on exit(). */
    if (slab_count < MAX_SLABS) {
        slab_ptrs[slab_count] = raw;
        slab_sizes[slab_count] = chunk;
        slab_count++;
    }
    block_hdr *b = (block_hdr *)raw;
    b->size = chunk - HDR_SIZE;
    b->used = 0;
    b->next = heap_list;
    heap_list = b;
    return b;
}

void *malloc(size_t size)
{
    if (!size) return (void *)0;
    size = ALIGN_UP(size, HEAP_ALIGN);
    for (block_hdr *b = heap_list; b; b = b->next) {
        if (!b->used && b->size >= size) {
            if (b->size >= size + HDR_SIZE + HEAP_ALIGN) {
                block_hdr *nb = (block_hdr *)((char *)b + HDR_SIZE + size);
                nb->size = b->size - size - HDR_SIZE;
                nb->used = 0;
                nb->next = b->next;
                b->size  = size;
                b->next  = nb;
            }
            b->used = 1;
            return (char *)b + HDR_SIZE;
        }
    }
    if (!heap_grow(size)) return (void *)0;
    return malloc(size);
}

void free(void *ptr)
{
    if (!ptr) return;
    block_hdr *b = (block_hdr *)((char *)ptr - HDR_SIZE);
    b->used = 0;
    /* Coalesce adjacent free blocks */
    while (b->next &&
           (char *)b->next == (char *)b + HDR_SIZE + b->size &&
           !b->next->used) {
        b->size += HDR_SIZE + b->next->size;
        b->next  = b->next->next;
    }
}

void *realloc(void *ptr, size_t size)
{
    if (!ptr)  return malloc(size);
    if (!size) { free(ptr); return (void *)0; }
    block_hdr *b = (block_hdr *)((char *)ptr - HDR_SIZE);
    if (size <= b->size) return ptr;
    void *np = malloc(size);
    if (!np) return (void *)0;
    memcpy(np, ptr, b->size);
    free(ptr);
    return np;
}

void *calloc(size_t nmemb, size_t size)
{
    size_t total = nmemb * size;
    void *p = malloc(total);
    if (p) memset(p, 0, total);
    return p;
}

/* ── exit ────────────────────────────────────────────────────────────────── */

#define ATEXIT_MAX 32
static void (*atexit_funcs[ATEXIT_MAX])(void);
static int   atexit_count = 0;

int atexit(void (*func)(void))
{
    if (atexit_count >= ATEXIT_MAX) return -1;
    atexit_funcs[atexit_count++] = func;
    return 0;
}

void exit(int code)
{
    for (int i = atexit_count - 1; i >= 0; i--)
        if (atexit_funcs[i]) atexit_funcs[i]();
    /* Return all heap slabs to the kernel so successive execs don't
       exhaust vmm.kmalloc.  Must happen before sys_exit kills the task. */
    for (int i = 0; i < slab_count; i++)
        sys_munmap(slab_ptrs[i], slab_sizes[i]);
    slab_count = 0;
    sys_exit(code);
    __builtin_unreachable();
}

/* ── integer conversion ──────────────────────────────────────────────────── */

char *utoa(unsigned long val, char *buf, int base)
{
    static const char digits[] = "0123456789abcdef";
    char tmp[65];
    int  i = 0;
    if (val == 0) { buf[0] = '0'; buf[1] = '\0'; return buf; }
    while (val) { tmp[i++] = digits[val % (unsigned)base]; val /= (unsigned)base; }
    int j = 0;
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = '\0';
    return buf;
}

char *itoa(long val, char *buf, int base)
{
    if (base == 10 && val < 0) {
        buf[0] = '-';
        utoa((unsigned long)-val, buf + 1, base);
        return buf;
    }
    return utoa((unsigned long)val, buf, base);
}

int atoi(const char *s)
{
    int sign = 1, n = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') n = n * 10 + (*s++ - '0');
    return sign * n;
}

long atol(const char *s)
{
    long sign = 1, n = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') n = n * 10 + (*s++ - '0');
    return sign * n;
}

long strtol(const char *s, char **endptr, int base)
{
    while (*s == ' ' || *s == '\t') s++;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;
    if ((base == 0 || base == 16) && s[0] == '0' &&
        (s[1] == 'x' || s[1] == 'X')) { s += 2; base = 16; }
    else if (base == 0 && s[0] == '0') { s++; base = 8; }
    else if (base == 0) base = 10;
    long n = 0;
    const char *start = s;
    while (*s) {
        int d;
        if (*s >= '0' && *s <= '9')      d = *s - '0';
        else if (*s >= 'a' && *s <= 'z') d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'Z') d = *s - 'A' + 10;
        else break;
        if (d >= base) break;
        n = n * base + d;
        s++;
    }
    if (s == start) s = (const char *)start; /* no digits consumed */
    if (endptr) *endptr = (char *)s;
    return neg ? -n : n;
}

unsigned long strtoul(const char *s, char **endptr, int base)
{
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '+') s++;
    if ((base == 0 || base == 16) && s[0] == '0' &&
        (s[1] == 'x' || s[1] == 'X')) { s += 2; base = 16; }
    else if (base == 0 && s[0] == '0') { s++; base = 8; }
    else if (base == 0) base = 10;
    unsigned long n = 0;
    while (*s) {
        int d;
        if (*s >= '0' && *s <= '9')      d = *s - '0';
        else if (*s >= 'a' && *s <= 'z') d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'Z') d = *s - 'A' + 10;
        else break;
        if (d >= base) break;
        n = n * (unsigned long)base + (unsigned long)d;
        s++;
    }
    if (endptr) *endptr = (char *)s;
    return n;
}

/* ── misc ────────────────────────────────────────────────────────────────── */

int abs(int x)  { return x < 0 ? -x : x; }
long labs(long x) { return x < 0 ? -x : x; }

char **environ = (char **)0;

char *getenv(const char *name)
{
    if (!environ || !name) return (char *)0;
    int nlen = 0;
    while (name[nlen]) nlen++;
    for (char **ep = environ; *ep; ep++) {
        const char *e = *ep;
        int i = 0;
        while (i < nlen && e[i] == name[i]) i++;
        if (i == nlen && e[i] == '=')
            return (char *)(e + i + 1);
    }
    return (char *)0;
}

static unsigned long rand_state = 12345UL;
int rand(void)
{
    rand_state = rand_state * 6364136223846793005ULL + 1442695040888963407ULL;
    return (int)((rand_state >> 33) & 0x7fffffff);
}
void srand(unsigned int seed) { rand_state = seed; }

/* ── time ─────────────────────────────────────────────────────────────────── */

/* errno stub (declared in errno.h; Doom links against it) */
int errno = 0;

time_t time(time_t *t)
{
    /* Convert 50 Hz ticks to seconds. */
    time_t sec = (time_t)(sys_get_ticks() / 50L);
    if (t) *t = sec;
    return sec;
}

clock_t clock(void)
{
    /* Returns microseconds; CLOCKS_PER_SEC = 1000000 */
    return (clock_t)sys_get_micros();
}

int gettimeofday(struct timeval *tv, struct timezone *tz)
{
    (void)tz;
    if (tv) {
        long long us = (long long)sys_get_micros();
        tv->tv_sec  = (time_t)(us / 1000000LL);
        tv->tv_usec = (suseconds_t)(us % 1000000LL);
    }
    return 0;
}

/* Minimal localtime stub -- returns a static struct with crude fields. */
struct tm *localtime(const time_t *timep)
{
    static struct tm result;
    memset(&result, 0, sizeof(result));
    if (timep) {
        time_t t = *timep;
        result.tm_sec  = (int)(t % 60);
        result.tm_min  = (int)((t / 60) % 60);
        result.tm_hour = (int)((t / 3600) % 24);
        result.tm_mday = 1;
    }
    return &result;
}

double strtod(const char *s, char **endptr)
{
    const char *p = s;
    while (*p == ' ' || *p == '\t') p++;
    int neg = 0;
    if (*p == '-') { neg = 1; p++; } else if (*p == '+') p++;

    double val = 0.0;
    while (*p >= '0' && *p <= '9') val = val * 10.0 + (*p++ - '0');
    if (*p == '.') {
        p++;
        double f = 0.1;
        while (*p >= '0' && *p <= '9') { val += (*p++ - '0') * f; f *= 0.1; }
    }

    /* Optional exponent: e / E */
    if (*p == 'e' || *p == 'E') {
        p++;
        int eneg = 0;
        if (*p == '-') { eneg = 1; p++; } else if (*p == '+') p++;
        int exp = 0;
        while (*p >= '0' && *p <= '9') exp = exp * 10 + (*p++ - '0');
        double base = 10.0;
        while (exp > 0) {
            if (exp & 1) val = eneg ? val / base : val * base;
            base *= base;
            exp >>= 1;
        }
    }

    if (endptr) *endptr = (char *)p;
    return neg ? -val : val;
}

float strtof(const char *s, char **endptr)
{
    return (float)strtod(s, endptr);
}

double atof(const char *s)
{
    return strtod(s, (char **)0);
}

/* Doom uses system() only for zenity error dialogs — not available. */
int system(const char *cmd) { (void)cmd; return -1; }

int putenv(char *s) { (void)s; return -1; }

/* Non-inline versions for code that doesn't include ctype.h */
int toupper(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }
int tolower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

/* Doom uses mkdir to create save-game directories. Our VFS is read-only. */
int mkdir(const char *path, unsigned int mode) { (void)path; (void)mode; return -1; }

/* ── qsort (iterative quicksort) ─────────────────────────────────────────── */

static void swap_bytes(char *a, char *b, size_t n)
{
    while (n--) { char t = *a; *a++ = *b; *b++ = t; }
}

void qsort(void *base, size_t nmemb, size_t size,
           int (*cmp)(const void *, const void *))
{
    if (nmemb <= 1) return;
    /* Iterative quicksort using an explicit stack of (lo,hi) pairs */
    typedef struct { long lo; long hi; } Range;
    Range stack[64];
    int   top = 0;
    stack[top].lo = 0;
    stack[top].hi = (long)nmemb - 1;
    top++;
    char *b = (char *)base;
    while (top > 0) {
        long lo = stack[--top].lo;
        long hi = stack[top].hi;
        if (lo >= hi) continue;
        /* Median-of-three pivot */
        long mid = lo + (hi - lo) / 2;
        if (cmp(b + mid * size, b + lo  * size) < 0) swap_bytes(b + mid*size, b + lo*size,  size);
        if (cmp(b + hi  * size, b + lo  * size) < 0) swap_bytes(b + hi*size,  b + lo*size,  size);
        if (cmp(b + mid * size, b + hi  * size) < 0) swap_bytes(b + mid*size, b + hi*size,  size);
        /* pivot is now at hi */
        char *piv = b + hi * size;
        long i = lo - 1, j = hi;
        for (;;) {
            while (cmp(b + (++i) * size, piv) < 0) {}
            while (j > lo && cmp(piv, b + (--j) * size) < 0) {}
            if (i >= j) break;
            swap_bytes(b + i*size, b + j*size, size);
        }
        swap_bytes(b + i*size, piv, size);
        if (top + 2 < (int)(sizeof(stack)/sizeof(stack[0]))) {
            stack[top].lo = lo;   stack[top++].hi = i - 1;
            stack[top].lo = i+1;  stack[top++].hi = hi;
        }
    }
}

/* ── sleep ───────────────────────────────────────────────────────────────── */

unsigned int sleep(unsigned int seconds)
{
    if (seconds == 0) return 0;
    sys_sleep_ms((unsigned long)seconds * 1000UL);
    return 0;
}

/* usleep: sleep for at least usec microseconds.
   Sub-millisecond values are rounded up to 1 ms (kernel rounds further to
   1 tick = 20 ms at 50 Hz, which is the finest granularity available). */
int usleep(unsigned int usec)
{
    if (usec == 0) return 0;
    unsigned long ms = ((unsigned long)usec + 999UL) / 1000UL;  /* round up to whole ms */
    sys_sleep_ms(ms);
    return 0;
}
