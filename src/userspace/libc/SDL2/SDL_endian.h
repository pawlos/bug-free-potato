#ifndef SDL_endian_h_
#define SDL_endian_h_

#include "SDL_stdinc.h"

/* x86_64 is always little-endian */
#define SDL_LIL_ENDIAN 1234
#define SDL_BIG_ENDIAN 4321
#define SDL_BYTEORDER  SDL_LIL_ENDIAN

static inline Uint16 SDL_Swap16(Uint16 x) { return (x << 8) | (x >> 8); }
static inline Uint32 SDL_Swap32(Uint32 x) {
    return ((x << 24) | ((x << 8) & 0x00FF0000) |
            ((x >> 8) & 0x0000FF00) | (x >> 24));
}
static inline Uint64 SDL_Swap64(Uint64 x) {
    Uint32 hi = (Uint32)(x >> 32);
    Uint32 lo = (Uint32)(x & 0xFFFFFFFF);
    return ((Uint64)SDL_Swap32(lo) << 32) | SDL_Swap32(hi);
}

#define SDL_SwapLE16(x) (x)
#define SDL_SwapLE32(x) (x)
#define SDL_SwapLE64(x) (x)
#define SDL_SwapBE16(x) SDL_Swap16(x)
#define SDL_SwapBE32(x) SDL_Swap32(x)
#define SDL_SwapBE64(x) SDL_Swap64(x)

#endif
