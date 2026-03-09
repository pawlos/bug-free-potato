#pragma once
#include "syscall.h"

#define O_RDONLY  0
#define O_WRONLY  1
#define O_RDWR    2
#define O_CREAT   0x40
#define O_TRUNC   0x200
#define O_BINARY  0
#define O_APPEND  0x400
#define FNDELAY   0x800
#define F_GETFL   3
#define F_SETFL   4

static inline int fcntl(int fd, int cmd, ...) { (void)fd; (void)cmd; return 0; }

/* Real open() implementation using potatOS syscalls */
static inline int open(const char *path, int flags, ...)
{
    if (flags & (O_WRONLY | O_CREAT))
        return (int)sys_create(path);
    return (int)sys_open(path);
}
