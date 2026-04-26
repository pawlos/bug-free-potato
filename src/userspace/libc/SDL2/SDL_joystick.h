#ifndef SDL_joystick_h_
#define SDL_joystick_h_

#include "SDL_stdinc.h"

#ifdef __cplusplus
extern "C" {
#endif


/* potatOS has no joystick/gamepad support — minimal stubs. */

typedef Sint32 SDL_JoystickID;
typedef struct SDL_Joystick SDL_Joystick;

static inline int SDL_NumJoysticks(void) { return 0; }
static inline SDL_Joystick* SDL_JoystickOpen(int index) { (void)index; return (SDL_Joystick*)0; }
static inline void SDL_JoystickClose(SDL_Joystick *j) { (void)j; }
static inline const char* SDL_JoystickName(SDL_Joystick *j) { (void)j; return ""; }
static inline SDL_JoystickID SDL_JoystickInstanceID(SDL_Joystick *j) { (void)j; return -1; }
static inline int SDL_JoystickNumButtons(SDL_Joystick *j) { (void)j; return 0; }
static inline int SDL_JoystickNumAxes(SDL_Joystick *j) { (void)j; return 0; }
static inline int SDL_JoystickNumHats(SDL_Joystick *j) { (void)j; return 0; }
static inline Uint8 SDL_JoystickGetButton(SDL_Joystick *j, int btn) { (void)j; (void)btn; return 0; }
static inline Sint16 SDL_JoystickGetAxis(SDL_Joystick *j, int axis) { (void)j; (void)axis; return 0; }
static inline Uint8 SDL_JoystickGetHat(SDL_Joystick *j, int hat) { (void)j; (void)hat; return 0; }

/* Hat positions */
#define SDL_HAT_CENTERED  0x00
#define SDL_HAT_UP        0x01
#define SDL_HAT_RIGHT     0x02
#define SDL_HAT_DOWN      0x04
#define SDL_HAT_LEFT      0x08

typedef struct { Uint8 data[16]; } SDL_JoystickGUID;
static inline SDL_JoystickGUID SDL_JoystickGetGUID(SDL_Joystick *j)
    { SDL_JoystickGUID g = {{0}}; (void)j; return g; }
static inline SDL_JoystickGUID SDL_JoystickGetDeviceGUID(int idx)
    { SDL_JoystickGUID g = {{0}}; (void)idx; return g; }
static inline const char* SDL_JoystickNameForIndex(int idx) { (void)idx; return ""; }
static inline void SDL_JoystickUpdate(void) {}
static inline int  SDL_JoystickEventState(int state) { (void)state; return 0; }


#ifdef __cplusplus
}
#endif

#endif
