#ifndef SDL_mutex_h_
#define SDL_mutex_h_

#include "SDL_stdinc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SDL_MUTEX_TIMEDOUT 1

typedef struct SDL_mutex SDL_mutex;

SDL_mutex* SDL_CreateMutex(void);
void       SDL_DestroyMutex(SDL_mutex *m);
int        SDL_LockMutex(SDL_mutex *m);
int        SDL_TryLockMutex(SDL_mutex *m);
int        SDL_UnlockMutex(SDL_mutex *m);

typedef struct SDL_cond SDL_cond;

SDL_cond* SDL_CreateCond(void);
void      SDL_DestroyCond(SDL_cond *c);
int       SDL_CondWait(SDL_cond *c, SDL_mutex *m);
int       SDL_CondSignal(SDL_cond *c);
int       SDL_CondBroadcast(SDL_cond *c);

typedef struct SDL_sem SDL_sem;

SDL_sem* SDL_CreateSemaphore(Uint32 initial_value);
void     SDL_DestroySemaphore(SDL_sem *s);
int      SDL_SemWait(SDL_sem *s);
int      SDL_SemTryWait(SDL_sem *s);
int      SDL_SemPost(SDL_sem *s);
Uint32   SDL_SemValue(SDL_sem *s);

#ifdef __cplusplus
}
#endif

#endif
