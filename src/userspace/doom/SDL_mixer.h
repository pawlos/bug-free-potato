/* SDL_mixer.h stub for the potatOS Doom port.
 *
 * doomgeneric's i_sound.c does `#include <SDL_mixer.h>` under FEATURE_SOUND but
 * uses no Mix_/SDL symbols here — the potato port routes all audio through
 * DG_sound_module (doomgeneric_potato_sound.c). We only need to satisfy the
 * include.
 *
 * This deliberately shadows the full SDL2 shim at libc/SDL_mixer.h (which pulls
 * in SDL2/SDL.h → SDL_stdinc.h's `#define false 0`, colliding with doomtype.h's
 * `enum { false = 0, ... }`). DOOM_DIR is placed first on the Doom include path
 * so this stub wins for Doom only; other SDL2 ports keep the real shim.
 */
#pragma once
