#ifndef SDL_version_h_
#define SDL_version_h_

#include "SDL_stdinc.h"

typedef struct {
    Uint8 major;
    Uint8 minor;
    Uint8 patch;
} SDL_version;

#define SDL_MAJOR_VERSION 2
#define SDL_MINOR_VERSION 28
#define SDL_PATCHLEVEL    0

#define SDL_VERSION(x) do { \
    (x)->major = SDL_MAJOR_VERSION; \
    (x)->minor = SDL_MINOR_VERSION; \
    (x)->patch = SDL_PATCHLEVEL; \
} while (0)

#define SDL_VERSIONNUM(X, Y, Z) ((X)*1000 + (Y)*100 + (Z))
#define SDL_COMPILEDVERSION SDL_VERSIONNUM(SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL)
#define SDL_VERSION_ATLEAST(X, Y, Z) (SDL_COMPILEDVERSION >= SDL_VERSIONNUM(X, Y, Z))

static inline void SDL_GetVersion(SDL_version *ver) { SDL_VERSION(ver); }

#endif
