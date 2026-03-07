#pragma once
#define _UNISTD_H 1
#include "syscall.h"

static inline int isatty(int fd) { (void)fd; return 0; }
static inline char *getcwd(char *buf, size_t size) {
    if (buf && size > 1) { buf[0] = '/'; buf[1] = '\0'; }
    return buf;
}
static inline int chdir(const char *path) { (void)path; return -1; }
typedef long ssize_t;
typedef int pid_t;
typedef unsigned int uid_t;
typedef unsigned int gid_t;
static inline uid_t getuid(void)  { return 0; }
static inline uid_t geteuid(void) { return 0; }
static inline gid_t getgid(void)  { return 0; }
static inline int setreuid(uid_t r, uid_t e) { (void)r; (void)e; return 0; }
static inline int seteuid(uid_t e) { (void)e; return 0; }
static inline int setegid(gid_t e) { (void)e; return 0; }
