#ifndef SDL_render_h_
#define SDL_render_h_

#include "SDL_stdinc.h"
#include "SDL_rect.h"
#include "SDL_video.h"

/* Renderer flags */
#define SDL_RENDERER_SOFTWARE      0x00000001
#define SDL_RENDERER_ACCELERATED   0x00000002
#define SDL_RENDERER_PRESENTVSYNC  0x00000004

/* Texture access modes */
#define SDL_TEXTUREACCESS_STATIC    0
#define SDL_TEXTUREACCESS_STREAMING 1
#define SDL_TEXTUREACCESS_TARGET    2

typedef struct SDL_Renderer SDL_Renderer;
typedef struct SDL_Texture  SDL_Texture;

SDL_Renderer* SDL_CreateRenderer(SDL_Window *window, int index, Uint32 flags);
void          SDL_DestroyRenderer(SDL_Renderer *renderer);

SDL_Texture*  SDL_CreateTexture(SDL_Renderer *renderer, Uint32 format,
                                int access, int w, int h);
void          SDL_DestroyTexture(SDL_Texture *texture);

int  SDL_UpdateTexture(SDL_Texture *texture, const SDL_Rect *rect,
                       const void *pixels, int pitch);
int  SDL_LockTexture(SDL_Texture *texture, const SDL_Rect *rect,
                     void **pixels, int *pitch);
void SDL_UnlockTexture(SDL_Texture *texture);

int  SDL_SetRenderDrawColor(SDL_Renderer *r, Uint8 red, Uint8 green,
                            Uint8 blue, Uint8 alpha);
int  SDL_RenderClear(SDL_Renderer *renderer);
int  SDL_RenderCopy(SDL_Renderer *renderer, SDL_Texture *texture,
                    const SDL_Rect *srcrect, const SDL_Rect *dstrect);
int  SDL_RenderFillRect(SDL_Renderer *renderer, const SDL_Rect *rect);
void SDL_RenderPresent(SDL_Renderer *renderer);

static inline void SDL_RenderGetScale(SDL_Renderer *r, float *sx, float *sy)
    { (void)r; if (sx) *sx = 1.0f; if (sy) *sy = 1.0f; }
static inline int SDL_QueryTexture(SDL_Texture *t, Uint32 *fmt, int *access, int *w, int *h)
    { (void)t; if(fmt)*fmt=0; if(access)*access=0; if(w)*w=0; if(h)*h=0; return 0; }
static inline int SDL_SetRenderTarget(SDL_Renderer *r, SDL_Texture *t) { (void)r; (void)t; return 0; }
static inline SDL_Texture* SDL_GetRenderTarget(SDL_Renderer *r) { (void)r; return (SDL_Texture*)0; }
static inline void SDL_RenderGetLogicalSize(SDL_Renderer *r, int *w, int *h)
    { (void)r; if(w)*w=0; if(h)*h=0; }
static inline int SDL_RenderSetLogicalSize(SDL_Renderer *r, int w, int h)
    { (void)r; (void)w; (void)h; return 0; }
static inline void SDL_RenderGetViewport(SDL_Renderer *r, SDL_Rect *rect)
    { (void)r; if(rect) { rect->x=0; rect->y=0; rect->w=0; rect->h=0; } }
static inline int SDL_SetTextureAlphaMod(SDL_Texture *t, Uint8 alpha) { (void)t; (void)alpha; return 0; }
static inline int SDL_SetTextureColorMod(SDL_Texture *t, Uint8 r, Uint8 g, Uint8 b)
    { (void)t; (void)r; (void)g; (void)b; return 0; }
static inline int SDL_SetTextureBlendMode(SDL_Texture *t, int mode) { (void)t; (void)mode; return 0; }
static inline int SDL_RenderSetClipRect(SDL_Renderer *r, const SDL_Rect *rect) { (void)r; (void)rect; return 0; }
static inline int SDL_RenderSetIntegerScale(SDL_Renderer *r, SDL_bool enable) { (void)r; (void)enable; return 0; }

SDL_Texture* SDL_CreateTextureFromSurface(SDL_Renderer *renderer, SDL_Surface *surface);

static inline int SDL_GetRendererOutputSize(SDL_Renderer *r, int *w, int *h)
    { (void)r; if(w)*w=0; if(h)*h=0; return 0; }

#endif
