/*
 * cxxrt.cpp -- Minimal C++ runtime for freestanding DevilutionX build.
 * Provides operator new/delete, __cxa_* stubs, and other C++ ABI symbols.
 */
#include <cstddef>

extern "C" {

/* malloc/free from our libc */
void *malloc(size_t size);
void  free(void *ptr);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
void  _exit(int code);

} // extern "C"

/* operator new/delete -- route to malloc/free */
void *operator new(std::size_t size) { return malloc(size); }
void *operator new[](std::size_t size) { return malloc(size); }
void  operator delete(void *ptr) noexcept { free(ptr); }
void  operator delete[](void *ptr) noexcept { free(ptr); }
void  operator delete(void *ptr, std::size_t) noexcept { free(ptr); }
void  operator delete[](void *ptr, std::size_t) noexcept { free(ptr); }

/* C++ ABI stubs */
extern "C" {

void *__dso_handle = nullptr;
int   __libc_single_threaded = 1;

int __cxa_atexit(void (*)(void *), void *, void *) { return 0; }
int __cxa_guard_acquire(long long *guard) {
    if (*guard) return 0;
    return 1;
}
void __cxa_guard_release(long long *guard) { *guard = 1; }
void __cxa_guard_abort(long long *) {}
void __cxa_pure_virtual() { _exit(99); }
void __cxa_throw_bad_array_new_length() { _exit(99); }

/* Stack protector -- disabled but symbol still referenced */
void __stack_chk_fail(void) { _exit(99); }

/* Unwind stubs -- we compile with -fno-exceptions so these should
   never actually be called, but libstdc++ remnants reference them. */
typedef struct {} _Unwind_Exception;
typedef enum { _URC_NO_REASON = 0 } _Unwind_Reason_Code;
typedef int _Unwind_Action;
struct _Unwind_Context;

_Unwind_Reason_Code _Unwind_RaiseException(_Unwind_Exception *e) { (void)e; _exit(99); __builtin_unreachable(); }
void _Unwind_Resume(_Unwind_Exception *e) { (void)e; _exit(99); }
_Unwind_Reason_Code _Unwind_Resume_or_Rethrow(_Unwind_Exception *e) { (void)e; _exit(99); __builtin_unreachable(); }
void _Unwind_DeleteException(_Unwind_Exception *e) { (void)e; }
unsigned long _Unwind_GetIPInfo(struct _Unwind_Context *c, int *ip_before_insn) { (void)c; if(ip_before_insn) *ip_before_insn=0; return 0; }
unsigned long _Unwind_GetLanguageSpecificData(struct _Unwind_Context *c) { (void)c; return 0; }
unsigned long _Unwind_GetRegionStart(struct _Unwind_Context *c) { (void)c; return 0; }
unsigned long _Unwind_GetDataRelBase(struct _Unwind_Context *c) { (void)c; return 0; }
unsigned long _Unwind_GetTextRelBase(struct _Unwind_Context *c) { (void)c; return 0; }
void _Unwind_SetGR(struct _Unwind_Context *c, int reg, unsigned long val) { (void)c; (void)reg; (void)val; }
void _Unwind_SetIP(struct _Unwind_Context *c, unsigned long val) { (void)c; (void)val; }

/* GCC builtins that may not be inlined */
int __popcountdi2(unsigned long long a) {
    int c = 0;
    while (a) { c += a & 1; a >>= 1; }
    return c;
}

} // extern "C"
