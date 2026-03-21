#ifndef SDL_thread_h_
#define SDL_thread_h_

#include "SDL_stdinc.h"

/* potatOS is single-threaded — thread API is stubbed. */

typedef struct SDL_Thread SDL_Thread;
typedef int (*SDL_ThreadFunction)(void *data);

static inline SDL_Thread* SDL_CreateThread(SDL_ThreadFunction fn, const char *name, void *data)
    { (void)fn; (void)name; (void)data; return (SDL_Thread*)0; }
static inline void SDL_WaitThread(SDL_Thread *t, int *status)
    { (void)t; if (status) *status = 0; }
static inline void SDL_DetachThread(SDL_Thread *t) { (void)t; }

typedef unsigned long SDL_threadID;
static inline SDL_threadID SDL_ThreadID(void) { return 1; }
static inline SDL_threadID SDL_GetThreadID(SDL_Thread *t) { (void)t; return 1; }

#endif
