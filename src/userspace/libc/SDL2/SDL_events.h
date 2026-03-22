#ifndef SDL_events_h_
#define SDL_events_h_

#include "SDL_stdinc.h"
#include "SDL_keycode.h"
#include "SDL_video.h"
#include "SDL_gamecontroller.h"

/* Event types */
enum {
    SDL_QUIT           = 0x100,
    SDL_WINDOWEVENT    = 0x200,
    SDL_KEYDOWN        = 0x300,
    SDL_KEYUP          = 0x301,
    SDL_MOUSEMOTION    = 0x400,
    SDL_MOUSEBUTTONDOWN = 0x401,
    SDL_MOUSEBUTTONUP  = 0x402,
    SDL_MOUSEWHEEL     = 0x403,
    SDL_JOYHATMOTION   = 0x602,
    SDL_CONTROLLERBUTTONDOWN = 0x651,
    SDL_CONTROLLERBUTTONUP   = 0x652,
    SDL_CONTROLLERAXISMOTION = 0x650,
    SDL_CONTROLLERDEVICEADDED   = 0x653,
    SDL_CONTROLLERDEVICEREMOVED = 0x654,
    SDL_JOYAXISMOTION    = 0x600,
    SDL_JOYBALLMOTION    = 0x601,
    SDL_JOYHATMOTION_    = 0x602,  /* alias */
    SDL_JOYBUTTONDOWN    = 0x603,
    SDL_JOYBUTTONUP      = 0x604,
    SDL_JOYDEVICEADDED   = 0x605,
    SDL_JOYDEVICEREMOVED = 0x606,
    SDL_FINGERDOWN       = 0x700,
    SDL_FINGERUP         = 0x701,
    SDL_FINGERMOTION     = 0x702,
    SDL_TEXTINPUT        = 0x303,
    SDL_TEXTEDITING      = 0x302,
    SDL_KEYMAPCHANGED    = 0x304,
    SDL_EVENT_KEYMAP_CHANGED = 0x304,
};

/* Mouse button constants */
#define SDL_BUTTON_LEFT   1
#define SDL_BUTTON_MIDDLE 2
#define SDL_BUTTON_RIGHT  3
#define SDL_BUTTON_X1     4
#define SDL_BUTTON_X2     5

/* Window events */
enum {
    SDL_WINDOWEVENT_NONE = 0,
    SDL_WINDOWEVENT_SHOWN = 1,
    SDL_WINDOWEVENT_HIDDEN = 2,
    SDL_WINDOWEVENT_EXPOSED = 3,
    SDL_WINDOWEVENT_MOVED = 4,
    SDL_WINDOWEVENT_RESIZED = 5,
    SDL_WINDOWEVENT_SIZE_CHANGED = 6,
    SDL_WINDOWEVENT_MINIMIZED = 7,
    SDL_WINDOWEVENT_MAXIMIZED = 8,
    SDL_WINDOWEVENT_RESTORED = 9,
    SDL_WINDOWEVENT_ENTER = 10,
    SDL_WINDOWEVENT_LEAVE = 11,
    SDL_WINDOWEVENT_FOCUS_GAINED = 12,
    SDL_WINDOWEVENT_FOCUS_LOST = 13,
    SDL_WINDOWEVENT_CLOSE = 14,
    SDL_WINDOWEVENT_TAKE_FOCUS = 15,
    SDL_WINDOWEVENT_HIT_TEST = 16,
};

#define SDL_AUDIODEVICEADDED   0x1100
#define SDL_AUDIODEVICEREMOVED 0x1101

/* Syswm event type (stub) */
#define SDL_SYSWMEVENT 0x201

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
    Uint32 which;
    Sint32 x, y;
    Sint32 xrel, yrel;
    Uint32 state;
} SDL_MouseMotionEvent;

typedef struct {
    Uint32 type;
    Uint32 timestamp;
    Uint32 windowID;
    Uint32 which;
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

typedef struct {
    Uint32 type;
    Uint32 timestamp;
    Uint32 windowID;
    Uint32 which;
    Sint32 x, y;
    Uint32 direction;
} SDL_MouseWheelEvent;

typedef Sint64 SDL_FingerID;
typedef Sint64 SDL_TouchID;

typedef struct {
    Uint32 type;
    Uint32 timestamp;
    SDL_TouchID touchId;
    SDL_FingerID fingerId;
    float x, y, dx, dy, pressure;
} SDL_TouchFingerEvent;

typedef struct {
    Uint32 type;
    Uint32 timestamp;
    Uint32 windowID;
    char text[32];
} SDL_TextInputEvent;

typedef struct {
    Uint32 type;
    Uint32 timestamp;
    SDL_JoystickID which;
    Uint8 button;
    Uint8 state;
} SDL_JoyButtonEvent;

typedef union {
    Uint32               type;
    SDL_KeyboardEvent    key;
    SDL_MouseMotionEvent motion;
    SDL_MouseButtonEvent button;
    SDL_MouseWheelEvent  wheel;
    SDL_WindowEvent      window;
    SDL_QuitEvent        quit;
    SDL_ControllerButtonEvent cbutton;
    SDL_ControllerAxisEvent   caxis;
    SDL_JoyHatEvent      jhat;
    SDL_ControllerDeviceEvent cdevice;
    SDL_JoyDeviceEvent   jdevice;
    SDL_TouchFingerEvent tfinger;
    SDL_TextInputEvent   text;
    SDL_JoyButtonEvent   jbutton;
    SDL_ControllerAxisEvent jaxis;
    struct { Uint32 type; Uint32 timestamp; SDL_JoystickID which; Uint8 ball; Sint16 xrel; Sint16 yrel; } jball;
    struct { Uint32 type; Uint32 timestamp; Uint32 which; Uint8 iscapture; } adevice;
    struct { Uint32 type; Uint32 timestamp; Uint32 windowID; Sint32 code; void *data1; void *data2; } user;
} SDL_Event;

static inline SDL_Scancode SDL_GetScancodeFromKey(SDL_Keycode key) { (void)key; return SDL_SCANCODE_UNKNOWN; }

int SDL_PollEvent(SDL_Event *event);
int SDL_PushEvent(SDL_Event *event);
Uint32 SDL_GetMouseState(int *x, int *y);
const Uint8* SDL_GetKeyboardState(int *numkeys);
static inline Uint16 SDL_GetModState(void) { return 0; }
static inline void SDL_SetModState(Uint16 modstate) { (void)modstate; }
static inline Uint32 SDL_RegisterEvents(int numevents) { (void)numevents; return 0xFFFFFFFF; }

#define SDL_TOUCH_MOUSEID ((Uint32)-1)
#define SDL_USEREVENT     0x8000

typedef Uint32 SDL_EventType;

#endif
