#ifndef SDL_mouse_h_
#define SDL_mouse_h_

#include "SDL_stdinc.h"
#include "SDL_video.h"

#ifdef __cplusplus
extern "C" {
#endif


typedef struct SDL_Cursor SDL_Cursor;

Uint32 SDL_GetMouseState(int *x, int *y);

static inline SDL_Cursor* SDL_CreateSystemCursor(int id) { (void)id; return (SDL_Cursor*)0; }
static inline void SDL_SetCursor(SDL_Cursor *c) { (void)c; }
static inline void SDL_FreeCursor(SDL_Cursor *c) { (void)c; }
static inline int  SDL_ShowCursor(int toggle) { (void)toggle; return 0; }
static inline void SDL_WarpMouseInWindow(SDL_Window *w, int x, int y)
    { (void)w; (void)x; (void)y; }
static inline int SDL_SetRelativeMouseMode(SDL_bool enabled) { (void)enabled; return 0; }
static inline SDL_bool SDL_GetRelativeMouseMode(void) { return SDL_FALSE; }
static inline Uint32 SDL_GetRelativeMouseState(int *x, int *y)
    { if (x) *x = 0; if (y) *y = 0; return 0; }

#define SDL_BUTTON_LEFT     1
#define SDL_BUTTON_MIDDLE   2
#define SDL_BUTTON_RIGHT    3
#define SDL_BUTTON_X1       4
#define SDL_BUTTON_X2       5
#define SDL_BUTTON(X)       (1 << ((X)-1))
#define SDL_BUTTON_LMASK    SDL_BUTTON(SDL_BUTTON_LEFT)
#define SDL_BUTTON_MMASK    SDL_BUTTON(SDL_BUTTON_MIDDLE)
#define SDL_BUTTON_RMASK    SDL_BUTTON(SDL_BUTTON_RIGHT)

#define SDL_SYSTEM_CURSOR_ARROW 0
#define SDL_SYSTEM_CURSOR_HAND  11

static inline SDL_Cursor* SDL_CreateColorCursor(SDL_Surface *s, int hot_x, int hot_y)
    { (void)s; (void)hot_x; (void)hot_y; return (SDL_Cursor*)0; }


#ifdef __cplusplus
}
#endif

#endif
