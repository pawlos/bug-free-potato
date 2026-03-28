#ifndef SDL_image_h_
#define SDL_image_h_

#include "SDL.h"
#include "SDL_surface.h"

#define IMG_INIT_PNG  0x02
#define IMG_INIT_JPG  0x01

static inline int IMG_Init(int flags) { (void)flags; return flags; }
static inline void IMG_Quit(void) {}
static inline const char* IMG_GetError(void) { return SDL_GetError(); }

SDL_Surface* IMG_Load(const char *file);
SDL_Surface* IMG_Load_RW(SDL_RWops *src, int freesrc);

#endif
