#ifndef SDL_mutex_h_
#define SDL_mutex_h_

#include "SDL_stdinc.h"

/* potatOS is single-threaded, so mutexes are no-ops. */

typedef struct SDL_mutex SDL_mutex;

static inline SDL_mutex* SDL_CreateMutex(void) { return (SDL_mutex*)1; }
static inline void       SDL_DestroyMutex(SDL_mutex *m) { (void)m; }
static inline int        SDL_LockMutex(SDL_mutex *m) { (void)m; return 0; }
static inline int        SDL_UnlockMutex(SDL_mutex *m) { (void)m; return 0; }

#define SDL_MUTEX_TIMEDOUT 1

#endif
