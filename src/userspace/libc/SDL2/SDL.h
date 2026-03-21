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

/* SDL_Init subsystem flags */
#define SDL_INIT_TIMER          0x00000001
#define SDL_INIT_AUDIO          0x00000010
#define SDL_INIT_VIDEO          0x00000020
#define SDL_INIT_EVENTS         0x00004000
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

/* Window surface (stub) */
static inline SDL_Surface* SDL_GetWindowSurface(SDL_Window *w) { (void)w; return (SDL_Surface*)0; }
static inline int SDL_UpdateWindowSurface(SDL_Window *w) { (void)w; return 0; }

/* Clipboard stubs */
static inline int SDL_SetClipboardText(const char *text) { (void)text; return 0; }
static inline char* SDL_GetClipboardText(void) { return (char*)""; }
static inline SDL_bool SDL_HasClipboardText(void) { return SDL_FALSE; }
static inline void SDL_free(void *ptr) { (void)ptr; }

/* Hints */
static inline SDL_bool SDL_SetHint(const char *name, const char *value)
    { (void)name; (void)value; return SDL_TRUE; }
#define SDL_HINT_ORIENTATIONS "SDL_HINT_ORIENTATIONS"

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
    void  *hidden;
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
static inline char* SDL_GetBasePath(void) { return (char*)""; }
static inline char* SDL_GetPrefPath(const char *org, const char *app) { (void)org; (void)app; return (char*)""; }

#endif
