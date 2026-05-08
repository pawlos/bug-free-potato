#ifndef SDL_timer_h_
#define SDL_timer_h_

#include "SDL_stdinc.h"

#ifdef __cplusplus
extern "C" {
#endif


Uint32 SDL_GetTicks(void);
void   SDL_Delay(Uint32 ms);

typedef int SDL_TimerID;
typedef Uint32 (*SDL_TimerCallback)(Uint32 interval, void *param);

/* Real implementations in sdl2_thread.c */
SDL_TimerID SDL_AddTimer(Uint32 interval, SDL_TimerCallback cb, void *param);
SDL_bool    SDL_RemoveTimer(SDL_TimerID id);


#ifdef __cplusplus
}
#endif

#endif
