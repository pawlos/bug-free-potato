/*
 * dvlx_wchar.c -- POSIX/glibc stubs compiled as plain C to avoid conflicts
 * with system wchar.h and locale.h pulled in by C++ <cstddef>/<cstdint>.
 */
typedef unsigned long size_t;
typedef int           wchar_t;
typedef unsigned int  wint_t;

typedef struct { int __state; } mbstate_t;
typedef void *locale_t;
typedef void *iconv_t;

/* ── errno ─────────────────────────────────────────────────────────────── */
static int _dvlx_errno = 0;
int *__errno_location(void) { return &_dvlx_errno; }

/* ── locale stubs ──────────────────────────────────────────────────────── */
locale_t __newlocale(int mask, const char *locale, locale_t base) { (void)mask; (void)locale; (void)base; return (locale_t)1; }
locale_t newlocale(int mask, const char *locale, locale_t base) { return __newlocale(mask, locale, base); }
void __freelocale(locale_t l) { (void)l; }
void freelocale(locale_t l) { (void)l; }
locale_t __uselocale(locale_t l) { (void)l; return (locale_t)1; }
locale_t uselocale(locale_t l) { return __uselocale(l); }
locale_t __duplocale(locale_t l) { (void)l; return (locale_t)1; }
char *setlocale(int cat, const char *locale) { (void)cat; (void)locale; return (char*)"C"; }

/* ── nl_langinfo ───────────────────────────────────────────────────────── */
char *nl_langinfo(int item) { (void)item; return (char*)""; }
char *__nl_langinfo_l(int item, locale_t l) { (void)item; (void)l; return (char*)""; }

/* ── gettext stubs ─────────────────────────────────────────────────────── */
char *gettext(const char *msgid) { return (char*)msgid; }
char *dgettext(const char *domain, const char *msgid) { (void)domain; return (char*)msgid; }
char *bindtextdomain(const char *domain, const char *dir) { (void)domain; (void)dir; return (char*)""; }
char *bind_textdomain_codeset(const char *domain, const char *codeset) { (void)domain; (void)codeset; return (char*)""; }

/* ── iconv stubs ───────────────────────────────────────────────────────── */
iconv_t iconv_open(const char *to, const char *from) { (void)to; (void)from; return (iconv_t)-1; }
size_t iconv(iconv_t cd, char **inbuf, size_t *inleft, char **outbuf, size_t *outleft) { (void)cd; (void)inbuf; (void)inleft; (void)outbuf; (void)outleft; return (size_t)-1; }
int iconv_close(iconv_t cd) { (void)cd; return 0; }

/* ── wide char functions ───────────────────────────────────────────────── */
size_t wcslen(const wchar_t *s) { size_t n=0; while(s && s[n]) n++; return n; }
int wcscmp(const wchar_t *a, const wchar_t *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a - *b;
}
wchar_t *wmemchr(const wchar_t *s, wchar_t c, size_t n) {
    for (size_t i=0; i<n; i++) if (s[i]==c) return (wchar_t*)(s+i);
    return 0;
}
int wmemcmp(const wchar_t *a, const wchar_t *b, size_t n) {
    for (size_t i=0; i<n; i++) { if (a[i]!=b[i]) return a[i]<b[i]?-1:1; }
    return 0;
}
wchar_t *wmemcpy(wchar_t *dst, const wchar_t *src, size_t n) {
    for (size_t i=0; i<n; i++) dst[i]=src[i];
    return dst;
}
wchar_t *wmemmove(wchar_t *dst, const wchar_t *src, size_t n) {
    if (dst < src) { for (size_t i=0; i<n; i++) dst[i]=src[i]; }
    else { for (size_t i=n; i>0; i--) dst[i-1]=src[i-1]; }
    return dst;
}
wchar_t *wmemset(wchar_t *s, wchar_t c, size_t n) {
    for (size_t i=0; i<n; i++) s[i]=c;
    return s;
}

/* ── Fortified wide string functions ───────────────────────────────────── */
wchar_t *__wmemcpy_chk(wchar_t *dst, const wchar_t *src, size_t n, size_t dstlen) { (void)dstlen; return wmemcpy(dst,src,n); }
wchar_t *__wmemmove_chk(wchar_t *dst, const wchar_t *src, size_t n, size_t dstlen) { (void)dstlen; return wmemmove(dst,src,n); }
wchar_t *__wmemset_chk(wchar_t *dst, wchar_t c, size_t n, size_t dstlen) { (void)dstlen; return wmemset(dst,c,n); }

