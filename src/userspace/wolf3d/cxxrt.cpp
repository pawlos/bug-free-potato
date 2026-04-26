/*
 * cxxrt.cpp -- Minimal C++ runtime for freestanding Wolf4SDL build.
 * Provides operator new/delete, __cxa_* stubs, __dso_handle.
 *
 * Mirrors the DevilutionX cxxrt.cpp pattern. libstdc++.a is linked alongside
 * to satisfy the rest of the C++ runtime references.
 */
#include <cstddef>
#include "syscall.h"  /* sys_open, sys_close, sys_read, sys_write, sys_lseek */

extern "C" {
void *malloc(size_t size);
void  free(void *ptr);
}
/* _exit lives in libc/unistd.h as a static inline that can't satisfy
 * external link refs. Provide a real symbol here by calling sys_exit. */
extern "C" void _exit(int code) { sys_exit(code); __builtin_unreachable(); }
extern "C" const char *gettext(const char *msg) { return msg; }

void *operator new(std::size_t size)        { return malloc(size); }
void *operator new[](std::size_t size)      { return malloc(size); }
void  operator delete(void *ptr) noexcept   { free(ptr); }
void  operator delete[](void *ptr) noexcept { free(ptr); }
void  operator delete(void *ptr, std::size_t) noexcept   { free(ptr); }
void  operator delete[](void *ptr, std::size_t) noexcept { free(ptr); }

extern "C" {

void *__dso_handle = nullptr;
int   __libc_single_threaded = 1;

int  __cxa_atexit(void (*)(void *), void *, void *) { return 0; }
int  __cxa_guard_acquire(long long *guard) { return *guard ? 0 : 1; }
void __cxa_guard_release(long long *guard) { *guard = 1; }
void __cxa_guard_abort(long long *) {}
void __cxa_pure_virtual(void) { _exit(99); }
void __cxa_throw_bad_array_new_length(void) { _exit(99); }
void __stack_chk_fail(void) { _exit(99); }

/* Unwind stubs (we compile -fno-exceptions, but libstdc++ remnants reference) */
typedef struct _Unwind_Exception _Unwind_Exception;
typedef enum { _URC_NO_REASON = 0 } _Unwind_Reason_Code;
struct _Unwind_Context;

_Unwind_Reason_Code _Unwind_RaiseException(_Unwind_Exception *e) { (void)e; _exit(99); __builtin_unreachable(); }
void                _Unwind_Resume(_Unwind_Exception *e)         { (void)e; _exit(99); }
_Unwind_Reason_Code _Unwind_Resume_or_Rethrow(_Unwind_Exception *e) { (void)e; _exit(99); __builtin_unreachable(); }
void                _Unwind_DeleteException(_Unwind_Exception *e) { (void)e; }
unsigned long       _Unwind_GetIPInfo(struct _Unwind_Context *c, int *b) { (void)c; if(b)*b=0; return 0; }
unsigned long       _Unwind_GetLanguageSpecificData(struct _Unwind_Context *c) { (void)c; return 0; }
unsigned long       _Unwind_GetRegionStart(struct _Unwind_Context *c) { (void)c; return 0; }
unsigned long       _Unwind_GetDataRelBase(struct _Unwind_Context *c) { (void)c; return 0; }
unsigned long       _Unwind_GetTextRelBase(struct _Unwind_Context *c) { (void)c; return 0; }
void                _Unwind_SetGR(struct _Unwind_Context *c, int r, unsigned long v) { (void)c; (void)r; (void)v; }
void                _Unwind_SetIP(struct _Unwind_Context *c, unsigned long v) { (void)c; (void)v; }

/* libstdc++ random_device / cp-demangle / abort references */
void abort(void)                              { _exit(99); __builtin_unreachable(); }
void *aligned_alloc(size_t a, size_t s)       { (void)a; return malloc(s); }
int   getentropy(void *buf, size_t len)       { unsigned char *p = (unsigned char*)buf;
                                                 for (size_t i=0;i<len;i++) p[i]=(unsigned char)(i*0xA7+0x13);
                                                 return 0; }

extern int vsnprintf(char *, size_t, const char *, __builtin_va_list);
int __sprintf_chk(char *buf, int flag, size_t buflen, const char *fmt, ...)
{
    (void)flag;
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int ret = vsnprintf(buf, buflen, fmt, ap);
    __builtin_va_end(ap);
    return ret;
}

/* Real (extern) wrappers for POSIX I/O so libstdc++.a links cleanly. The
 * libc/unistd.h versions are static-inline and don't satisfy external refs. */
int  open(const char *path, int flags, ...)   { (void)flags; return sys_open(path); }
int  close(int fd)                             { return sys_close(fd); }
long read(int fd, void *buf, size_t n)         { return sys_read(fd, buf, n); }
long write(int fd, const void *buf, size_t n)  { return sys_write(fd, buf, n); }
long writev(int fd, const void *iov, int n)    { (void)fd; (void)iov; (void)n; return -1; }
long lseek64(int fd, long o, int w)            { return sys_lseek(fd, o, w); }
int  ioctl(int fd, unsigned long req, ...)     { (void)fd; (void)req; return -1; }

/* Per-thread errno location used by libstdc++. We have a single global. */
static int g_errno;
int *__errno_location(void) { return &g_errno; }

}
