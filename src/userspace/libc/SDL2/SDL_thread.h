#ifndef SDL_thread_h_
#define SDL_thread_h_

#include "SDL_stdinc.h"

#ifdef __cplusplus
extern "C" {
#endif


/* potatOS is single-threaded — run thread functions synchronously. */

#ifndef SDLCALL
#define SDLCALL
#endif

typedef struct SDL_Thread SDL_Thread;
typedef int (*SDL_ThreadFunction)(void *data);

/* Run fn inline; return a non-NULL sentinel so callers see success. */
static inline SDL_Thread* SDL_CreateThread(SDL_ThreadFunction fn, const char *name, void *data)
    { (void)name; if (fn) fn(data); return (SDL_Thread*)1; }
static inline void SDL_WaitThread(SDL_Thread *t, int *status)
    { (void)t; if (status) *status = 0; }
static inline void SDL_DetachThread(SDL_Thread *t) { (void)t; }

typedef unsigned long SDL_threadID;
static inline SDL_threadID SDL_ThreadID(void) { return 1; }
static inline SDL_threadID SDL_GetThreadID(SDL_Thread *t) { (void)t; return 1; }


#ifdef __cplusplus
}
#endif

#endif
