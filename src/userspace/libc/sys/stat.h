#pragma once
#include "../syscall.h"

#ifndef S_IFDIR
#define S_IFDIR 0040000
#endif
#ifndef S_IFREG
#define S_IFREG 0100000
#endif
#ifndef S_IREAD
#define S_IREAD 0400
#endif
#ifndef S_IWRITE
#define S_IWRITE 0200
#endif
#ifndef S_IRUSR
#define S_IRUSR 0400
#endif
#ifndef S_IWUSR
#define S_IWUSR 0200
#endif
#ifndef S_IRGRP
#define S_IRGRP 0040
#endif
#ifndef S_IWGRP
#define S_IWGRP 0020
#endif
#ifndef S_IROTH
#define S_IROTH 0004
#endif

/* Q2-compatible struct stat (minimal) */
struct stat {
    unsigned int st_mode;
    long         st_mtime;
    unsigned int st_size;
};

/* Stub: always return -1 (file not found) -- Q2 uses this only for
   CompareAttributes in Sys_FindFirst, which we handle differently. */
static inline int stat(const char *path, struct stat *buf)
{
    (void)path;
    if (buf) {
        buf->st_mode  = S_IFREG;
        buf->st_mtime = 0;
        buf->st_size  = 0;
    }
    return -1;
}

/* fstat: get file size via SYS_STAT if available, else stub */
static inline int fstat(int fd, struct stat *buf)
{
    (void)fd;
    if (buf) {
        buf->st_mode  = S_IFREG;
        buf->st_mtime = 0;
        buf->st_size  = 0;
    }
    return 0;
}

