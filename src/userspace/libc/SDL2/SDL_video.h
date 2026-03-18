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

typedef struct SDL_Window SDL_Window;

SDL_Window* SDL_CreateWindow(const char *title, int x, int y, int w, int h,
                             Uint32 flags);
void        SDL_DestroyWindow(SDL_Window *window);
void        SDL_GetWindowSize(SDL_Window *window, int *w, int *h);
void        SDL_SetWindowTitle(SDL_Window *window, const char *title);
Uint32      SDL_GetWindowID(SDL_Window *window);

#endif
