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

#endif
