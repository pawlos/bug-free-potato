#pragma once

/* Syscall numbers — must match src/intf/syscall.h in the kernel. */
#define SYS_WRITE     0   /* rdi = null-terminated string ptr              */
#define SYS_EXIT      1   /* rdi = exit code                               */
#define SYS_READ_KEY  2   /* returns char (0-255) or (long)-1 if no key    */
#define SYS_OPEN      3   /* rdi = filename ptr; returns fd or -1          */
#define SYS_READ      4   /* rdi=fd, rsi=buf, rdx=count; returns bytes     */
#define SYS_CLOSE     5   /* rdi = fd; returns 0 or -1                     */
#define SYS_MMAP      6   /* rdi = size; returns virt addr or -1           */
#define SYS_MUNMAP    7   /* rdi = ptr; returns 0 or -1                    */
#define SYS_YIELD     8   /* cooperative yield                             */
#define SYS_GET_TICKS 9   /* returns current tick count                    */
#define SYS_GET_TIME  10  /* returns (hours<<8)|minutes                    */
#define SYS_FILL_RECT 11  /* rdi=x, rsi=y, rdx=w, rcx=h, r8=0xRRGGBB     */
#define SYS_DRAW_TEXT 12  /* rdi=x, rsi=y, rdx=str, rcx=fg, r8=bg         */
#define SYS_FB_WIDTH  13  /* returns framebuffer width in pixels           */

typedef unsigned long size_t;
typedef long          ssize_t;

/* ── raw syscall stubs ─────────────────────────────────────────────────── */

static inline long __sc0(long nr)
{
    long ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(nr) : "memory");
    return ret;
}

static inline long __sc1(long nr, long a1)
{
    long ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(nr), "D"(a1) : "memory");
    return ret;
}

static inline long __sc2(long nr, long a1, long a2)
{
    long ret;
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(nr), "D"(a1), "S"(a2)
                     : "memory");
    return ret;
}

static inline long __sc3(long nr, long a1, long a2, long a3)
{
    long ret;
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(nr), "D"(a1), "S"(a2), "d"(a3)
                     : "memory");
    return ret;
}

static inline long __sc5(long nr, long a1, long a2, long a3, long a4, long a5)
{
    long ret;
    register long _a4 __asm__("rcx") = a4;
    register long _a5 __asm__("r8")  = a5;
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(nr), "D"(a1), "S"(a2), "d"(a3), "r"(_a4), "r"(_a5)
                     : "memory");
    return ret;
}

/* ── high-level wrappers ───────────────────────────────────────────────── */

static inline void  sys_write(const char *s)
    { __sc1(SYS_WRITE, (long)s); }

static inline void  sys_exit(int code)
    { __sc1(SYS_EXIT, code); }

static inline long  sys_read_key(void)
    { return __sc0(SYS_READ_KEY); }

static inline int   sys_open(const char *name)
    { return (int)__sc1(SYS_OPEN, (long)name); }

static inline long  sys_read(int fd, void *buf, size_t n)
    { return __sc3(SYS_READ, fd, (long)buf, (long)n); }

static inline int   sys_close(int fd)
    { return (int)__sc1(SYS_CLOSE, fd); }

static inline void *sys_mmap(size_t size)
    { return (void *)__sc1(SYS_MMAP, (long)size); }

static inline void  sys_munmap(void *ptr)
    { __sc1(SYS_MUNMAP, (long)ptr); }

static inline void  sys_yield(void)
    { __sc0(SYS_YIELD); }

static inline long  sys_get_ticks(void)
    { return __sc0(SYS_GET_TICKS); }

static inline long  sys_get_time(void)
    { return __sc0(SYS_GET_TIME); }

static inline void  sys_fill_rect(long x, long y, long w, long h, long rgb)
    { __sc5(SYS_FILL_RECT, x, y, w, h, rgb); }

static inline void  sys_draw_text(long x, long y, const char *s, long fg, long bg)
    { __sc5(SYS_DRAW_TEXT, x, y, (long)s, fg, bg); }

static inline long  sys_fb_width(void)
    { return __sc0(SYS_FB_WIDTH); }
