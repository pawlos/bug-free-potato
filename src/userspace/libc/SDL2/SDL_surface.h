#ifndef SDL_surface_h_
#define SDL_surface_h_

#include "SDL_stdinc.h"
#include "SDL_rect.h"
#include "SDL_pixels.h"

typedef struct SDL_Surface {
    Uint32  flags;
    SDL_PixelFormat *format;
    int     w, h;
    int     pitch;
    void   *pixels;
    void   *userdata;
    int     locked;
    void   *lock_data;
    SDL_Rect clip_rect;
    int     refcount;
} SDL_Surface;

SDL_Surface* SDL_CreateRGBSurface(Uint32 flags, int w, int h, int depth,
                                   Uint32 Rmask, Uint32 Gmask,
                                   Uint32 Bmask, Uint32 Amask);
SDL_Surface* SDL_CreateRGBSurfaceWithFormat(Uint32 flags, int w, int h,
                                            int depth, Uint32 format);
SDL_Surface* SDL_CreateRGBSurfaceFrom(void *pixels, int w, int h, int depth,
                                       int pitch, Uint32 Rmask, Uint32 Gmask,
                                       Uint32 Bmask, Uint32 Amask);
void         SDL_FreeSurface(SDL_Surface *surface);
int          SDL_LockSurface(SDL_Surface *surface);
void         SDL_UnlockSurface(SDL_Surface *surface);
int          SDL_SetSurfaceColorMod(SDL_Surface *surface, Uint8 r, Uint8 g, Uint8 b);
int          SDL_SetSurfaceAlphaMod(SDL_Surface *surface, Uint8 alpha);
int          SDL_SetColorKey(SDL_Surface *surface, int flag, Uint32 key);
int          SDL_BlitSurface(SDL_Surface *src, const SDL_Rect *srcrect,
                             SDL_Surface *dst, SDL_Rect *dstrect);
int          SDL_FillRect(SDL_Surface *dst, const SDL_Rect *rect, Uint32 color);
Uint32       SDL_MapRGB(const SDL_PixelFormat *format, Uint8 r, Uint8 g, Uint8 b);
Uint32       SDL_MapRGBA(const SDL_PixelFormat *format, Uint8 r, Uint8 g, Uint8 b, Uint8 a);
SDL_Surface* SDL_ConvertSurface(SDL_Surface *src, const SDL_PixelFormat *fmt, Uint32 flags);
SDL_Surface* SDL_ConvertSurfaceFormat(SDL_Surface *src, Uint32 pixel_format, Uint32 flags);
SDL_Surface* SDL_CreateRGBSurfaceWithFormatFrom(void *pixels, int w, int h,
                                                 int depth, int pitch, Uint32 format);

int  SDL_SetSurfacePalette(SDL_Surface *surface, SDL_Palette *palette);
int  SDL_SetClipRect(SDL_Surface *surface, const SDL_Rect *rect);
void SDL_GetClipRect(SDL_Surface *surface, SDL_Rect *rect);

int  SDL_BlitScaled(SDL_Surface *src, const SDL_Rect *srcrect,
                    SDL_Surface *dst, SDL_Rect *dstrect);
int  SDL_SoftStretch(SDL_Surface *src, const SDL_Rect *srcrect,
                     SDL_Surface *dst, const SDL_Rect *dstrect);

#define SDL_MUSTLOCK(s) ((s)->locked)

#endif
