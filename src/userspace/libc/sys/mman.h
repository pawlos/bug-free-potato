#pragma once
#include "../syscall.h"

/* PROT_* flags already defined in syscall.h */

#define MAP_FAILED ((void *)-1)

static inline void *mmap(void *addr, size_t length, int prot, int flags,
                         int fd, long offset) {
    (void)addr; (void)prot; (void)flags; (void)fd; (void)offset;
    long va = syscall1(SYS_MMAP, (long)length);
    return (va == -1) ? MAP_FAILED : (void *)va;
}

static inline int munmap(void *addr, size_t length) {
    return (int)syscall2(SYS_MUNMAP, (long)addr, (long)length);
}

static inline int mprotect(void *addr, size_t len, int prot) {
    return (int)syscall3(SYS_MPROTECT, (long)addr, (long)len, (long)prot);
}