size_t __mbsrtowcs_chk(wchar_t *dst, const char **src, size_t len, mbstate_t *ps, size_t dstlen) {
    (void)ps; (void)dstlen;
    if (!src || !*src) return 0;
    size_t i;
    for (i=0; i<len && (*src)[i]; i++) { if(dst) dst[i]=(wchar_t)(unsigned char)(*src)[i]; }
    if (dst && i<len) dst[i]=0;
    if ((*src)[i]==0) *src=0;
    return i;
}

size_t __mbsnrtowcs_chk(wchar_t *dst, const char **src, size_t nms, size_t len, mbstate_t *ps, size_t dstlen) {
    (void)nms;
    return __mbsrtowcs_chk(dst, src, len, ps, dstlen);
}

/* ── multibyte/wide conversions ────────────────────────────────────────── */
wint_t btowc(int c) { return (c >= 0 && c < 128) ? (wint_t)c : (wint_t)-1; }
int wctob(wint_t c) { return (c < 128) ? (int)c : -1; }

size_t mbrtowc(wchar_t *pwc, const char *s, size_t n, mbstate_t *ps) {
    (void)ps;
    if (!s) return 0;
    if (n == 0) return (size_t)-2;
    if (pwc) *pwc = (wchar_t)(unsigned char)*s;
    return *s ? 1 : 0;
}

size_t wcrtomb(char *s, wchar_t wc, mbstate_t *ps) {
    (void)ps;
    if (!s) return 1;
    *s = (char)(unsigned char)wc;
    return 1;
}

size_t mbsnrtowcs(wchar_t *dst, const char **src, size_t nms, size_t len, mbstate_t *ps) {
    (void)ps;
    if (!src || !*src) return 0;
    size_t max = nms < len ? nms : len;
    size_t i;
    for (i = 0; i < max && (*src)[i]; i++) {
        if (dst) dst[i] = (wchar_t)(unsigned char)(*src)[i];
    }
    if (dst && i < len) dst[i] = 0;
    if (i < nms && (*src)[i] == 0) *src = 0;
    return i;
}

size_t wcsnrtombs(char *dst, const wchar_t **src, size_t nwc, size_t len, mbstate_t *ps) {
    (void)ps;
    if (!src || !*src) return 0;
    size_t max = nwc < len ? nwc : len;
    size_t i;
    for (i = 0; i < max && (*src)[i]; i++) {
        if (dst) dst[i] = (char)(unsigned char)(*src)[i];
    }
    if (dst && i < len) dst[i] = 0;
    if (i < nwc && (*src)[i] == 0) *src = 0;
    return i;
}

size_t __ctype_get_mb_cur_max(void) { return 1; }

/* ── locale-aware string/float stubs ───────────────────────────────────── */
extern int strcmp(const char*, const char*);
extern size_t strlen(const char*);
extern char *strncpy(char*, const char*, size_t);
extern double strtod(const char*, char**);
extern float strtof(const char*, char**);
extern size_t strftime(char*, size_t, const char*, const void*);

int __strcoll_l(const char *a, const char *b, locale_t l) { (void)l; return strcmp(a,b); }
size_t __strxfrm_l(char *dst, const char *src, size_t n, locale_t l) { (void)l; size_t len=strlen(src); if(dst&&n) { strncpy(dst,src,n); dst[n-1]=0; } return len; }
int __wcscoll_l(const wchar_t *a, const wchar_t *b, locale_t l) { (void)l; return wcscmp(a,b); }
size_t __wcsxfrm_l(wchar_t *dst, const wchar_t *src, size_t n, locale_t l) { (void)l; size_t len=wcslen(src); if(dst&&n) { size_t cp=n<len?n:len; wmemcpy(dst,src,cp); if(n>len) dst[len]=0; dst[n-1]=0; } return len; }
size_t __strftime_l(char *s, size_t max, const char *fmt, const void *tm, locale_t l) { (void)l; return strftime(s,max,fmt,tm); }
size_t __wcsftime_l(wchar_t *s, size_t max, const wchar_t *fmt, const void *tm, locale_t l) { (void)s; (void)max; (void)fmt; (void)tm; (void)l; return 0; }
double __strtod_l(const char *s, char **end, locale_t l) { (void)l; return strtod(s,end); }
float __strtof_l(const char *s, char **end, locale_t l) { (void)l; return strtof(s,end); }
long double strtold(const char *s, char **end) { return (long double)strtod(s,end); }
long double strtold_l(const char *s, char **end, locale_t l) { (void)l; return strtold(s,end); }

