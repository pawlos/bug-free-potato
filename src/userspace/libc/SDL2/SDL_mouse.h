#ifndef SDL_mouse_h_
#define SDL_mouse_h_

#include "SDL_stdinc.h"
#include "SDL_video.h"

typedef struct SDL_Cursor SDL_Cursor;

Uint32 SDL_GetMouseState(int *x, int *y);

static inline SDL_Cursor* SDL_CreateSystemCursor(int id) { (void)id; return (SDL_Cursor*)0; }
static inline void SDL_SetCursor(SDL_Cursor *c) { (void)c; }
static inline void SDL_FreeCursor(SDL_Cursor *c) { (void)c; }
static inline int  SDL_ShowCursor(int toggle) { (void)toggle; return 0; }
static inline void SDL_WarpMouseInWindow(SDL_Window *w, int x, int y)
    { (void)w; (void)x; (void)y; }

#define SDL_SYSTEM_CURSOR_ARROW 0
#define SDL_SYSTEM_CURSOR_HAND  11

#endif
