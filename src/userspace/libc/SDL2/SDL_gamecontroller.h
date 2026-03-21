#ifndef SDL_gamecontroller_h_
#define SDL_gamecontroller_h_

#include "SDL_stdinc.h"
#include "SDL_joystick.h"

typedef struct SDL_GameController SDL_GameController;

typedef enum {
    SDL_CONTROLLER_BUTTON_INVALID = -1,
    SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B,
    SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y,
    SDL_CONTROLLER_BUTTON_BACK, SDL_CONTROLLER_BUTTON_GUIDE,
    SDL_CONTROLLER_BUTTON_START,
    SDL_CONTROLLER_BUTTON_LEFTSTICK, SDL_CONTROLLER_BUTTON_RIGHTSTICK,
    SDL_CONTROLLER_BUTTON_LEFTSHOULDER, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
    SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN,
    SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
    SDL_CONTROLLER_BUTTON_MAX
} SDL_GameControllerButton;

typedef enum {
    SDL_CONTROLLER_AXIS_INVALID = -1,
    SDL_CONTROLLER_AXIS_LEFTX, SDL_CONTROLLER_AXIS_LEFTY,
    SDL_CONTROLLER_AXIS_RIGHTX, SDL_CONTROLLER_AXIS_RIGHTY,
    SDL_CONTROLLER_AXIS_TRIGGERLEFT, SDL_CONTROLLER_AXIS_TRIGGERRIGHT,
    SDL_CONTROLLER_AXIS_MAX
} SDL_GameControllerAxis;

typedef struct {
    Uint32 type;
    Uint32 timestamp;
    SDL_JoystickID which;
    Uint8 button;
    Uint8 state;
} SDL_ControllerButtonEvent;

typedef struct {
    Uint32 type;
    Uint32 timestamp;
    SDL_JoystickID which;
    Uint8 axis;
    Sint16 value;
} SDL_ControllerAxisEvent;

typedef struct {
    Uint32 type;
    Uint32 timestamp;
    SDL_JoystickID which;
    Uint8 hat;
    Uint8 value;
} SDL_JoyHatEvent;

typedef struct {
    Uint32 type;
    Uint32 timestamp;
    SDL_JoystickID which;
} SDL_ControllerDeviceEvent;

typedef struct {
    Uint32 type;
    Uint32 timestamp;
    SDL_JoystickID which;
} SDL_JoyDeviceEvent;

/* No gamepad support — all stubs */
static inline int SDL_IsGameController(int idx) { (void)idx; return 0; }
static inline SDL_GameController* SDL_GameControllerOpen(int idx) { (void)idx; return (SDL_GameController*)0; }
static inline void SDL_GameControllerClose(SDL_GameController *gc) { (void)gc; }
static inline const char* SDL_GameControllerName(SDL_GameController *gc) { (void)gc; return ""; }
static inline SDL_bool SDL_GameControllerHasButton(SDL_GameController *gc, SDL_GameControllerButton b)
    { (void)gc; (void)b; return SDL_FALSE; }
static inline Uint8 SDL_GameControllerGetButton(SDL_GameController *gc, SDL_GameControllerButton b)
    { (void)gc; (void)b; return 0; }
static inline Sint16 SDL_GameControllerGetAxis(SDL_GameController *gc, SDL_GameControllerAxis axis)
    { (void)gc; (void)axis; return 0; }
static inline SDL_bool SDL_GameControllerHasAxis(SDL_GameController *gc, SDL_GameControllerAxis axis)
    { (void)gc; (void)axis; return SDL_FALSE; }
static inline SDL_GameController* SDL_GameControllerFromInstanceID(SDL_JoystickID id)
    { (void)id; return (SDL_GameController*)0; }
static inline SDL_Joystick* SDL_GameControllerGetJoystick(SDL_GameController *gc)
    { (void)gc; return (SDL_Joystick*)0; }

#endif
