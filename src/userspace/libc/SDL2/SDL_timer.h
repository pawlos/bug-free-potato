#ifndef SDL_timer_h_
#define SDL_timer_h_

#include "SDL_stdinc.h"

Uint32 SDL_GetTicks(void);
void   SDL_Delay(Uint32 ms);

typedef int SDL_TimerID;
typedef Uint32 (*SDL_TimerCallback)(Uint32 interval, void *param);

static inline SDL_TimerID SDL_AddTimer(Uint32 interval, SDL_TimerCallback cb, void *param)
    { (void)interval; (void)cb; (void)param; return 1; }
static inline SDL_bool SDL_RemoveTimer(SDL_TimerID id)
    { (void)id; return SDL_TRUE; }

#endif