/* ── towlower/towupper/iswctype ────────────────────────────────────────── */
wint_t __towlower_l(wint_t wc, locale_t l) { (void)l; if(wc>='A'&&wc<='Z') return wc+32; return wc; }
wint_t __towupper_l(wint_t wc, locale_t l) { (void)l; if(wc>='a'&&wc<='z') return wc-32; return wc; }
int __iswctype_l(wint_t wc, unsigned long type, locale_t l) { (void)wc; (void)type; (void)l; return 0; }
unsigned long __wctype_l(const char *name, locale_t l) { (void)name; (void)l; return 0; }

/* ── floating-point rounding ───────────────────────────────────────────── */
int fegetround(void) { return 0; }
int fesetround(int round) { (void)round; return 0; }

/* ── pthread stubs (single-threaded OS) ────────────────────────────────── */
typedef unsigned int pthread_key_t;
typedef unsigned int pthread_once_t;
typedef struct { int dummy; } pthread_mutex_t;
typedef struct { int dummy; } pthread_rwlock_t;

int pthread_key_create(pthread_key_t *key, void (*dtor)(void*)) { (void)dtor; if(key) *key=0; return 0; }
int pthread_key_delete(pthread_key_t key) { (void)key; return 0; }
static void *_tls_val = 0;
void *pthread_getspecific(pthread_key_t key) { (void)key; return _tls_val; }
int pthread_setspecific(pthread_key_t key, const void *val) { (void)key; _tls_val=(void*)val; return 0; }
int pthread_once(pthread_once_t *once, void (*init)(void)) { if(once && !*once) { init(); *once=1; } return 0; }
int pthread_mutex_lock(pthread_mutex_t *m) { (void)m; return 0; }
int pthread_mutex_unlock(pthread_mutex_t *m) { (void)m; return 0; }
int pthread_rwlock_rdlock(pthread_rwlock_t *rw) { (void)rw; return 0; }
int pthread_rwlock_wrlock(pthread_rwlock_t *rw) { (void)rw; return 0; }
int pthread_rwlock_unlock(pthread_rwlock_t *rw) { (void)rw; return 0; }

/* ── POSIX file/dir stubs ──────────────────────────────────────────────── */

/* Raw syscall helper -- inline asm to avoid depending on syscall.h */
static long _sc2(long nr, long a1, long a2) {
    long ret;
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(nr), "D"(a1), "S"(a2)
                     : "memory");
    return ret;
}
static long _sc0(long nr) {
    long ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(nr) : "memory");
    return ret;
}

#define _SYS_STAT  40
#define _SYS_GET_MICROS 33

int stat(const char *path, void *buf) {
    return (int)_sc2(_SYS_STAT, (long)path, (long)buf);
}
int lstat(const char *path, void *buf) { return stat(path, buf); }
int fstat64(int fd, void *buf) { (void)fd; (void)buf; return -1; }
int __openat_2(int dirfd, const char *path, int flags) { (void)dirfd; (void)path; (void)flags; return -1; }
int openat(int dirfd, const char *path, int flags, ...) { (void)dirfd; (void)path; (void)flags; return -1; }
int link(const char *old, const char *new_) { (void)old; (void)new_; return -1; }
int symlink(const char *target, const char *linkpath) { (void)target; (void)linkpath; return -1; }
long readlink(const char *path, char *buf, size_t bufsiz) { (void)path; (void)buf; (void)bufsiz; return -1; }
char *realpath(const char *path, char *resolved) {
    if (!path) return 0;
    if (resolved) { extern char *strcpy(char*, const char*); strcpy(resolved, path); return resolved; }
    extern void *malloc(size_t);
    size_t len = strlen(path)+1;
    char *r = (char*)malloc(len);
    if (r) { extern char *strcpy(char*, const char*); strcpy(r, path); }
    return r;
}
int unlinkat(int dirfd, const char *path, int flags) { (void)dirfd; (void)path; (void)flags; return -1; }
int fchmod(int fd, unsigned int mode) { (void)fd; (void)mode; return 0; }
int fchmodat(int dirfd, const char *path, unsigned int mode, int flags) { (void)dirfd; (void)path; (void)mode; (void)flags; return 0; }
int utimensat(int dirfd, const char *path, const void *times, int flags) { (void)dirfd; (void)path; (void)times; (void)flags; return 0; }
long sendfile(int out_fd, int in_fd, long *offset, size_t count) { (void)out_fd; (void)in_fd; (void)offset; (void)count; return -1; }
int poll(void *fds, unsigned long nfds, int timeout) { (void)fds; (void)nfds; (void)timeout; return 0; }
int ioctl(int fd, unsigned long req, ...) { (void)fd; (void)req; return -1; }
int statvfs(const char *path, void *buf) { (void)path; (void)buf; return -1; }
int getentropy(void *buf, size_t len) {
    unsigned char *p = (unsigned char*)buf;
    static unsigned int seed = 12345;
    for (size_t i=0; i<len; i++) { seed = seed * 1103515245 + 12345; p[i] = (unsigned char)(seed >> 16); }
    return 0;
}
char *secure_getenv(const char *name) { (void)name; return 0; }
void *fdopen(int fd, const char *mode) { (void)fd; (void)mode; return 0; }
void *fdopendir(int fd) { (void)fd; return 0; }
int dirfd(void *dirp) { (void)dirp; return -1; }
void *fopen64(const char *path, const char *mode) {
    extern void *fopen(const char*, const char*);
    return fopen(path, mode);
}

