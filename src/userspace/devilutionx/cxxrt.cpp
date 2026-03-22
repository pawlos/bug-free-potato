/*
 * cxxrt.cpp — Minimal C++ runtime for freestanding DevilutionX build.
 * Provides operator new/delete, __cxa_* stubs, and other C++ ABI symbols.
 */
#include <cstddef>

extern "C" {

/* malloc/free from our libc */
void *malloc(size_t size);
void  free(void *ptr);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
void  abort(void);

} // extern "C"

/* operator new/delete — route to malloc/free */
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
void __cxa_pure_virtual() { abort(); }

/* GCC builtins that may not be inlined */
int __popcountdi2(unsigned long long a) {
    int c = 0;
    while (a) { c += a & 1; a >>= 1; }
    return c;
}

} // extern "C"
