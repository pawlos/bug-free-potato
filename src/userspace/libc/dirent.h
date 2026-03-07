#pragma once
#include "syscall.h"  /* size_t */

/* ── d_type constants ───────────────────────────────────────────────────── */
#define DT_UNKNOWN  0
#define DT_DIR      4
#define DT_REG      8

/* ── struct dirent ──────────────────────────────────────────────────────── */
struct dirent {
    unsigned long  d_ino;       /* fake inode (entry index) */
    unsigned long  d_off;       /* not used; 0              */
    unsigned short d_reclen;    /* sizeof(struct dirent)    */
    unsigned char  d_type;      /* DT_REG for all entries   */
    char           d_name[256]; /* NUL-terminated filename  */
};

/* ── DIR handle ─────────────────────────────────────────────────────────── */
typedef struct {
    int            index;    /* next sys_readdir index to fetch */
    struct dirent  ent;      /* storage for the last entry read */
    char           path[128]; /* directory path for readdir_ex   */
} DIR;

/* ── API ─────────────────────────────────────────────────────────────────── */

/* Open a directory stream.  Since the filesystem is flat (single root
   directory), any non-NULL path is accepted and iterates the root.
   Returns NULL on allocation failure. */
DIR *opendir(const char *path);

/* Return the next entry, or NULL at end-of-directory. */
struct dirent *readdir(DIR *dirp);

/* Close the directory stream and free resources. */
int closedir(DIR *dirp);

/* Reset the stream to the beginning. */
void rewinddir(DIR *dirp);
