#pragma once
#include "../syscall.h"

#ifndef S_IFDIR
#define S_IFDIR 0040000
#endif
#ifndef S_IFREG
#define S_IFREG 0100000
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
