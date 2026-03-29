#ifndef SDL_h_
#define SDL_h_

#include "SDL_stdinc.h"
#include "SDL_error.h"
#include "SDL_rect.h"
#include "SDL_pixels.h"
#include "SDL_surface.h"
#include "SDL_video.h"
#include "SDL_render.h"
#include "SDL_events.h"
#include "SDL_keycode.h"
#include "SDL_scancode.h"
#include "SDL_timer.h"
#include "SDL_version.h"
#include "SDL_endian.h"
#include "SDL_mouse.h"
#include "SDL_mutex.h"
#include "SDL_thread.h"
#include "SDL_log.h"
#include "SDL_joystick.h"
#include "SDL_main.h"

#ifdef __cplusplus
extern "C" {
#endif


/* SDL_Init subsystem flags */
#define SDL_INIT_TIMER          0x00000001
#define SDL_INIT_AUDIO          0x00000010
#define SDL_INIT_VIDEO          0x00000020
#define SDL_INIT_EVENTS         0x00004000
#define SDL_INIT_JOYSTICK       0x00000200
#define SDL_INIT_GAMECONTROLLER 0x00002000
#define SDL_INIT_HAPTIC         0x00001000
#define SDL_INIT_NOPARACHUTE    0x00100000
#define SDL_INIT_EVERYTHING     0x0000FFFF

int  SDL_Init(Uint32 flags);
void SDL_Quit(void);
static inline Uint32 SDL_WasInit(Uint32 flags) { (void)flags; return SDL_INIT_EVERYTHING; }

/* Message box */
#define SDL_MESSAGEBOX_ERROR       0x00000010
#define SDL_MESSAGEBOX_WARNING     0x00000020
#define SDL_MESSAGEBOX_INFORMATION 0x00000040
static inline int SDL_ShowSimpleMessageBox(Uint32 flags, const char *title, const char *msg, SDL_Window *w)
    { (void)flags; (void)title; (void)msg; (void)w; return 0; }

/* Window surface — implemented in sdl2.c (declared in SDL_video.h) */

/* Clipboard stubs */
static inline int SDL_SetClipboardText(const char *text) { (void)text; return 0; }
static inline char* SDL_GetClipboardText(void) { return (char*)""; }
static inline SDL_bool SDL_HasClipboardText(void) { return SDL_FALSE; }
static inline void SDL_free(void *ptr) { (void)ptr; }

/* Hints */
static inline SDL_bool SDL_SetHint(const char *name, const char *value)
    { (void)name; (void)value; return SDL_TRUE; }
#define SDL_HINT_ORIENTATIONS "SDL_HINT_ORIENTATIONS"
#define SDL_HINT_IME_INTERNAL_EDITING "SDL_IME_INTERNAL_EDITING"
#define SDL_HINT_GAMECONTROLLERCONFIG "SDL_GAMECONTROLLERCONFIG"
#define SDL_HINT_MOUSE_TOUCH_EVENTS "SDL_MOUSE_TOUCH_EVENTS"
#define SDL_HINT_ACCELEROMETER_AS_JOYSTICK "SDL_ACCELEROMETER_AS_JOYSTICK"
#define SDL_HINT_RENDER_VSYNC "SDL_RENDER_VSYNC"
#define SDL_HINT_RENDER_SCALE_QUALITY "SDL_RENDER_SCALE_QUALITY"
#define SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS "SDL_GAMECONTROLLER_USE_BUTTON_LABELS"
#define SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS "SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS"

/* Audio */
typedef Uint32 SDL_AudioDeviceID;
typedef Uint16 SDL_AudioFormat;
#define AUDIO_U8     0x0008
#define AUDIO_S8     0x8008
#define AUDIO_S16SYS 0x8010
#define SDL_AUDIO_ALLOW_ANY_CHANGE 0

typedef struct SDL_AudioSpec {
    int freq;
    SDL_AudioFormat format;
    Uint8 channels;
    Uint8 silence;
    Uint16 samples;
    Uint32 size;
    void (*callback)(void *userdata, Uint8 *stream, int len);
    void *userdata;
} SDL_AudioSpec;

SDL_AudioDeviceID SDL_OpenAudioDevice(const char *device, int iscapture,
    const SDL_AudioSpec *desired, SDL_AudioSpec *obtained, int allowed_changes);
void SDL_CloseAudioDevice(SDL_AudioDeviceID dev);
int  SDL_QueueAudio(SDL_AudioDeviceID dev, const void *data, Uint32 len);
Uint32 SDL_GetQueuedAudioSize(SDL_AudioDeviceID dev);
void SDL_ClearQueuedAudio(SDL_AudioDeviceID dev);
void SDL_PauseAudioDevice(SDL_AudioDeviceID dev, int pause_on);

static inline int SDL_GetNumAudioDevices(int iscapture) { (void)iscapture; return 1; }
static inline const char* SDL_GetAudioDeviceName(int idx, int iscapture) { (void)idx; (void)iscapture; return "potatOS AC97"; }

/* SDL_PushEvent declared in SDL_events.h */
/* SDL_GetKeyboardState declared in SDL_events.h */

/* Text input stubs */
static inline void SDL_StartTextInput(void) {}
static inline void SDL_StopTextInput(void) {}
static inline void SDL_SetTextInputRect(const SDL_Rect *rect) { (void)rect; }

/* Query constants for SDL_ShowCursor etc. */
#define SDL_QUERY   -1
#define SDL_DISABLE  0
#define SDL_ENABLE   1

/* SDL_RWops — minimal for DevilutionX file I/O */
typedef struct SDL_RWops {
    Sint64 (*size)(struct SDL_RWops *ctx);
    Sint64 (*seek)(struct SDL_RWops *ctx, Sint64 offset, int whence);
    size_t (*read)(struct SDL_RWops *ctx, void *ptr, size_t size, size_t maxnum);
    size_t (*write)(struct SDL_RWops *ctx, const void *ptr, size_t size, size_t num);
    int    (*close)(struct SDL_RWops *ctx);
    Uint32 type;
    union {
        struct { void *data1; void *data2; } unknown;
        struct { void *fp; int autoclose; } stdio;
        struct { Uint8 *base; Uint8 *here; Uint8 *stop; } mem;
    } hidden;
} SDL_RWops;

SDL_RWops* SDL_RWFromFile(const char *file, const char *mode);
SDL_RWops* SDL_RWFromMem(void *mem, int size);
SDL_RWops* SDL_RWFromConstMem(const void *mem, int size);
Sint64     SDL_RWsize(SDL_RWops *ctx);
Sint64     SDL_RWseek(SDL_RWops *ctx, Sint64 offset, int whence);
size_t     SDL_RWread(SDL_RWops *ctx, void *ptr, size_t size, size_t maxnum);
size_t     SDL_RWwrite(SDL_RWops *ctx, const void *ptr, size_t size, size_t num);
int        SDL_RWclose(SDL_RWops *ctx);
SDL_RWops* SDL_AllocRW(void);
void       SDL_FreeRW(SDL_RWops *area);

int        SDL_SaveBMP_RW(SDL_Surface *surface, SDL_RWops *dst, int freedst);
SDL_Surface* SDL_LoadBMP_RW(SDL_RWops *src, int freesrc);

/* RWops seek constants */
#define RW_SEEK_SET 0
#define RW_SEEK_CUR 1
#define RW_SEEK_END 2
#define SDL_RWOPS_UNKNOWN 0

/* Haptic stubs */
typedef struct SDL_Haptic SDL_Haptic;
static inline SDL_Haptic* SDL_HapticOpen(int idx) { (void)idx; return (SDL_Haptic*)0; }
static inline void SDL_HapticClose(SDL_Haptic *h) { (void)h; }
static inline int SDL_HapticRumbleInit(SDL_Haptic *h) { (void)h; return -1; }
static inline int SDL_HapticRumblePlay(SDL_Haptic *h, float strength, Uint32 len)
    { (void)h; (void)strength; (void)len; return -1; }
static inline int SDL_HapticRumbleStop(SDL_Haptic *h) { (void)h; return 0; }

/* Misc */
static inline const char* SDL_GetPlatform(void) { return "potatOS"; }
static inline int SDL_GetCPUCount(void) { return 1; }
static inline Uint32 SDL_GetWindowFlags(SDL_Window *w) { (void)w; return SDL_WINDOW_SHOWN; }
static inline int SDL_GetWindowDisplayIndex(SDL_Window *w) { (void)w; return 0; }
static inline int SDL_GetDisplayBounds(int idx, SDL_Rect *r)
    { (void)idx; if (r) { r->x=0; r->y=0; r->w=1024; r->h=768; } return 0; }
static inline void SDL_SetWindowSize(SDL_Window *w, int width, int height)
    { (void)w; (void)width; (void)height; }
static inline void SDL_SetWindowPosition(SDL_Window *w, int x, int y)
    { (void)w; (void)x; (void)y; }
static inline void SDL_GetWindowPosition(SDL_Window *w, int *x, int *y)
    { (void)w; if (x) *x = 50; if (y) *y = 50; }
static inline int SDL_SetWindowFullscreen(SDL_Window *w, Uint32 flags)
    { (void)w; (void)flags; return 0; }
static inline void SDL_SetWindowGrab(SDL_Window *w, SDL_bool grabbed)
    { (void)w; (void)grabbed; }
static inline void SDL_ShowWindow(SDL_Window *w) { (void)w; }
static inline void SDL_HideWindow(SDL_Window *w) { (void)w; }
static inline void SDL_RaiseWindow(SDL_Window *w) { (void)w; }
static inline void SDL_MaximizeWindow(SDL_Window *w) { (void)w; }
static inline void SDL_RestoreWindow(SDL_Window *w) { (void)w; }
static inline char* SDL_GetBasePath(void) { return (char*)"GAMES/DIABLO/"; }
static inline char* SDL_GetPrefPath(const char *org, const char *app) { (void)org; (void)app; return (char*)"GAMES/DIABLO/"; }
static inline void SDL_DisableScreenSaver(void) {}
static inline void SDL_EnableScreenSaver(void) {}
static inline int SDL_GetDisplayDPI(int idx, float *ddpi, float *hdpi, float *vdpi)
    { (void)idx; if(ddpi)*ddpi=96.0f; if(hdpi)*hdpi=96.0f; if(vdpi)*vdpi=96.0f; return 0; }


#ifdef __cplusplus
}
#endif

#endif
