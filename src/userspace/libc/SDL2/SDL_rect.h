#ifndef SDL_rect_h_
#define SDL_rect_h_

#include "SDL_stdinc.h"

typedef struct { int x, y; } SDL_Point;
typedef struct { int x, y, w, h; } SDL_Rect;

static inline SDL_bool SDL_RectEmpty(const SDL_Rect *r)
{
    return (!r || r->w <= 0 || r->h <= 0) ? SDL_TRUE : SDL_FALSE;
}

#endif
