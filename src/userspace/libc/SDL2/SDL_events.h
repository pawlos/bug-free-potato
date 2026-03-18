#ifndef SDL_events_h_
#define SDL_events_h_

#include "SDL_stdinc.h"
#include "SDL_keycode.h"
#include "SDL_video.h"

/* Event types */
enum {
    SDL_QUIT           = 0x100,
    SDL_KEYDOWN        = 0x300,
    SDL_KEYUP          = 0x301,
    SDL_MOUSEMOTION    = 0x400,
    SDL_MOUSEBUTTONDOWN = 0x401,
    SDL_MOUSEBUTTONUP  = 0x402,
    SDL_MOUSEWHEEL     = 0x403,
    SDL_WINDOWEVENT    = 0x200,
};

/* Mouse button constants */
#define SDL_BUTTON_LEFT   1
#define SDL_BUTTON_MIDDLE 2
#define SDL_BUTTON_RIGHT  3

/* Window events */
enum {
    SDL_WINDOWEVENT_CLOSE = 14,
};

/* Key states */
#define SDL_PRESSED  1
#define SDL_RELEASED 0

typedef struct {
    SDL_Scancode scancode;
    SDL_Keycode  sym;
    Uint16       mod;
} SDL_Keysym;

typedef struct {
    Uint32    type;
    Uint32    timestamp;
    Uint32    windowID;
    Uint8     state;
    Uint8     repeat;
    SDL_Keysym keysym;
} SDL_KeyboardEvent;

typedef struct {
    Uint32 type;
    Uint32 timestamp;
    Uint32 windowID;
    Sint32 x, y;
    Sint32 xrel, yrel;
    Uint32 state;
} SDL_MouseMotionEvent;

typedef struct {
    Uint32 type;
    Uint32 timestamp;
    Uint32 windowID;
    Uint8  button;
    Uint8  state;
    Uint8  clicks;
    Sint32 x, y;
} SDL_MouseButtonEvent;

typedef struct {
    Uint32 type;
    Uint32 timestamp;
} SDL_QuitEvent;

typedef struct {
    Uint32 type;
    Uint32 timestamp;
    Uint32 windowID;
    Uint8  event;
} SDL_WindowEvent;

typedef union {
    Uint32               type;
    SDL_KeyboardEvent    key;
    SDL_MouseMotionEvent motion;
    SDL_MouseButtonEvent button;
    SDL_WindowEvent      window;
    SDL_QuitEvent        quit;
} SDL_Event;

int SDL_PollEvent(SDL_Event *event);
Uint32 SDL_GetMouseState(int *x, int *y);

#endif
