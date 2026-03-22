#pragma once

/* Pre-define MAGIC_ENUM_ASSERT before magic_enum.hpp is included,
   avoiding the system <cassert> include chain issues. */
#define MAGIC_ENUM_ASSERT(...) (static_cast<void>(0))

/* Disable features we don't support */
#define NONET 1
#define NOSOUND 1
#define NOEXIT 1
#define NDEBUG 1

/* Additional controller types for game_controller.cpp */
#define SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_LEFT  7
#define SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT 8
#define SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_PAIR  9

/* Resampler — add a dummy member so the enum isn't empty, and define the default */
#define DVL_AULIB_SUPPORTS_SDL_RESAMPLER 1
#define DEVILUTIONX_DEFAULT_RESAMPLER SDL

/* SDL_FITHEIGHT for ctr display */
#define SDL_FITHEIGHT 0

/* More controller types */
#define SDL_CONTROLLER_TYPE_GOOGLE_STADIA 10
#define SDL_CONTROLLER_TYPE_AMAZON_LUNA   11
#define SDL_CONTROLLER_TYPE_NVIDIA_SHIELD 12
#define SDL_CONTROLLER_TYPE_VIRTUAL      13

/* Display texture format */
#define DEVILUTIONX_DISPLAY_TEXTURE_FORMAT SDL_PIXELFORMAT_ARGB8888

/* Use the optimized 16-entry palette blending path (avoids arg count mismatch) */
#define DEVILUTIONX_PALETTE_TRANSPARENCY_BLACK_16_LUT 1
