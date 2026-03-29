#ifndef SDL_rect_h_
#define SDL_rect_h_

#include "SDL_stdinc.h"

#ifdef __cplusplus
extern "C" {
#endif


typedef struct { int x, y; } SDL_Point;
typedef struct { int x, y, w, h; } SDL_Rect;

static inline SDL_bool SDL_RectEmpty(const SDL_Rect *r)
{
    return (!r || r->w <= 0 || r->h <= 0) ? SDL_TRUE : SDL_FALSE;
}

static inline SDL_bool SDL_PointInRect(const SDL_Point *p, const SDL_Rect *r)
{
    return (p && r &&
            p->x >= r->x && p->x < (r->x + r->w) &&
            p->y >= r->y && p->y < (r->y + r->h)) ? SDL_TRUE : SDL_FALSE;
}

static inline SDL_bool SDL_HasIntersection(const SDL_Rect *a, const SDL_Rect *b)
{
    if (!a || !b) return SDL_FALSE;
    if (a->x + a->w <= b->x || b->x + b->w <= a->x) return SDL_FALSE;
    if (a->y + a->h <= b->y || b->y + b->h <= a->y) return SDL_FALSE;
    return SDL_TRUE;
}

static inline SDL_bool SDL_IntersectRect(const SDL_Rect *a, const SDL_Rect *b, SDL_Rect *result)
{
    if (!SDL_HasIntersection(a, b)) return SDL_FALSE;
    int x1 = a->x > b->x ? a->x : b->x;
    int y1 = a->y > b->y ? a->y : b->y;
    int x2 = (a->x+a->w) < (b->x+b->w) ? (a->x+a->w) : (b->x+b->w);
    int y2 = (a->y+a->h) < (b->y+b->h) ? (a->y+a->h) : (b->y+b->h);
    if (result) { result->x = x1; result->y = y1; result->w = x2-x1; result->h = y2-y1; }
    return SDL_TRUE;
}

static inline void SDL_UnionRect(const SDL_Rect *a, const SDL_Rect *b, SDL_Rect *result)
{
    if (!result) return;
    int x1 = a->x < b->x ? a->x : b->x;
    int y1 = a->y < b->y ? a->y : b->y;
    int x2 = (a->x+a->w) > (b->x+b->w) ? (a->x+a->w) : (b->x+b->w);
    int y2 = (a->y+a->h) > (b->y+b->h) ? (a->y+a->h) : (b->y+b->h);
    result->x = x1; result->y = y1; result->w = x2-x1; result->h = y2-y1;
}


#ifdef __cplusplus
}
#endif

#endif
