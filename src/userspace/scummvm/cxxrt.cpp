/*
 * cxxrt.cpp -- Minimal C++ runtime for freestanding ScummVM build.
 * Provides operator new/delete, __cxa_* stubs, __dso_handle, and
 * unwind/popcount fallbacks. Mirrors the wolf3d/devilutionx pattern.
 *
 * libstdc++.a is linked alongside to satisfy the rest of the C++
 * runtime references.
 */
#include <cstddef>
#include "syscall.h"  /* sys_exit is a static inline here (C++ linkage) */

extern "C" {
void *malloc(size_t size);
void  free(void *ptr);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
}

extern "C" void _exit(int code) { sys_exit(code); __builtin_unreachable(); }

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
unsigned long       _Unwind_GetDataRelBase(struct _Unwind_Context *c)  { (void)c; return 0; }
unsigned long       _Unwind_GetTextRelBase(struct _Unwind_Context *c)  { (void)c; return 0; }
void                _Unwind_SetGR(struct _Unwind_Context *c, int reg, unsigned long val) { (void)c; (void)reg; (void)val; }
void                _Unwind_SetIP(struct _Unwind_Context *c, unsigned long val) { (void)c; (void)val; }

int __popcountdi2(unsigned long long a) {
    int c = 0;
    while (a) { c += a & 1; a >>= 1; }
    return c;
}

}
