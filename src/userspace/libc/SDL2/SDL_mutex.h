#ifndef SDL_mutex_h_
#define SDL_mutex_h_

#include "SDL_stdinc.h"

#ifdef __cplusplus
extern "C" {
#endif


/* potatOS is single-threaded, so mutexes are no-ops. */

typedef struct SDL_mutex SDL_mutex;

static inline SDL_mutex* SDL_CreateMutex(void) { return (SDL_mutex*)1; }
static inline void       SDL_DestroyMutex(SDL_mutex *m) { (void)m; }
static inline int        SDL_LockMutex(SDL_mutex *m) { (void)m; return 0; }
static inline int        SDL_UnlockMutex(SDL_mutex *m) { (void)m; return 0; }

#define SDL_MUTEX_TIMEDOUT 1

static inline int SDL_TryLockMutex(SDL_mutex *m) { (void)m; return 0; }

typedef struct SDL_cond SDL_cond;
static inline SDL_cond* SDL_CreateCond(void) { return (SDL_cond*)1; }
static inline void SDL_DestroyCond(SDL_cond *c) { (void)c; }
static inline int SDL_CondWait(SDL_cond *c, SDL_mutex *m) { (void)c; (void)m; return 0; }
static inline int SDL_CondSignal(SDL_cond *c) { (void)c; return 0; }
static inline int SDL_CondBroadcast(SDL_cond *c) { (void)c; return 0; }

typedef struct SDL_sem SDL_sem;
static inline SDL_sem* SDL_CreateSemaphore(Uint32 val) { (void)val; return (SDL_sem*)1; }
static inline void SDL_DestroySemaphore(SDL_sem *s) { (void)s; }
static inline int SDL_SemWait(SDL_sem *s) { (void)s; return 0; }
static inline int SDL_SemPost(SDL_sem *s) { (void)s; return 0; }


#ifdef __cplusplus
}
#endif

#endif
