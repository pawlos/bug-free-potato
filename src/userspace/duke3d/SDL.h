/* SDL.h stub for potatOS -- minimal types and constants for Duke3D compilation */
#pragma once

/* SDL_Quit is a no-op */
#ifndef SDL_Quit
#define SDL_Quit() ((void)0)
#endif

/* SDL_Surface stub */
typedef struct SDL_Surface {
    int w, h;
    void *pixels;
} SDL_Surface;

/* SDL grab mode constants */
#define SDL_GRAB_QUERY  (-1)
#define SDL_GRAB_OFF    0
#define SDL_GRAB_ON     1

/* SDL init constants */
#define SDL_INIT_VIDEO  0x00000020
#define SDL_INIT_AUDIO  0x00000010
#define SDL_INIT_TIMER  0x00000001

/* SDL_mutex stub (used by multivoc.h) */
typedef int SDL_mutex;
static inline SDL_mutex *SDL_CreateMutex(void) { return (SDL_mutex *)0; }
static inline void SDL_DestroyMutex(SDL_mutex *m) { (void)m; }
static inline int SDL_LockMutex(SDL_mutex *m) { (void)m; return 0; }
static inline int SDL_UnlockMutex(SDL_mutex *m) { (void)m; return 0; }
#define SDL_mutexP(m) SDL_LockMutex(m)
#define SDL_mutexV(m) SDL_UnlockMutex(m)

/* SDL_WM_GrabInput stub */
static inline int SDL_WM_GrabInput(int mode) { (void)mode; return SDL_GRAB_OFF; }
static inline int SDL_Init(unsigned int flags) { (void)flags; return 0; }
static inline int SDL_WasInit(unsigned int flags) { (void)flags; return 0; }
static inline void SDL_WM_SetCaption(const char *t, const char *i) { (void)t; (void)i; }
static inline void SDL_QuitSubSystem(unsigned int flags) { (void)flags; }
static inline int SDL_ShowCursor(int toggle) { (void)toggle; return 0; }
