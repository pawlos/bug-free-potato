#ifndef SDL_pixels_h_
#define SDL_pixels_h_

#include "SDL_stdinc.h"

#ifdef __cplusplus
extern "C" {
#endif


/* Pixel format constants — only the ones games actually use. */
#define SDL_PIXELFORMAT_UNKNOWN   0
#define SDL_PIXELFORMAT_INDEX8    0x13000001
#define SDL_PIXELFORMAT_RGB332    0x14110801
#define SDL_PIXELFORMAT_RGB444    0x15120C02
#define SDL_PIXELFORMAT_RGB555    0x15130F02
#define SDL_PIXELFORMAT_RGB565    0x15151002
#define SDL_PIXELFORMAT_RGB888    0x16161804
#define SDL_PIXELFORMAT_RGBX8888  0x16261804
#define SDL_PIXELFORMAT_ARGB8888  0x16362004
#define SDL_PIXELFORMAT_RGBA8888  0x16462004
#define SDL_PIXELFORMAT_ABGR8888  0x16762004
#define SDL_PIXELFORMAT_BGRA8888  0x16862004
#define SDL_PIXELFORMAT_BGR888    0x16B61804
#define SDL_PIXELFORMAT_RGB24     0x17101803
#define SDL_PIXELFORMAT_BGR24     0x17401803

typedef Uint32 SDL_PixelFormatEnum;

#define SDL_BITSPERPIXEL(X)  (((X) >> 8) & 0xFF)
#define SDL_BYTESPERPIXEL(X) (((X) & 0xFF) ? ((X) & 0xFF) : (SDL_BITSPERPIXEL(X) + 7) / 8)
#define SDL_ISPIXELFORMAT_INDEXED(X) ((X) == SDL_PIXELFORMAT_INDEX8)

typedef struct {
    Uint8 r, g, b, a;
} SDL_Color;

#define SDL_ALPHA_OPAQUE      255
#define SDL_ALPHA_TRANSPARENT 0

typedef struct SDL_Palette {
    int       ncolors;
    SDL_Color *colors;
    Uint32    version;
    int       refcount;
} SDL_Palette;

typedef struct SDL_PixelFormat {
    Uint32      format;
    SDL_Palette *palette;
    int         BitsPerPixel;
    int         BytesPerPixel;
    Uint32      Rmask, Gmask, Bmask, Amask;
} SDL_PixelFormat;

SDL_Palette* SDL_AllocPalette(int ncolors);
void         SDL_FreePalette(SDL_Palette *palette);
int          SDL_SetPaletteColors(SDL_Palette *palette, const SDL_Color *colors,
                                   int firstcolor, int ncolors);


#ifdef __cplusplus
}
#endif

#endif
