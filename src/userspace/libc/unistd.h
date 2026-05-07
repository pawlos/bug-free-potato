#pragma once
#define _UNISTD_H 1
#include "syscall.h"

#ifdef __cplusplus
extern "C" {
#endif


/* access() mode flags */
#define F_OK 0
#define R_OK 4
#define W_OK 2
#define X_OK 1
static inline int access(const char *path, int mode) {
    (void)mode;
    int fd = sys_open(path);
    if (fd >= 0) { sys_close(fd); return 0; }
    /* Directories can't be opened with sys_open; probe via readdir_ex.
       Returns 1 only if the path resolves to a non-empty directory — empty
       directories will read as nonexistent, but ScummVM enumerates dirs that
       contain game data so this is fine in practice. */
    char nm[256];
    unsigned int sz = 0;
    unsigned char ty = 0;
    if (sys_readdir_ex(0, nm, &sz, path, &ty) == 1) return 0;
    return -1;
}

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

/* POSIX fd-level I/O wrappers over potatOS syscalls */
static inline ssize_t read(int fd, void *buf, size_t count) {
    return (ssize_t)sys_read(fd, buf, count);
}
static inline ssize_t write(int fd, const void *buf, size_t count) {
    return (ssize_t)sys_write(fd, buf, count);
}
static inline int close(int fd) {
    return (int)sys_close(fd);
}
static inline long lseek(int fd, long offset, int whence) {
    return (long)sys_lseek(fd, offset, whence);
}
static inline void _exit(int code) { sys_exit(code); __builtin_unreachable(); }
static inline int truncate(const char *path, long length) { (void)path; (void)length; return -1; }
static inline int ftruncate(int fd, long length) { (void)fd; (void)length; return -1; }
static inline pid_t getpid(void) { return (pid_t)sys_getpid(); }
static inline pid_t getppid(void) { return 1; }
static inline int unlink(const char *path) { return sys_remove(path); }
/* fork/exec stubs — potatOS userspace doesn't implement variadic execlp,
 * and ScummVM only uses these to spawn xdg-open for logfile viewing.
 * Returning -1 makes the caller treat the spawn as failed and skip it. */
static inline pid_t fork(void) { return -1; }
static inline int execlp(const char *file, const char *arg, ...) { (void)file; (void)arg; return -1; }
static inline int execvp(const char *file, char *const argv[]) { (void)file; (void)argv; return -1; }
static inline pid_t waitpid(pid_t pid, int *wstatus, int options)
    { (void)wstatus; (void)options; return (pid_t)sys_waitpid((int)pid, 0); }
#define WEXITSTATUS(s) ((s) & 0xff)
#define WIFEXITED(s)   1

#ifdef __cplusplus
}
#endif
