#ifndef SDL_syswm_h_
#define SDL_syswm_h_
/* Stub: potatOS has no window manager protocol. Wolf4SDL only needs the
 * type to exist for SDL_GetWindowWMInfo, which we make a no-op. */

#include "SDL_stdinc.h"
#include "SDL_video.h"
#include "SDL_version.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SDL_SYSWM_UNKNOWN
} SDL_SYSWM_TYPE;

typedef struct SDL_SysWMmsg { SDL_version version; SDL_SYSWM_TYPE subsystem; } SDL_SysWMmsg;
typedef struct SDL_SysWMinfo {
    SDL_version    version;
    SDL_SYSWM_TYPE subsystem;
    union { int dummy; } info;
} SDL_SysWMinfo;

static inline SDL_bool SDL_GetWindowWMInfo(SDL_Window *w, SDL_SysWMinfo *info)
{ (void)w; (void)info; return SDL_FALSE; }

#ifdef __cplusplus
}
#endif

#endif
