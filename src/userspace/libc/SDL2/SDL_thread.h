#ifndef SDL_thread_h_
#define SDL_thread_h_

#include "SDL_stdinc.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SDLCALL
#define SDLCALL
#endif

typedef struct SDL_Thread SDL_Thread;
typedef int (*SDL_ThreadFunction)(void *data);
typedef unsigned long SDL_threadID;

SDL_Thread*  SDL_CreateThread(SDL_ThreadFunction fn, const char *name, void *data);
void         SDL_WaitThread(SDL_Thread *t, int *status);
void         SDL_DetachThread(SDL_Thread *t);
SDL_threadID SDL_ThreadID(void);
SDL_threadID SDL_GetThreadID(SDL_Thread *t);

/* Stack-size variant — same as SDL_CreateThread, stacksize ignored. */
static inline SDL_Thread* SDL_CreateThreadWithStackSize(
    SDL_ThreadFunction fn, const char *name, size_t stacksize, void *data)
{ (void)stacksize; return SDL_CreateThread(fn, name, data); }

#ifdef __cplusplus
}
#endif

#endif
