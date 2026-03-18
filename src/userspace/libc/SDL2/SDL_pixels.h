#ifndef SDL_pixels_h_
#define SDL_pixels_h_

#include "SDL_stdinc.h"

/* Pixel format constants — only the ones games actually use. */
#define SDL_PIXELFORMAT_UNKNOWN   0
#define SDL_PIXELFORMAT_RGB888    0x16161804
#define SDL_PIXELFORMAT_ARGB8888  0x16362004
#define SDL_PIXELFORMAT_ABGR8888  0x16762004
#define SDL_PIXELFORMAT_RGB24     0x17101803

typedef struct {
    Uint32 format;
    int BitsPerPixel;
    int BytesPerPixel;
    Uint32 Rmask, Gmask, Bmask, Amask;
} SDL_PixelFormat;

typedef struct {
    Uint8 r, g, b, a;
} SDL_Color;

#endif
