#pragma once

/* Minimal signal stub — enough for Quake/Doom to compile.
   No signals are actually delivered; signal() is a no-op. */

typedef void (*sighandler_t)(int);

#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)
#define SIG_ERR ((sighandler_t)-1)

#define SIGHUP    1
#define SIGINT    2
#define SIGQUIT   3
#define SIGILL    4
#define SIGTRAP   5
#define SIGABRT   6
#define SIGBUS    7
#define SIGFPE    8
#define SIGKILL   9
#define SIGUSR1   10
#define SIGSEGV   11
#define SIGUSR2   12
#define SIGPIPE   13
#define SIGALRM   14
#define SIGTERM   15

/* Register a signal handler — stub, does nothing and returns SIG_DFL. */
static inline sighandler_t signal(int sig, sighandler_t handler)
{
    (void)sig; (void)handler;
    return SIG_DFL;
}

/* Raise a signal — stub, does nothing. */
static inline int raise(int sig)
{
    (void)sig;
    return 0;
}