/* clock_gettime */
struct timespec_compat { long tv_sec; long tv_nsec; };
int clock_gettime(int clk_id, struct timespec_compat *tp) {
    (void)clk_id;
    long us = _sc0(_SYS_GET_MICROS);
    if (tp) { tp->tv_sec = us / 1000000; tp->tv_nsec = (us % 1000000) * 1000; }
    return 0;
}

/* fseeko -- used by libmpq */
extern int fseek(void *f, long offset, int whence);
int fseeko(void *f, long offset, int whence) { return fseek(f, offset, whence); }

/* POSIX functions needed by libstdc++.a (filesystem, iostream, etc.)
   These are static inline in our headers but libstdc++.a needs real symbols. */
int chdir(const char *path) { (void)path; return -1; }
char *getcwd(char *buf, size_t size) {
    if (buf && size > 1) { buf[0] = '/'; buf[1] = '\0'; }
    return buf;
}
int setvbuf(void *stream, char *buf, int mode, size_t size) { (void)stream; (void)buf; (void)mode; (void)size; return 0; }
int truncate(const char *path, long length) { (void)path; (void)length; return -1; }

/* _exit -- noreturn */
void _exit(int code) {
    long ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"((long)1), "D"((long)code) : "memory");
    __builtin_unreachable();
}

/* strftime -- weak fallback if not in libc.a */
size_t strftime(char *s, size_t max, const char *fmt, const void *tm) __attribute__((weak));
size_t strftime(char *s, size_t max, const char *fmt, const void *tm) {
    (void)fmt; (void)tm;
    if (s && max > 0) s[0] = 0;
    return 0;
}

/* zlib stubs -- libmpq links against zlib but Diablo MPQs don't use zlib */
int inflateInit_(void *strm, const char *version, int stream_size) { (void)strm; (void)version; (void)stream_size; return -1; }
int inflate(void *strm, int flush) { (void)strm; (void)flush; return -1; }
int inflateEnd(void *strm) { (void)strm; return -1; }

/* bzip2 stubs -- same reasoning */
int BZ2_bzDecompressInit(void *strm, int verbosity, int small) { (void)strm; (void)verbosity; (void)small; return -1; }
int BZ2_bzDecompress(void *strm) { (void)strm; return -1; }
int BZ2_bzDecompressEnd(void *strm) { (void)strm; return -1; }

/* __udivti3 -- 128-bit unsigned division (GCC runtime) */
typedef unsigned long long uint64_t;
typedef struct { uint64_t lo, hi; } uint128_t;
uint128_t __udivti3(uint128_t a, uint128_t b) {
    /* Simplified: only handle cases where hi==0 (common case) */
    if (b.hi == 0 && a.hi == 0) {
        uint128_t r = { a.lo / b.lo, 0 };
        return r;
    }
    /* Fallback: return 0 for complex cases (shouldn't hit in practice) */
    uint128_t r = { 0, 0 };
    return r;
}
