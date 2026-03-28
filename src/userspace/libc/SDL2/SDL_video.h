#ifndef SDL_video_h_
#define SDL_video_h_

#include "SDL_stdinc.h"
#include "SDL_rect.h"

/* Window position sentinels */
#define SDL_WINDOWPOS_UNDEFINED 0x1FFF0000
#define SDL_WINDOWPOS_CENTERED  0x2FFF0000

/* Window flags */
#define SDL_WINDOW_FULLSCREEN         0x00000001
#define SDL_WINDOW_SHOWN              0x00000004
#define SDL_WINDOW_HIDDEN             0x00000008
#define SDL_WINDOW_RESIZABLE          0x00000020
#define SDL_WINDOW_FULLSCREEN_DESKTOP 0x00001001
#define SDL_WINDOW_ALLOW_HIGHDPI      0x00002000
#define SDL_WINDOW_INPUT_FOCUS        0x00000200
#define SDL_WINDOW_MOUSE_FOCUS        0x00000400

/* Legacy SDL1 compat */
#define SDL_FULLSCREEN SDL_WINDOW_FULLSCREEN

typedef struct {
    Uint32 format;
    int w, h;
    int refresh_rate;
    void *driverdata;
} SDL_DisplayMode;

typedef struct SDL_Window SDL_Window;

SDL_Window* SDL_CreateWindow(const char *title, int x, int y, int w, int h,
                             Uint32 flags);
void        SDL_DestroyWindow(SDL_Window *window);
void        SDL_GetWindowSize(SDL_Window *window, int *w, int *h);
void        SDL_SetWindowTitle(SDL_Window *window, const char *title);
Uint32      SDL_GetWindowID(SDL_Window *window);
static inline SDL_Window* SDL_GetKeyboardFocus(void) { return (SDL_Window*)0; }
static inline SDL_Window* SDL_GetMouseFocus(void) { return (SDL_Window*)0; }
static inline void SDL_SetWindowResizable(SDL_Window *w, SDL_bool resizable) { (void)w; (void)resizable; }

struct SDL_Surface; /* forward decl */
SDL_Surface* SDL_GetWindowSurface(SDL_Window *window);
int          SDL_UpdateWindowSurface(SDL_Window *window);

static inline int SDL_GetDesktopDisplayMode(int idx, SDL_DisplayMode *mode)
    { (void)idx; if (mode) { mode->format=0; mode->w=1024; mode->h=768; mode->refresh_rate=60; mode->driverdata=0; } return 0; }
static inline int SDL_GetCurrentDisplayMode(int idx, SDL_DisplayMode *mode)
    { return SDL_GetDesktopDisplayMode(idx, mode); }
static inline int SDL_GetDisplayMode(int idx, int modeIdx, SDL_DisplayMode *mode)
    { (void)modeIdx; return SDL_GetDesktopDisplayMode(idx, mode); }
static inline int SDL_GetNumDisplayModes(int idx) { (void)idx; return 1; }
static inline int SDL_GetNumVideoDisplays(void) { return 1; }
static inline int SDL_GetWindowDisplayMode(SDL_Window *w, SDL_DisplayMode *mode)
    { (void)w; return SDL_GetDesktopDisplayMode(0, mode); }
static inline int SDL_SetWindowDisplayMode(SDL_Window *w, const SDL_DisplayMode *mode)
    { (void)w; (void)mode; return 0; }

#endif
