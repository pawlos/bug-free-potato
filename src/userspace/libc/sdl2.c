/*
 * sdl2.c — Minimal SDL2 shim for potat OS.
 *
 * Maps SDL2 video, events, and timer calls to potatOS syscalls.
 * Only implements functions that games actually use.
 */

#include "SDL2/SDL.h"
#include "syscall.h"
#include "string.h"
#include "stdlib.h"
#include "file.h"

/* Define SDL_DEBUG_TRACE to enable serial debug output for rendering pipeline */
/* #define SDL_DEBUG_TRACE */
#ifdef SDL_DEBUG_TRACE
extern int serial_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#define sdl_trace(...) serial_printf(__VA_ARGS__)
#else
#define sdl_trace(...) ((void)0)
#endif

/* Forward declaration for SDL_GetKeyboardState / SDL_PollEvent */
static Uint8 g_keyboard_state[512];

#define STBI_NO_THREAD_LOCALS
#define STBI_NO_SIMD
#define STBI_NO_STDIO
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

/* ── Error ──────────────────────────────────────────────────────────────── */

static char sdl_error_buf[256] = "";

const char* SDL_GetError(void) { return sdl_error_buf; }

int SDL_SetError(const char *fmt, ...)
{
    /* Simplified: just copy the format string (no varargs formatting) */
    int i = 0;
    while (fmt[i] && i < 255) { sdl_error_buf[i] = fmt[i]; i++; }
    sdl_error_buf[i] = '\0';
    return -1;
}

void SDL_ClearError(void) { sdl_error_buf[0] = '\0'; }

/* ── Internal structures ────────────────────────────────────────────────── */

struct SDL_Window {
    long   wid;         /* potatOS window ID */
    int    w, h;        /* client dimensions */
    char   title[32];
};

struct SDL_Renderer {
    SDL_Window *window;
    Uint8  draw_r, draw_g, draw_b, draw_a;
    /* Conversion buffer: ARGB32 → RGB24 for sys_draw_pixels */
    unsigned char *rgb24_buf;
    int rgb24_w, rgb24_h;
};

struct SDL_Texture {
    int    w, h;
    Uint32 format;
    Uint32 *pixels;     /* ARGB8888 pixel buffer */
    int    pitch;       /* bytes per row */
};

/* One window at a time (potatOS constraint for simple games) */
static SDL_Window   g_window;
static SDL_Renderer g_renderer;
static int g_initialized = 0;

/* ── SDL_Init / SDL_Quit ────────────────────────────────────────────────── */

int SDL_Init(Uint32 flags)
{
    (void)flags;
    g_initialized = 1;
    return 0;
}

void SDL_Quit(void)
{
    g_initialized = 0;
}

/* ── Timer ──────────────────────────────────────────────────────────────── */

Uint32 SDL_GetTicks(void)
{
    return (Uint32)(sys_get_micros() / 1000);
}

void SDL_Delay(Uint32 ms)
{
    sys_sleep_ms(ms);
}

/* ── Window ─────────────────────────────────────────────────────────────── */

SDL_Window* SDL_CreateWindow(const char *title, int x, int y, int w, int h,
                             Uint32 flags)
{
    (void)x; (void)y; (void)flags;
    long wid = sys_create_window(50, 50, w, h);
    if (wid < 0) {
        SDL_SetError("Failed to create window");
        return (SDL_Window*)0;
    }
    g_window.wid = wid;
    g_window.w = w;
    g_window.h = h;
    /* Copy title */
    int i = 0;
    while (title && title[i] && i < 31) { g_window.title[i] = title[i]; i++; }
    g_window.title[i] = '\0';
    sys_set_window_title(wid, g_window.title);
    sdl_trace("[SDL] CreateWindow: '%s' %dx%d wid=%ld\n", g_window.title, w, h, wid);
    return &g_window;
}

void SDL_DestroyWindow(SDL_Window *window)
{
    if (window) sys_destroy_window(window->wid);
}

void SDL_GetWindowSize(SDL_Window *window, int *w, int *h)
{
    if (w) *w = window ? window->w : 0;
    if (h) *h = window ? window->h : 0;
}

void SDL_SetWindowTitle(SDL_Window *window, const char *title)
{
    if (window) sys_set_window_title(window->wid, title);
}

Uint32 SDL_GetWindowID(SDL_Window *window)
{
    return window ? (Uint32)window->wid : 0;
}

/* ── Renderer ───────────────────────────────────────────────────────────── */

SDL_Renderer* SDL_CreateRenderer(SDL_Window *window, int index, Uint32 flags)
{
    (void)index; (void)flags;
    if (!window) return (SDL_Renderer*)0;
    g_renderer.window = window;
    g_renderer.draw_r = 0;
    g_renderer.draw_g = 0;
    g_renderer.draw_b = 0;
    g_renderer.draw_a = 255;
    g_renderer.rgb24_buf = (unsigned char*)0;
    g_renderer.rgb24_w = 0;
    g_renderer.rgb24_h = 0;
    return &g_renderer;
}

void SDL_DestroyRenderer(SDL_Renderer *renderer)
{
    if (renderer && renderer->rgb24_buf) {
        free(renderer->rgb24_buf);
        renderer->rgb24_buf = (unsigned char*)0;
    }
}

int SDL_SetRenderDrawColor(SDL_Renderer *r, Uint8 red, Uint8 green,
                           Uint8 blue, Uint8 alpha)
{
    if (!r) return -1;
    r->draw_r = red;
    r->draw_g = green;
    r->draw_b = blue;
    r->draw_a = alpha;
    return 0;
}

int SDL_RenderClear(SDL_Renderer *renderer)
{
    if (!renderer || !renderer->window) return -1;
    long rgb = ((long)renderer->draw_r << 16) |
               ((long)renderer->draw_g << 8)  |
               (long)renderer->draw_b;
    sys_fill_rect(0, 0, renderer->window->w, renderer->window->h, rgb);
    return 0;
}

int SDL_RenderFillRect(SDL_Renderer *renderer, const SDL_Rect *rect)
{
    if (!renderer) return -1;
    long rgb = ((long)renderer->draw_r << 16) |
               ((long)renderer->draw_g << 8)  |
               (long)renderer->draw_b;
    if (!rect)
        sys_fill_rect(0, 0, renderer->window->w, renderer->window->h, rgb);
    else
        sys_fill_rect(rect->x, rect->y, rect->w, rect->h, rgb);
    return 0;
}

/* Ensure the RGB24 conversion buffer is large enough */
static void ensure_rgb24_buf(SDL_Renderer *r, int w, int h)
{
    if (r->rgb24_buf && r->rgb24_w == w && r->rgb24_h == h) return;
    if (r->rgb24_buf) free(r->rgb24_buf);
    r->rgb24_buf = (unsigned char*)malloc(w * h * 3);
    r->rgb24_w = w;
    r->rgb24_h = h;
}

int SDL_RenderCopy(SDL_Renderer *renderer, SDL_Texture *texture,
                   const SDL_Rect *srcrect, const SDL_Rect *dstrect)
{
    if (!renderer || !texture || !texture->pixels) return -1;

    /* Source rect defaults to full texture */
    int sx = 0, sy = 0, sw = texture->w, sh = texture->h;
    if (srcrect) { sx = srcrect->x; sy = srcrect->y; sw = srcrect->w; sh = srcrect->h; }

    /* Dest rect defaults to full window */
    int dx = 0, dy = 0, dw = renderer->window->w, dh = renderer->window->h;
    if (dstrect) { dx = dstrect->x; dy = dstrect->y; dw = dstrect->w; dh = dstrect->h; }

    /* For simplicity: blit src to dest without scaling if sizes match,
       otherwise do nearest-neighbor scaling */
    ensure_rgb24_buf(renderer, dw, dh);
    if (!renderer->rgb24_buf) return -1;

    int src_pitch_px = texture->pitch / 4;

    for (int y = 0; y < dh; y++) {
        int ty = sy + (sh != dh ? (y * sh / dh) : y);
        if (ty >= texture->h) ty = texture->h - 1;
        for (int x = 0; x < dw; x++) {
            int tx = sx + (sw != dw ? (x * sw / dw) : x);
            if (tx >= texture->w) tx = texture->w - 1;
            Uint32 px = texture->pixels[ty * src_pitch_px + tx];
            unsigned char *dst = renderer->rgb24_buf + (y * dw + x) * 3;
            dst[0] = (px >> 16) & 0xFF;  /* R */
            dst[1] = (px >> 8)  & 0xFF;  /* G */
            dst[2] = px & 0xFF;          /* B */
        }
    }

    sys_draw_pixels(renderer->rgb24_buf, dx, dy, dw, dh);

    static int rc_trace = 0;
    if (rc_trace < 30) {
        rc_trace++;
        /* Count non-black pixels in texture */
        int nonblack = 0;
        int total = texture->w * texture->h;
        for (int _i = 0; _i < total && _i < 50000; _i++) {
            Uint32 p = texture->pixels[_i];
            if ((p & 0x00FFFFFF) != 0) { nonblack++; }
        }
        sdl_trace("[SDL] RenderCopy #%d: tex=%dx%d nonblack=%d/%d\n",
                  rc_trace, texture->w, texture->h, nonblack,
                  total < 50000 ? total : 50000);
    }

    return 0;
}

void SDL_RenderPresent(SDL_Renderer *renderer)
{
    /* All drawing is immediate in our implementation — this is a no-op.
       Games that do all rendering via RenderCopy get their pixels pushed
       during that call. */
    (void)renderer;
}

/* ── Texture ────────────────────────────────────────────────────────────── */

SDL_Texture* SDL_CreateTexture(SDL_Renderer *renderer, Uint32 format,
                               int access, int w, int h)
{
    (void)renderer; (void)access;
    SDL_Texture *tex = (SDL_Texture*)calloc(1, sizeof(SDL_Texture));
    if (!tex) return (SDL_Texture*)0;
    tex->w = w;
    tex->h = h;
    tex->format = format;
    tex->pitch = w * 4;
    tex->pixels = (Uint32*)calloc(w * h, sizeof(Uint32));
    if (!tex->pixels) { free(tex); return (SDL_Texture*)0; }
    return tex;
}

void SDL_DestroyTexture(SDL_Texture *texture)
{
    if (!texture) return;
    if (texture->pixels) free(texture->pixels);
    free(texture);
}

int SDL_QueryTexture(SDL_Texture *t, Uint32 *fmt, int *access, int *w, int *h)
{
    if (!t) return -1;
    if (fmt) *fmt = t->format;
    if (access) *access = 0;
    if (w) *w = t->w;
    if (h) *h = t->h;
    return 0;
}

/* Global palette for 8-bit→ARGB conversion in SDL_UpdateTexture.
   Updated whenever SDL_SetPaletteColors is called. */
static SDL_Palette *g_active_palette = (SDL_Palette*)0;

int SDL_UpdateTexture(SDL_Texture *texture, const SDL_Rect *rect,
                      const void *pixels, int pitch)
{
    if (!texture || !pixels) return -1;
    const unsigned char *src = (const unsigned char *)pixels;

    int tw = rect ? rect->w : texture->w;
    int th = rect ? rect->h : texture->h;
    int dx = rect ? rect->x : 0;
    int dy = rect ? rect->y : 0;

    int src_bpp = (tw > 0) ? pitch / tw : 4;  /* detect source bytes per pixel */

    static int ut_trace = 0;
    if (ut_trace < 20) {
        ut_trace++;
        /* Sample source pixels for non-black */
        int nonblack = 0;
        const Uint32 *sp32 = (const Uint32*)pixels;
        int total32 = (pitch / 4) * th;
        for (int _i = 0; _i < total32 && _i < 50000; _i++) {
            if ((sp32[_i] & 0x00FFFFFF) != 0) nonblack++;
        }
        sdl_trace("[SDL] UpdateTexture #%d: %dx%d pitch=%d bpp=%d nonblack=%d\n",
                  ut_trace, tw, th, pitch, src_bpp, nonblack);
    }

    if (src_bpp == 1 && g_active_palette && g_active_palette->ncolors > 0) {
        /* 8-bit indexed → ARGB8888 via palette lookup */
        for (int y = 0; y < th; y++) {
            const Uint8 *row = src + y * pitch;
            Uint32 *dst = texture->pixels + (dy + y) * texture->w + dx;
            for (int x = 0; x < tw; x++) {
                int idx = row[x];
                if (idx < g_active_palette->ncolors) {
                    SDL_Color c = g_active_palette->colors[idx];
                    dst[x] = 0xFF000000 | ((Uint32)c.r << 16) |
                             ((Uint32)c.g << 8) | c.b;
                } else {
                    dst[x] = 0xFF000000;
                }
            }
        }
        return 0;
    }

    if (src_bpp == 3) {
        /* 24-bit BGR (little-endian Rmsk=0xFF) → ARGB8888 */
        for (int y = 0; y < th; y++) {
            const Uint8 *row = src + y * pitch;
            Uint32 *dst = texture->pixels + (dy + y) * texture->w + dx;
            for (int x = 0; x < tw; x++) {
                /* In memory: byte0=R(low mask), byte1=G, byte2=B(high mask) on LE */
                Uint8 lo = row[x * 3 + 0];  /* Rmask=0xFF → R */
                Uint8 md = row[x * 3 + 1];  /* Gmask=0xFF00 → G */
                Uint8 hi = row[x * 3 + 2];  /* Bmask=0xFF0000 → B */
                dst[x] = 0xFF000000 | ((Uint32)lo << 16) | ((Uint32)md << 8) | hi;
            }
        }
        return 0;
    }

    if (!rect) {
        /* Full texture update (32-bit → 32-bit) */
        for (int y = 0; y < texture->h; y++) {
            memcpy(texture->pixels + y * texture->w,
                   src + y * pitch, texture->w * 4);
        }
    } else {
        for (int y = 0; y < rect->h; y++) {
            memcpy(texture->pixels + (rect->y + y) * texture->w + rect->x,
                   src + y * pitch, rect->w * 4);
        }
    }
    return 0;
}

int SDL_LockTexture(SDL_Texture *texture, const SDL_Rect *rect,
                    void **pixels, int *pitch)
{
    (void)rect;
    if (!texture) return -1;
    if (pixels) *pixels = texture->pixels;
    if (pitch) *pitch = texture->pitch;
    return 0;
}

void SDL_UnlockTexture(SDL_Texture *texture)
{
    (void)texture;
}

/* ── PS/2 set-1 → SDL scancode mapping ──────────────────────────────────── */

static SDL_Scancode ps2_to_sdl_scancode(int ps2)
{
    switch (ps2) {
    case 0x01: return SDL_SCANCODE_ESCAPE;
    case 0x02: return SDL_SCANCODE_1;
    case 0x03: return SDL_SCANCODE_2;
    case 0x04: return SDL_SCANCODE_3;
    case 0x05: return SDL_SCANCODE_4;
    case 0x06: return SDL_SCANCODE_5;
    case 0x07: return SDL_SCANCODE_6;
    case 0x08: return SDL_SCANCODE_7;
    case 0x09: return SDL_SCANCODE_8;
    case 0x0A: return SDL_SCANCODE_9;
    case 0x0B: return SDL_SCANCODE_0;
    case 0x0C: return SDL_SCANCODE_MINUS;
    case 0x0D: return SDL_SCANCODE_EQUALS;
    case 0x0E: return SDL_SCANCODE_BACKSPACE;
    case 0x0F: return SDL_SCANCODE_TAB;
    case 0x10: return SDL_SCANCODE_Q;
    case 0x11: return SDL_SCANCODE_W;
    case 0x12: return SDL_SCANCODE_E;
    case 0x13: return SDL_SCANCODE_R;
    case 0x14: return SDL_SCANCODE_T;
    case 0x15: return SDL_SCANCODE_Y;
    case 0x16: return SDL_SCANCODE_U;
    case 0x17: return SDL_SCANCODE_I;
    case 0x18: return SDL_SCANCODE_O;
    case 0x19: return SDL_SCANCODE_P;
    case 0x1A: return SDL_SCANCODE_LEFTBRACKET;
    case 0x1B: return SDL_SCANCODE_RIGHTBRACKET;
    case 0x1C: return SDL_SCANCODE_RETURN;
    case 0x1D: return SDL_SCANCODE_LCTRL;
    case 0x1E: return SDL_SCANCODE_A;
    case 0x1F: return SDL_SCANCODE_S;
    case 0x20: return SDL_SCANCODE_D;
    case 0x21: return SDL_SCANCODE_F;
    case 0x22: return SDL_SCANCODE_G;
    case 0x23: return SDL_SCANCODE_H;
    case 0x24: return SDL_SCANCODE_J;
    case 0x25: return SDL_SCANCODE_K;
    case 0x26: return SDL_SCANCODE_L;
    case 0x27: return SDL_SCANCODE_SEMICOLON;
    case 0x28: return SDL_SCANCODE_APOSTROPHE;
    case 0x29: return SDL_SCANCODE_GRAVE;
    case 0x2A: return SDL_SCANCODE_LSHIFT;
    case 0x2B: return SDL_SCANCODE_BACKSLASH;
    case 0x2C: return SDL_SCANCODE_Z;
    case 0x2D: return SDL_SCANCODE_X;
    case 0x2E: return SDL_SCANCODE_C;
    case 0x2F: return SDL_SCANCODE_V;
    case 0x30: return SDL_SCANCODE_B;
    case 0x31: return SDL_SCANCODE_N;
    case 0x32: return SDL_SCANCODE_M;
    case 0x33: return SDL_SCANCODE_COMMA;
    case 0x34: return SDL_SCANCODE_PERIOD;
    case 0x35: return SDL_SCANCODE_SLASH;
    case 0x36: return SDL_SCANCODE_RSHIFT;
    case 0x38: return SDL_SCANCODE_LALT;
    case 0x39: return SDL_SCANCODE_SPACE;
    case 0x3A: return SDL_SCANCODE_CAPSLOCK;
    case 0x3B: return SDL_SCANCODE_F1;
    case 0x3C: return SDL_SCANCODE_F2;
    case 0x3D: return SDL_SCANCODE_F3;
    case 0x3E: return SDL_SCANCODE_F4;
    case 0x3F: return SDL_SCANCODE_F5;
    case 0x40: return SDL_SCANCODE_F6;
    case 0x41: return SDL_SCANCODE_F7;
    case 0x42: return SDL_SCANCODE_F8;
    case 0x43: return SDL_SCANCODE_F9;
    case 0x44: return SDL_SCANCODE_F10;
    case 0x57: return SDL_SCANCODE_F11;
    case 0x58: return SDL_SCANCODE_F12;
    case 0x48: return SDL_SCANCODE_UP;
    case 0x4B: return SDL_SCANCODE_LEFT;
    case 0x4D: return SDL_SCANCODE_RIGHT;
    case 0x50: return SDL_SCANCODE_DOWN;
    default:   return SDL_SCANCODE_UNKNOWN;
    }
}

/* Map SDL scancode to SDL keycode */
static SDL_Keycode scancode_to_keycode(SDL_Scancode sc)
{
    /* Letters → lowercase ASCII */
    if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z)
        return 'a' + (sc - SDL_SCANCODE_A);
    /* Numbers */
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9)
        return '1' + (sc - SDL_SCANCODE_1);
    if (sc == SDL_SCANCODE_0) return '0';
    /* Special keys */
    switch (sc) {
    case SDL_SCANCODE_RETURN:    return SDLK_RETURN;
    case SDL_SCANCODE_ESCAPE:    return SDLK_ESCAPE;
    case SDL_SCANCODE_BACKSPACE: return SDLK_BACKSPACE;
    case SDL_SCANCODE_TAB:       return SDLK_TAB;
    case SDL_SCANCODE_SPACE:     return SDLK_SPACE;
    case SDL_SCANCODE_MINUS:     return '-';
    case SDL_SCANCODE_EQUALS:    return '=';
    case SDL_SCANCODE_LEFTBRACKET:  return '[';
    case SDL_SCANCODE_RIGHTBRACKET: return ']';
    case SDL_SCANCODE_BACKSLASH:    return '\\';
    case SDL_SCANCODE_SEMICOLON:    return ';';
    case SDL_SCANCODE_APOSTROPHE:   return '\'';
    case SDL_SCANCODE_GRAVE:        return '`';
    case SDL_SCANCODE_COMMA:        return ',';
    case SDL_SCANCODE_PERIOD:       return '.';
    case SDL_SCANCODE_SLASH:        return '/';
    default:
        /* F-keys, arrows, modifiers → use SCANCODE_TO_KEYCODE */
        return SDL_SCANCODE_TO_KEYCODE(sc);
    }
}

/* ── Text Input ────────────────────────────────────────────────────────── */

static int g_text_input_enabled = 0;

void SDL_StartTextInput(void)  { g_text_input_enabled = 1; }
void SDL_StopTextInput(void)   { g_text_input_enabled = 0; }

/* Pending text input event (generated alongside SDL_KEYDOWN) */
static int g_pending_textinput = 0;
static SDL_Event g_pending_textinput_ev;

/* Convert SDL scancode + modifiers to a printable ASCII character, or 0 */
static char scancode_to_char(SDL_Scancode sc, Uint16 mod)
{
    int shift = (mod & (KMOD_LSHIFT | KMOD_RSHIFT)) != 0;
    /* Letters */
    if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) {
        char c = 'a' + (sc - SDL_SCANCODE_A);
        return shift ? (c - 32) : c;
    }
    /* Number row */
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) {
        if (!shift) return '1' + (sc - SDL_SCANCODE_1);
        static const char shifted[] = "!@#$%^&*(";
        return shifted[sc - SDL_SCANCODE_1];
    }
    if (sc == SDL_SCANCODE_0) return shift ? ')' : '0';
    /* Space */
    if (sc == SDL_SCANCODE_SPACE) return ' ';
    /* Punctuation */
    switch (sc) {
    case SDL_SCANCODE_MINUS:        return shift ? '_' : '-';
    case SDL_SCANCODE_EQUALS:       return shift ? '+' : '=';
    case SDL_SCANCODE_LEFTBRACKET:  return shift ? '{' : '[';
    case SDL_SCANCODE_RIGHTBRACKET: return shift ? '}' : ']';
    case SDL_SCANCODE_BACKSLASH:    return shift ? '|' : '\\';
    case SDL_SCANCODE_SEMICOLON:    return shift ? ':' : ';';
    case SDL_SCANCODE_APOSTROPHE:   return shift ? '"' : '\'';
    case SDL_SCANCODE_GRAVE:        return shift ? '~' : '`';
    case SDL_SCANCODE_COMMA:        return shift ? '<' : ',';
    case SDL_SCANCODE_PERIOD:       return shift ? '>' : '.';
    case SDL_SCANCODE_SLASH:        return shift ? '?' : '/';
    default: return 0;
    }
}

/* ── Events ─────────────────────────────────────────────────────────────── */

/* Modifier key tracking */
static Uint16 g_key_modifiers = 0;

static void update_modifiers(SDL_Scancode sc, int pressed)
{
    Uint16 bit = 0;
    switch (sc) {
    case SDL_SCANCODE_LSHIFT: bit = KMOD_LSHIFT; break;
    case SDL_SCANCODE_RSHIFT: bit = KMOD_RSHIFT; break;
    case SDL_SCANCODE_LCTRL:  bit = KMOD_LCTRL;  break;
    case SDL_SCANCODE_RCTRL:  bit = KMOD_RCTRL;  break;
    case SDL_SCANCODE_LALT:   bit = KMOD_LALT;   break;
    case SDL_SCANCODE_RALT:   bit = KMOD_RALT;   break;
    default: return;
    }
    if (pressed)
        g_key_modifiers |= bit;
    else
        g_key_modifiers &= ~bit;
}

/* Previous mouse state for generating motion events */
static int g_prev_mouse_x = -1;
static int g_prev_mouse_y = -1;
static int g_prev_mouse_left  = 0;
static int g_prev_mouse_right = 0;

/* Convert absolute screen mouse coords to window-relative client coords */
static void screen_to_client(int *mx, int *my)
{
    long wpos = sys_get_window_pos();
    if (wpos != -1) {
        int cox = (int)(short)(wpos & 0xFFFF);
        int coy = (int)(short)((wpos >> 16) & 0xFFFF);
        *mx -= cox;
        *my -= coy;
    }
}

/* ── Pushed event queue (for SDL_PushEvent) ────────────────────────────── */
#define PUSH_QUEUE_SIZE 32
static SDL_Event g_push_queue[PUSH_QUEUE_SIZE];
static int g_push_head = 0;
static int g_push_tail = 0;

/* Audio mixer hook — overridden by aulib_potato.cpp when DevilutionX links
 * in the Aulib shim. Default is a no-op so apps without audio link cleanly. */
__attribute__((weak)) void __sdl2_audio_tick(void) { }

int SDL_PollEvent(SDL_Event *event)
{
    /* Keep the Aulib mixer fed every time the game polls for events. */
    __sdl2_audio_tick();

    if (!event) return 0;

    /* Drain pushed events first (from SDL_PushEvent) */
    if (g_push_head != g_push_tail) {
        *event = g_push_queue[g_push_tail];
        g_push_tail = (g_push_tail + 1) % PUSH_QUEUE_SIZE;
        return 1;
    }

    /* Drain pending text input event (queued from prior key press) */
    if (g_pending_textinput) {
        *event = g_pending_textinput_ev;
        g_pending_textinput = 0;
        return 1;
    }

    /* Check keyboard events from window queue */
    long kev = sys_get_key_event();
    if (kev != -1) {
        int pressed = (kev & 0x100) != 0;
        int ps2_sc  = (int)(kev & 0xFF);
        SDL_Scancode sc = ps2_to_sdl_scancode(ps2_sc);
        update_modifiers(sc, pressed);

        event->type = pressed ? SDL_KEYDOWN : SDL_KEYUP;
        event->key.timestamp = SDL_GetTicks();
        event->key.windowID = g_window.wid;
        event->key.state = pressed ? SDL_PRESSED : SDL_RELEASED;
        event->key.repeat = 0;
        event->key.keysym.scancode = sc;
        event->key.keysym.sym = scancode_to_keycode(sc);
        event->key.keysym.mod = g_key_modifiers;
        /* Keep g_keyboard_state in sync for SDL_GetKeyboardState */
        if (sc < SDL_NUM_SCANCODES)
            g_keyboard_state[sc] = pressed ? 1 : 0;

        /* Queue a text input event for printable key presses */
        if (pressed && g_text_input_enabled
            && !(g_key_modifiers & (KMOD_LCTRL | KMOD_RCTRL | KMOD_LALT | KMOD_RALT))) {
            char ch = scancode_to_char(sc, g_key_modifiers);
            if (ch) {
                g_pending_textinput = 1;
                g_pending_textinput_ev.type = SDL_TEXTINPUT;
                g_pending_textinput_ev.text.timestamp = SDL_GetTicks();
                g_pending_textinput_ev.text.windowID = g_window.wid;
                g_pending_textinput_ev.text.text[0] = ch;
                g_pending_textinput_ev.text.text[1] = '\0';
            }
        }
        return 1;
    }

    /* Check mouse — generate motion and button events from position changes */
    long mpos = sys_get_mouse_pos();
    if (mpos != -1) {
        int mx    = (int)(short)(mpos & 0xFFFF);
        int my    = (int)(short)((mpos >> 16) & 0xFFFF);
        int left  = (mpos >> 32) & 1;
        int right = (mpos >> 33) & 1;
        screen_to_client(&mx, &my);

        /* Button press/release takes priority */
        if (left != g_prev_mouse_left) {
            event->type = left ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
            event->button.timestamp = SDL_GetTicks();
            event->button.windowID = g_window.wid;
            event->button.button = SDL_BUTTON_LEFT;
            event->button.state = left ? SDL_PRESSED : SDL_RELEASED;
            event->button.clicks = 1;
            event->button.x = mx;
            event->button.y = my;
            g_prev_mouse_left = left;
            g_prev_mouse_x = mx;
            g_prev_mouse_y = my;
            return 1;
        }

        if (right != g_prev_mouse_right) {
            event->type = right ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
            event->button.timestamp = SDL_GetTicks();
            event->button.windowID = g_window.wid;
            event->button.button = SDL_BUTTON_RIGHT;
            event->button.state = right ? SDL_PRESSED : SDL_RELEASED;
            event->button.clicks = 1;
            event->button.x = mx;
            event->button.y = my;
            g_prev_mouse_right = right;
            g_prev_mouse_x = mx;
            g_prev_mouse_y = my;
            return 1;
        }

        /* Motion event */
        if (mx != g_prev_mouse_x || my != g_prev_mouse_y) {
            event->type = SDL_MOUSEMOTION;
            event->motion.timestamp = SDL_GetTicks();
            event->motion.windowID = g_window.wid;
            event->motion.x = mx;
            event->motion.y = my;
            event->motion.xrel = (g_prev_mouse_x >= 0) ? mx - g_prev_mouse_x : 0;
            event->motion.yrel = (g_prev_mouse_y >= 0) ? my - g_prev_mouse_y : 0;
            event->motion.state = (left ? 1 : 0) | (right ? 4 : 0);
            g_prev_mouse_x = mx;
            g_prev_mouse_y = my;
            return 1;
        }
    }

    return 0;
}

Uint32 SDL_GetMouseState(int *x, int *y)
{
    long mpos = sys_get_mouse_pos();
    int mx = (int)(short)(mpos & 0xFFFF);
    int my = (int)(short)((mpos >> 16) & 0xFFFF);
    int left  = (mpos >> 32) & 1;
    int right = (mpos >> 33) & 1;
    screen_to_client(&mx, &my);
    if (x) *x = mx;
    if (y) *y = my;
    return (left ? 1 : 0) | (right ? 4 : 0);
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Task 1: Pixel Format Helpers (palette, MapRGB, MapRGBA)
 * ═══════════════════════════════════════════════════════════════════════════ */

SDL_Palette* SDL_AllocPalette(int ncolors)
{
    if (ncolors < 1) return (SDL_Palette*)0;
    SDL_Palette *pal = (SDL_Palette*)calloc(1, sizeof(SDL_Palette));
    if (!pal) return (SDL_Palette*)0;
    pal->colors = (SDL_Color*)calloc(ncolors, sizeof(SDL_Color));
    if (!pal->colors) { free(pal); return (SDL_Palette*)0; }
    pal->ncolors = ncolors;
    pal->version = 1;
    pal->refcount = 1;
    /* calloc zeroed r/g/b; just set alpha to fully opaque */
    for (int i = 0; i < ncolors; i++)
        pal->colors[i].a = 255;
    return pal;
}

void SDL_FreePalette(SDL_Palette *palette)
{
    if (!palette) return;
    palette->refcount--;
    if (palette->refcount > 0) return;
    if (palette->colors) free(palette->colors);
    free(palette);
}

int SDL_SetPaletteColors(SDL_Palette *palette, const SDL_Color *colors,
                         int firstcolor, int ncolors)
{
    if (!palette || !colors) return -1;
    if (firstcolor < 0 || firstcolor + ncolors > palette->ncolors) return -1;
    memcpy(palette->colors + firstcolor, colors, ncolors * sizeof(SDL_Color));
    palette->version++;
    g_active_palette = palette;  /* track for 8-bit→ARGB in SDL_UpdateTexture */
    /* Sample multiple entries to find non-black colors */
    {
        int found_idx = -1;
        for (int _i = 0; _i < ncolors && _i < 256; _i++) {
            if (colors[_i].r || colors[_i].g || colors[_i].b) { found_idx = _i; break; }
        }
        if (found_idx >= 0) {
            sdl_trace("[SDL] SetPaletteColors: pal=%p first=%d n=%d first_nonblack[%d]=(%d,%d,%d,%d)\n",
                      palette, firstcolor, ncolors, found_idx + firstcolor,
                      colors[found_idx].r, colors[found_idx].g, colors[found_idx].b, colors[found_idx].a);
        } else {
            sdl_trace("[SDL] SetPaletteColors: pal=%p first=%d n=%d ALL BLACK\n",
                      palette, firstcolor, ncolors);
        }
    }
    return 0;
}

Uint32 SDL_MapRGB(const SDL_PixelFormat *format, Uint8 r, Uint8 g, Uint8 b)
{
    return SDL_MapRGBA(format, r, g, b, 255);
}

Uint32 SDL_MapRGBA(const SDL_PixelFormat *format, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
    if (!format) return 0;

    /* Palettized: find closest color */
    if (format->palette) {
        int best = 0;
        int best_dist = 0x7FFFFFFF;
        for (int i = 0; i < format->palette->ncolors; i++) {
            SDL_Color *c = &format->palette->colors[i];
            int dr = (int)r - (int)c->r;
            int dg = (int)g - (int)c->g;
            int db = (int)b - (int)c->b;
            int dist = dr*dr + dg*dg + db*db;
            if (dist == 0) return (Uint32)i;
            if (dist < best_dist) { best_dist = dist; best = i; }
        }
        return (Uint32)best;
    }

    /* Direct color: shift via masks */
    Uint32 pixel = 0;
    if (format->Rmask) {
        int shift = __builtin_ctz(format->Rmask);
        pixel |= ((Uint32)r << shift) & format->Rmask;
    }
    if (format->Gmask) {
        int shift = __builtin_ctz(format->Gmask);
        pixel |= ((Uint32)g << shift) & format->Gmask;
    }
    if (format->Bmask) {
        int shift = __builtin_ctz(format->Bmask);
        pixel |= ((Uint32)b << shift) & format->Bmask;
    }
    if (format->Amask) {
        int shift = __builtin_ctz(format->Amask);
        pixel |= ((Uint32)a << shift) & format->Amask;
    }
    return pixel;
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Task 2: Surface Creation and Management
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Internal: allocate and populate an SDL_PixelFormat from bpp + masks */
static SDL_PixelFormat* create_pixel_format(int bpp, Uint32 Rmask, Uint32 Gmask,
                                            Uint32 Bmask, Uint32 Amask)
{
    SDL_PixelFormat *fmt = (SDL_PixelFormat*)calloc(1, sizeof(SDL_PixelFormat));
    if (!fmt) return (SDL_PixelFormat*)0;
    fmt->BitsPerPixel  = bpp;
    fmt->BytesPerPixel = (bpp + 7) / 8;
    fmt->Rmask = Rmask;
    fmt->Gmask = Gmask;
    fmt->Bmask = Bmask;
    fmt->Amask = Amask;
    fmt->palette = (SDL_Palette*)0;

    /* Detect format enum */
    if (bpp == 8 && Rmask == 0 && Gmask == 0 && Bmask == 0) {
        fmt->format = SDL_PIXELFORMAT_INDEX8;
        fmt->palette = SDL_AllocPalette(256);
    } else if (bpp == 32 && Rmask == 0x00FF0000 && Gmask == 0x0000FF00 &&
               Bmask == 0x000000FF && Amask == 0xFF000000) {
        fmt->format = SDL_PIXELFORMAT_ARGB8888;
    } else if (bpp == 32 && Rmask == 0x00FF0000 && Gmask == 0x0000FF00 &&
               Bmask == 0x000000FF && Amask == 0) {
        fmt->format = SDL_PIXELFORMAT_RGB888;
    } else if (bpp == 32 && Rmask == 0xFF000000 && Gmask == 0x00FF0000 &&
               Bmask == 0x0000FF00 && Amask == 0x000000FF) {
        fmt->format = SDL_PIXELFORMAT_RGBA8888;
    } else if (bpp == 32 && Rmask == 0x000000FF && Gmask == 0x0000FF00 &&
               Bmask == 0x00FF0000 && Amask == 0xFF000000) {
        fmt->format = SDL_PIXELFORMAT_ABGR8888;
    } else if (bpp == 32 && Rmask == 0x0000FF00 && Gmask == 0x00FF0000 &&
               Bmask == 0xFF000000 && Amask == 0x000000FF) {
        fmt->format = SDL_PIXELFORMAT_BGRA8888;
    } else if (bpp == 24 && Rmask == 0x00FF0000 && Gmask == 0x0000FF00 &&
               Bmask == 0x000000FF) {
        fmt->format = SDL_PIXELFORMAT_RGB24;
    } else if (bpp == 24 && Rmask == 0x000000FF && Gmask == 0x0000FF00 &&
               Bmask == 0x00FF0000) {
        fmt->format = SDL_PIXELFORMAT_BGR24;
    } else {
        fmt->format = SDL_PIXELFORMAT_UNKNOWN;
    }
    return fmt;
}

/* Internal: given a format enum, output bpp and masks */
static int masks_from_format(Uint32 format, int *bpp,
                             Uint32 *Rm, Uint32 *Gm, Uint32 *Bm, Uint32 *Am)
{
    *Rm = *Gm = *Bm = *Am = 0;
    switch (format) {
    case SDL_PIXELFORMAT_INDEX8:
        *bpp = 8; return 0;
    case SDL_PIXELFORMAT_RGB888:
        *bpp = 32; *Rm = 0x00FF0000; *Gm = 0x0000FF00; *Bm = 0x000000FF; return 0;
    case SDL_PIXELFORMAT_ARGB8888:
        *bpp = 32; *Rm = 0x00FF0000; *Gm = 0x0000FF00; *Bm = 0x000000FF; *Am = 0xFF000000; return 0;
    case SDL_PIXELFORMAT_RGBA8888:
        *bpp = 32; *Rm = 0xFF000000; *Gm = 0x00FF0000; *Bm = 0x0000FF00; *Am = 0x000000FF; return 0;
    case SDL_PIXELFORMAT_ABGR8888:
        *bpp = 32; *Rm = 0x000000FF; *Gm = 0x0000FF00; *Bm = 0x00FF0000; *Am = 0xFF000000; return 0;
    case SDL_PIXELFORMAT_BGRA8888:
        *bpp = 32; *Rm = 0x0000FF00; *Gm = 0x00FF0000; *Bm = 0xFF000000; *Am = 0x000000FF; return 0;
    case SDL_PIXELFORMAT_RGB24:
        *bpp = 24; *Rm = 0x00FF0000; *Gm = 0x0000FF00; *Bm = 0x000000FF; return 0;
    case SDL_PIXELFORMAT_BGR24:
        *bpp = 24; *Rm = 0x000000FF; *Gm = 0x0000FF00; *Bm = 0x00FF0000; return 0;
    default:
        *bpp = 32; return -1;
    }
}

/* Internal: free a pixel format and its palette */
static void free_pixel_format(SDL_PixelFormat *fmt)
{
    if (!fmt) return;
    if (fmt->palette) SDL_FreePalette(fmt->palette);
    free(fmt);
}

SDL_Surface* SDL_CreateRGBSurface(Uint32 flags, int w, int h, int depth,
                                  Uint32 Rmask, Uint32 Gmask,
                                  Uint32 Bmask, Uint32 Amask)
{
    SDL_Surface *s = (SDL_Surface*)calloc(1, sizeof(SDL_Surface));
    if (!s) return (SDL_Surface*)0;

    s->format = create_pixel_format(depth, Rmask, Gmask, Bmask, Amask);
    if (!s->format) { free(s); return (SDL_Surface*)0; }

    s->w = w;
    s->h = h;
    s->flags = flags;
    /* Pitch: bytes per row, aligned to 4 bytes */
    int bpp = s->format->BytesPerPixel;
    s->pitch = (w * bpp + 3) & ~3;
    s->pixels = calloc(1, s->pitch * h);
    if (!s->pixels && w > 0 && h > 0) {
        free_pixel_format(s->format);
        free(s);
        return (SDL_Surface*)0;
    }
    s->clip_rect.x = 0;
    s->clip_rect.y = 0;
    s->clip_rect.w = w;
    s->clip_rect.h = h;
    s->refcount = 1;
    return s;
}

SDL_Surface* SDL_CreateRGBSurfaceWithFormat(Uint32 flags, int w, int h,
                                            int depth, Uint32 format)
{
    int bpp;
    Uint32 Rm, Gm, Bm, Am;
    masks_from_format(format, &bpp, &Rm, &Gm, &Bm, &Am);
    (void)depth;
    return SDL_CreateRGBSurface(flags, w, h, bpp, Rm, Gm, Bm, Am);
}

SDL_Surface* SDL_CreateRGBSurfaceFrom(void *pixels, int w, int h, int depth,
                                      int pitch, Uint32 Rmask, Uint32 Gmask,
                                      Uint32 Bmask, Uint32 Amask)
{
    SDL_Surface *s = (SDL_Surface*)calloc(1, sizeof(SDL_Surface));
    if (!s) return (SDL_Surface*)0;

    s->format = create_pixel_format(depth, Rmask, Gmask, Bmask, Amask);
    if (!s->format) { free(s); return (SDL_Surface*)0; }

    s->w = w;
    s->h = h;
    s->pitch = pitch;
    s->pixels = pixels;
    s->flags = SDL_PREALLOC;
    s->clip_rect.x = 0;
    s->clip_rect.y = 0;
    s->clip_rect.w = w;
    s->clip_rect.h = h;
    s->refcount = 1;
    return s;
}

SDL_Surface* SDL_CreateRGBSurfaceWithFormatFrom(void *pixels, int w, int h,
                                                int depth, int pitch, Uint32 format)
{
    int bpp;
    Uint32 Rm, Gm, Bm, Am;
    masks_from_format(format, &bpp, &Rm, &Gm, &Bm, &Am);
    (void)depth;
    return SDL_CreateRGBSurfaceFrom(pixels, w, h, bpp, pitch, Rm, Gm, Bm, Am);
}

void SDL_FreeSurface(SDL_Surface *surface)
{
    if (!surface) return;
    surface->refcount--;
    if (surface->refcount > 0) return;
    if (surface->pixels && !(surface->flags & SDL_PREALLOC))
        free(surface->pixels);
    if (surface->format)
        free_pixel_format(surface->format);
    free(surface);
}

int SDL_LockSurface(SDL_Surface *surface)
{
    (void)surface;
    return 0;
}

void SDL_UnlockSurface(SDL_Surface *surface)
{
    (void)surface;
}

int SDL_SetSurfacePalette(SDL_Surface *surface, SDL_Palette *palette)
{
    if (!surface || !surface->format || !palette) return -1;
    if (surface->format->palette)
        SDL_FreePalette(surface->format->palette);
    palette->refcount++;
    surface->format->palette = palette;
    sdl_trace("[SDL] SetSurfacePalette: pal=%p ncolors=%d first3=(%d,%d,%d) (%d,%d,%d) (%d,%d,%d)\n",
              palette, palette->ncolors,
              palette->ncolors > 0 ? palette->colors[0].r : -1,
              palette->ncolors > 0 ? palette->colors[0].g : -1,
              palette->ncolors > 0 ? palette->colors[0].b : -1,
              palette->ncolors > 1 ? palette->colors[1].r : -1,
              palette->ncolors > 1 ? palette->colors[1].g : -1,
              palette->ncolors > 1 ? palette->colors[1].b : -1,
              palette->ncolors > 2 ? palette->colors[2].r : -1,
              palette->ncolors > 2 ? palette->colors[2].g : -1,
              palette->ncolors > 2 ? palette->colors[2].b : -1);
    return 0;
}

int SDL_SetColorKey(SDL_Surface *surface, int flag, Uint32 key)
{
    if (!surface) return -1;
    if (flag)
        surface->flags |= 0x1000;
    else
        surface->flags &= ~((Uint32)0x1000);
    surface->color_key = key;
    return 0;
}

int SDL_SetClipRect(SDL_Surface *surface, const SDL_Rect *rect)
{
    if (!surface) return SDL_FALSE;
    if (!rect) {
        surface->clip_rect.x = 0;
        surface->clip_rect.y = 0;
        surface->clip_rect.w = surface->w;
        surface->clip_rect.h = surface->h;
    } else {
        /* Clamp to surface bounds */
        int x1 = rect->x < 0 ? 0 : rect->x;
        int y1 = rect->y < 0 ? 0 : rect->y;
        int x2 = rect->x + rect->w;
        int y2 = rect->y + rect->h;
        if (x2 > surface->w) x2 = surface->w;
        if (y2 > surface->h) y2 = surface->h;
        if (x1 >= x2 || y1 >= y2) {
            surface->clip_rect.x = 0;
            surface->clip_rect.y = 0;
            surface->clip_rect.w = 0;
            surface->clip_rect.h = 0;
            return SDL_FALSE;
        }
        surface->clip_rect.x = x1;
        surface->clip_rect.y = y1;
        surface->clip_rect.w = x2 - x1;
        surface->clip_rect.h = y2 - y1;
    }
    return SDL_TRUE;
}

void SDL_GetClipRect(SDL_Surface *surface, SDL_Rect *rect)
{
    if (!surface || !rect) return;
    *rect = surface->clip_rect;
}

int SDL_SetSurfaceColorMod(SDL_Surface *surface, Uint8 r, Uint8 g, Uint8 b)
{
    (void)surface; (void)r; (void)g; (void)b;
    return 0;
}

int SDL_SetSurfaceAlphaMod(SDL_Surface *surface, Uint8 alpha)
{
    (void)surface; (void)alpha;
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Task 3: Surface Blitting
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Read a pixel at (x,y) from surface */
static Uint32 get_pixel(SDL_Surface *surface, int x, int y)
{
    Uint8 *p = (Uint8*)surface->pixels + y * surface->pitch +
               x * surface->format->BytesPerPixel;
    switch (surface->format->BytesPerPixel) {
    case 1: return *p;
    case 2: return *(Uint16*)p;
    case 3:
        /* Little-endian assumed */
        return (Uint32)p[0] | ((Uint32)p[1] << 8) | ((Uint32)p[2] << 16);
    case 4: return *(Uint32*)p;
    default: return 0;
    }
}

/* Write a pixel at (x,y) to surface */
static void put_pixel(SDL_Surface *surface, int x, int y, Uint32 pixel)
{
    Uint8 *p = (Uint8*)surface->pixels + y * surface->pitch +
               x * surface->format->BytesPerPixel;
    switch (surface->format->BytesPerPixel) {
    case 1: *p = (Uint8)pixel; break;
    case 2: *(Uint16*)p = (Uint16)pixel; break;
    case 3:
        p[0] = pixel & 0xFF;
        p[1] = (pixel >> 8) & 0xFF;
        p[2] = (pixel >> 16) & 0xFF;
        break;
    case 4: *(Uint32*)p = pixel; break;
    }
}

/* Extract R,G,B,A components from a pixel value given a format */
static void extract_rgba(Uint32 pixel, const SDL_PixelFormat *fmt,
                         Uint8 *r, Uint8 *g, Uint8 *b, Uint8 *a)
{
    if (fmt->palette) {
        int idx = (int)(pixel & 0xFF);
        if (idx < fmt->palette->ncolors) {
            *r = fmt->palette->colors[idx].r;
            *g = fmt->palette->colors[idx].g;
            *b = fmt->palette->colors[idx].b;
            *a = fmt->palette->colors[idx].a;
        } else {
            *r = *g = *b = 0; *a = 255;
        }
        return;
    }
    if (fmt->Rmask) {
        int shift = __builtin_ctz(fmt->Rmask);
        *r = (Uint8)((pixel & fmt->Rmask) >> shift);
    } else { *r = 0; }
    if (fmt->Gmask) {
        int shift = __builtin_ctz(fmt->Gmask);
        *g = (Uint8)((pixel & fmt->Gmask) >> shift);
    } else { *g = 0; }
    if (fmt->Bmask) {
        int shift = __builtin_ctz(fmt->Bmask);
        *b = (Uint8)((pixel & fmt->Bmask) >> shift);
    } else { *b = 0; }
    if (fmt->Amask) {
        int shift = __builtin_ctz(fmt->Amask);
        *a = (Uint8)((pixel & fmt->Amask) >> shift);
    } else { *a = 255; }
}

/* Convert a pixel from one format to another */
static Uint32 convert_pixel(Uint32 pixel, const SDL_PixelFormat *src_fmt,
                            const SDL_PixelFormat *dst_fmt)
{
    Uint8 r, g, b, a;
    extract_rgba(pixel, src_fmt, &r, &g, &b, &a);
    return SDL_MapRGBA(dst_fmt, r, g, b, a);
}

int SDL_FillRect(SDL_Surface *dst, const SDL_Rect *rect, Uint32 color)
{
    if (!dst || !dst->pixels) return -1;

    int x0, y0, x1, y1;
    if (!rect) {
        x0 = 0; y0 = 0; x1 = dst->w; y1 = dst->h;
    } else {
        x0 = rect->x; y0 = rect->y;
        x1 = rect->x + rect->w;
        y1 = rect->y + rect->h;
    }
    /* Clip to surface bounds */
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > dst->w) x1 = dst->w;
    if (y1 > dst->h) y1 = dst->h;
    if (x0 >= x1 || y0 >= y1) return 0;

    int bpp = dst->format->BytesPerPixel;

    if (bpp == 4) {
        for (int y = y0; y < y1; y++) {
            Uint32 *row = (Uint32*)((Uint8*)dst->pixels + y * dst->pitch) + x0;
            for (int x = 0; x < x1 - x0; x++)
                row[x] = color;
        }
    } else if (bpp == 1) {
        Uint8 c8 = (Uint8)color;
        for (int y = y0; y < y1; y++) {
            Uint8 *row = (Uint8*)dst->pixels + y * dst->pitch + x0;
            memset(row, c8, x1 - x0);
        }
    } else {
        for (int y = y0; y < y1; y++)
            for (int x = x0; x < x1; x++)
                put_pixel(dst, x, y, color);
    }
    return 0;
}

static int g_blit_trace_count = 0;

int SDL_BlitSurface(SDL_Surface *src, const SDL_Rect *srcrect,
                    SDL_Surface *dst, SDL_Rect *dstrect)
{
    if (!src || !dst || !src->pixels || !dst->pixels) return -1;

    if (g_blit_trace_count < 20) {
        g_blit_trace_count++;
        sdl_trace("[SDL] BlitSurface #%d: src=%dx%d fmt=0x%x bpp=%d pal=%p -> dst=%dx%d fmt=0x%x bpp=%d\n",
                  g_blit_trace_count,
                  src->w, src->h, src->format->format, src->format->BytesPerPixel,
                  src->format->palette,
                  dst->w, dst->h, dst->format->format, dst->format->BytesPerPixel);
        if (src->format->palette && src->format->palette->ncolors > 0) {
            /* Sample the first non-zero src pixel and show what it maps to */
            Uint8 *sp = (Uint8*)src->pixels;
            int found = -1;
            for (int i = 0; i < src->w * src->h && i < 10000; i++) {
                if (sp[i] != 0) { found = i; break; }
            }
            if (found >= 0) {
                int idx = sp[found];
                SDL_Color c = src->format->palette->colors[idx];
                sdl_trace("[SDL]   px[%d]=idx %d -> RGBA(%d,%d,%d,%d)  pal[1]=(%d,%d,%d,%d) pal[128]=(%d,%d,%d,%d)\n",
                          found, idx, c.r, c.g, c.b, c.a,
                          src->format->palette->colors[1].r, src->format->palette->colors[1].g,
                          src->format->palette->colors[1].b, src->format->palette->colors[1].a,
                          src->format->palette->colors[128].r, src->format->palette->colors[128].g,
                          src->format->palette->colors[128].b, src->format->palette->colors[128].a);
            } else {
                sdl_trace("[SDL]   all src pixels are 0 (first 10000)\n");
            }
        }
    }

    /* Source rect defaults to full surface */
    int sx = 0, sy = 0, sw = src->w, sh = src->h;
    if (srcrect) { sx = srcrect->x; sy = srcrect->y; sw = srcrect->w; sh = srcrect->h; }

    /* Dest position */
    int dx = 0, dy = 0;
    if (dstrect) { dx = dstrect->x; dy = dstrect->y; }

    /* Clip to dest clip_rect */
    int cx0 = dst->clip_rect.x;
    int cy0 = dst->clip_rect.y;
    int cx1 = cx0 + dst->clip_rect.w;
    int cy1 = cy0 + dst->clip_rect.h;

    /* Adjust for clipping at left/top */
    if (dx < cx0) { int d = cx0 - dx; sx += d; sw -= d; dx = cx0; }
    if (dy < cy0) { int d = cy0 - dy; sy += d; sh -= d; dy = cy0; }
    /* Clip at right/bottom */
    if (dx + sw > cx1) sw = cx1 - dx;
    if (dy + sh > cy1) sh = cy1 - dy;
    if (sw <= 0 || sh <= 0) return 0;

    int has_ckey = (src->flags & 0x1000) != 0;
    Uint32 ckey = has_ckey ? src->color_key : 0;
    /* For non-palettized surfaces, compare color key ignoring alpha */
    Uint32 ckey_mask = 0xFFFFFFFF;
    if (has_ckey && src->format->BytesPerPixel > 1 && src->format->Amask)
        ckey_mask = ~src->format->Amask;

    /* Check if formats are the same and non-palettized (fast path) */
    int same_fmt = (src->format->format == dst->format->format &&
                    src->format->format != SDL_PIXELFORMAT_INDEX8 &&
                    src->format->format != SDL_PIXELFORMAT_UNKNOWN);

    int bpp = src->format->BytesPerPixel;
    int do_blend = (src->blend_mode == 1); /* SDL_BLENDMODE_BLEND */

    for (int y = 0; y < sh; y++) {
        if (same_fmt && !has_ckey && !do_blend && bpp > 0) {
            /* Direct copy row */
            Uint8 *sp = (Uint8*)src->pixels + (sy + y) * src->pitch + sx * bpp;
            Uint8 *dp = (Uint8*)dst->pixels + (dy + y) * dst->pitch + dx * bpp;
            memcpy(dp, sp, sw * bpp);
        } else {
            for (int x = 0; x < sw; x++) {
                Uint32 pixel = get_pixel(src, sx + x, sy + y);
                if (has_ckey && (pixel & ckey_mask) == (ckey & ckey_mask)) continue;

                if (do_blend) {
                    /* Extract source alpha */
                    Uint8 sr, sg, sb, sa;
                    extract_rgba(pixel, src->format, &sr, &sg, &sb, &sa);
                    if (sa == 0) continue; /* fully transparent */
                    if (sa == 255) {
                        /* Fully opaque — direct write */
                        Uint32 out = SDL_MapRGBA(dst->format, sr, sg, sb, 255);
                        put_pixel(dst, dx + x, dy + y, out);
                    } else {
                        /* Semi-transparent — alpha blend */
                        Uint32 dpx = get_pixel(dst, dx + x, dy + y);
                        Uint8 dr, dg, db, da;
                        extract_rgba(dpx, dst->format, &dr, &dg, &db, &da);
                        Uint8 or_ = (sr * sa + dr * (255 - sa)) / 255;
                        Uint8 og  = (sg * sa + dg * (255 - sa)) / 255;
                        Uint8 ob  = (sb * sa + db * (255 - sa)) / 255;
                        put_pixel(dst, dx + x, dy + y,
                                  SDL_MapRGBA(dst->format, or_, og, ob, 255));
                    }
                } else {
                    if (!same_fmt)
                        pixel = convert_pixel(pixel, src->format, dst->format);
                    put_pixel(dst, dx + x, dy + y, pixel);
                }
            }
        }
    }

    if (dstrect) { dstrect->w = sw; dstrect->h = sh; }
    return 0;
}

int SDL_BlitScaled(SDL_Surface *src, const SDL_Rect *srcrect,
                   SDL_Surface *dst, SDL_Rect *dstrect)
{
    if (!src || !dst || !src->pixels || !dst->pixels) return -1;

    int sx = 0, sy = 0, sw = src->w, sh = src->h;
    if (srcrect) { sx = srcrect->x; sy = srcrect->y; sw = srcrect->w; sh = srcrect->h; }

    int dx = 0, dy = 0, dw = dst->w, dh = dst->h;
    if (dstrect) { dx = dstrect->x; dy = dstrect->y; dw = dstrect->w; dh = dstrect->h; }

    /* If same size, delegate to non-scaled blit */
    if (sw == dw && sh == dh) {
        SDL_Rect sr = { sx, sy, sw, sh };
        SDL_Rect dr = { dx, dy, dw, dh };
        int ret = SDL_BlitSurface(src, &sr, dst, &dr);
        if (dstrect) { dstrect->w = dr.w; dstrect->h = dr.h; }
        return ret;
    }

    /* Guard against zero-size rects (prevents divide-by-zero below) */
    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return 0;

    int has_ckey = (src->flags & 0x1000) != 0;
    Uint32 ckey = has_ckey ? src->color_key : 0;
    Uint32 ckey_mask = 0xFFFFFFFF;
    if (has_ckey && src->format->BytesPerPixel > 1 && src->format->Amask)
        ckey_mask = ~src->format->Amask;

    /* Clip dest to clip_rect */
    int cx0 = dst->clip_rect.x;
    int cy0 = dst->clip_rect.y;
    int rx0 = dx, ry0 = dy;
    int rx1 = dx + dw, ry1 = dy + dh;
    if (rx0 < cx0) rx0 = cx0;
    if (ry0 < cy0) ry0 = cy0;
    if (rx1 > cx0 + dst->clip_rect.w) rx1 = cx0 + dst->clip_rect.w;
    if (ry1 > cy0 + dst->clip_rect.h) ry1 = cy0 + dst->clip_rect.h;

    /* Nearest-neighbor scaled blit */
    for (int y = ry0; y < ry1; y++) {
        int ty = sy + ((y - dy) * sh) / dh;
        if (ty < sy) ty = sy;
        if (ty >= sy + sh) ty = sy + sh - 1;
        for (int x = rx0; x < rx1; x++) {
            int tx = sx + ((x - dx) * sw) / dw;
            if (tx < sx) tx = sx;
            if (tx >= sx + sw) tx = sx + sw - 1;
            Uint32 pixel = get_pixel(src, tx, ty);
            if (has_ckey && (pixel & ckey_mask) == (ckey & ckey_mask)) continue;
            Uint32 out = convert_pixel(pixel, src->format, dst->format);
            put_pixel(dst, x, y, out);
        }
    }
    return 0;
}

int SDL_SoftStretch(SDL_Surface *src, const SDL_Rect *srcrect,
                    SDL_Surface *dst, const SDL_Rect *dstrect)
{
    /* dstrect is const here, copy to mutable for BlitScaled */
    SDL_Rect dr;
    if (dstrect) { dr = *dstrect; } else { dr.x = 0; dr.y = 0; dr.w = dst->w; dr.h = dst->h; }
    return SDL_BlitScaled(src, srcrect, dst, &dr);
}

SDL_Surface* SDL_ConvertSurface(SDL_Surface *src, const SDL_PixelFormat *fmt, Uint32 flags)
{
    if (!src || !fmt) return (SDL_Surface*)0;
    (void)flags;

    SDL_Surface *dst = SDL_CreateRGBSurface(0, src->w, src->h,
                                            fmt->BitsPerPixel,
                                            fmt->Rmask, fmt->Gmask,
                                            fmt->Bmask, fmt->Amask);
    if (!dst) return (SDL_Surface*)0;

    /* If both palettized, copy palette from source */
    if (fmt->palette && src->format->palette) {
        SDL_SetPaletteColors(dst->format->palette, src->format->palette->colors,
                             0, src->format->palette->ncolors < dst->format->palette->ncolors
                                ? src->format->palette->ncolors
                                : dst->format->palette->ncolors);
    }

    SDL_BlitSurface(src, (const SDL_Rect*)0, dst, (SDL_Rect*)0);
    return dst;
}

SDL_Surface* SDL_ConvertSurfaceFormat(SDL_Surface *src, Uint32 pixel_format, Uint32 flags)
{
    if (!src) return (SDL_Surface*)0;
    int bpp;
    Uint32 Rm, Gm, Bm, Am;
    masks_from_format(pixel_format, &bpp, &Rm, &Gm, &Bm, &Am);

    SDL_PixelFormat tmp;
    tmp.format = pixel_format;
    tmp.BitsPerPixel = bpp;
    tmp.BytesPerPixel = (bpp + 7) / 8;
    tmp.Rmask = Rm;
    tmp.Gmask = Gm;
    tmp.Bmask = Bm;
    tmp.Amask = Am;
    tmp.palette = (SDL_Palette*)0;

    return SDL_ConvertSurface(src, &tmp, flags);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Task 4: Window Surface Path
 * ═══════════════════════════════════════════════════════════════════════════ */

static SDL_Surface *g_window_surface = (SDL_Surface*)0;

SDL_Surface* SDL_GetWindowSurface(SDL_Window *window)
{
    if (!window) return (SDL_Surface*)0;
    int w = window->w;
    int h = window->h;

    /* Reuse if dimensions match */
    if (g_window_surface && g_window_surface->w == w && g_window_surface->h == h)
        return g_window_surface;

    /* Free old surface if dimensions changed */
    if (g_window_surface) {
        SDL_FreeSurface(g_window_surface);
        g_window_surface = (SDL_Surface*)0;
    }

    /* ARGB8888 surface */
    g_window_surface = SDL_CreateRGBSurface(0, w, h, 32,
                                            0x00FF0000, 0x0000FF00,
                                            0x000000FF, 0xFF000000);
    sdl_trace("[SDL] GetWindowSurface: created %dx%d ARGB8888 surf=%p pixels=%p\n",
              w, h, g_window_surface, g_window_surface ? g_window_surface->pixels : 0);
    return g_window_surface;
}

static int g_update_trace_count = 0;

int SDL_UpdateWindowSurface(SDL_Window *window)
{
    if (!window || !g_window_surface) return -1;
    int w = g_window_surface->w;
    int h = g_window_surface->h;
    int total = w * h;
    unsigned char *rgb = (unsigned char*)malloc(total * 3);
    if (!rgb) return -1;

    Uint32 *src = (Uint32*)g_window_surface->pixels;

    if (g_update_trace_count < 20) {
        g_update_trace_count++;
        /* Sample a few pixels from the ARGB window surface */
        Uint32 p0 = total > 0 ? src[0] : 0;
        Uint32 pm = total > 1 ? src[total / 2] : 0;
        int non_black = 0;
        for (int i = 0; i < total && i < 50000; i++) {
            if (src[i] != 0 && src[i] != 0xFF000000) { non_black++; }
        }
        sdl_trace("[SDL] UpdateWindowSurface #%d: %dx%d surf=%p px[0]=0x%08x px[mid]=0x%08x non_black=%d/%d\n",
                  g_update_trace_count, w, h, g_window_surface,
                  p0, pm, non_black, total < 50000 ? total : 50000);
    }

    for (int i = 0; i < total; i++) {
        Uint32 px = src[i]; /* ARGB */
        rgb[i * 3 + 0] = (px >> 16) & 0xFF; /* R */
        rgb[i * 3 + 1] = (px >>  8) & 0xFF; /* G */
        rgb[i * 3 + 2] = (px      ) & 0xFF; /* B */
    }

    sys_draw_pixels(rgb, 0, 0, w, h);
    free(rgb);
    return 0;
}

SDL_Texture* SDL_CreateTextureFromSurface(SDL_Renderer *renderer, SDL_Surface *surface)
{
    if (!renderer || !surface) return (SDL_Texture*)0;
    int w = surface->w;
    int h = surface->h;

    SDL_Texture *tex = (SDL_Texture*)malloc(sizeof(SDL_Texture));
    if (!tex) return (SDL_Texture*)0;
    tex->w = w;
    tex->h = h;
    tex->format = SDL_PIXELFORMAT_ARGB8888;
    tex->pitch = w * 4;
    tex->pixels = (Uint32*)malloc(w * h * 4);
    if (!tex->pixels) { free(tex); return (SDL_Texture*)0; }

    int bpp = surface->format->BitsPerPixel;
    SDL_Palette *pal = surface->format->palette;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            Uint8 r = 0, g = 0, b = 0, a = 0xFF;
            if (bpp == 8 && pal) {
                Uint8 idx = ((Uint8*)surface->pixels)[y * surface->pitch + x];
                if (idx < pal->ncolors) {
                    r = pal->colors[idx].r;
                    g = pal->colors[idx].g;
                    b = pal->colors[idx].b;
                    a = pal->colors[idx].a;
                }
            } else if (bpp == 32) {
                Uint32 px = ((Uint32*)surface->pixels)[y * (surface->pitch / 4) + x];
                /* Extract based on surface masks */
                if (surface->format->Rmask == 0x00FF0000) {
                    /* ARGB or XRGB */
                    a = (surface->format->Amask) ? ((px >> 24) & 0xFF) : 0xFF;
                    r = (px >> 16) & 0xFF;
                    g = (px >>  8) & 0xFF;
                    b = (px      ) & 0xFF;
                } else if (surface->format->Rmask == 0x000000FF) {
                    /* ABGR (RGBA memory order) */
                    r = (px      ) & 0xFF;
                    g = (px >>  8) & 0xFF;
                    b = (px >> 16) & 0xFF;
                    a = (surface->format->Amask) ? ((px >> 24) & 0xFF) : 0xFF;
                } else {
                    /* Fallback: treat as ARGB */
                    a = (px >> 24) & 0xFF;
                    r = (px >> 16) & 0xFF;
                    g = (px >>  8) & 0xFF;
                    b = (px      ) & 0xFF;
                }
            } else if (bpp == 24) {
                Uint8 *p = (Uint8*)surface->pixels + y * surface->pitch + x * 3;
                /* Assume RGB */
                r = p[0]; g = p[1]; b = p[2];
            } else if (bpp == 16) {
                Uint16 px = ((Uint16*)surface->pixels)[y * (surface->pitch / 2) + x];
                /* RGB565 */
                r = ((px >> 11) & 0x1F) << 3;
                g = ((px >>  5) & 0x3F) << 2;
                b = ((px      ) & 0x1F) << 3;
            }
            tex->pixels[y * w + x] = ((Uint32)a << 24) | ((Uint32)r << 16)
                                    | ((Uint32)g << 8) | (Uint32)b;
        }
    }

    return tex;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Task 5: RWops Implementation
 * ═══════════════════════════════════════════════════════════════════════════ */

/* ── File-backed RWops ──────────────────────────────────────────────────── */

static Sint64 stdio_size(SDL_RWops *ctx)
{
    FILE *fp = (FILE*)ctx->hidden.stdio.fp;
    long cur = ftell(fp);
    fseek(fp, 0, SEEK_END);
    long end = ftell(fp);
    fseek(fp, cur, SEEK_SET);
    return (Sint64)end;
}

static Sint64 stdio_seek(SDL_RWops *ctx, Sint64 offset, int whence)
{
    FILE *fp = (FILE*)ctx->hidden.stdio.fp;
    fseek(fp, (long)offset, whence);
    return (Sint64)ftell(fp);
}

static size_t stdio_read(SDL_RWops *ctx, void *ptr, size_t size, size_t maxnum)
{
    FILE *fp = (FILE*)ctx->hidden.stdio.fp;
    return fread(ptr, size, maxnum, fp);
}

static size_t stdio_write(SDL_RWops *ctx, const void *ptr, size_t size, size_t num)
{
    FILE *fp = (FILE*)ctx->hidden.stdio.fp;
    return fwrite(ptr, size, num, fp);
}

static int stdio_close(SDL_RWops *ctx)
{
    if (!ctx) return -1;
    if (ctx->hidden.stdio.autoclose && ctx->hidden.stdio.fp)
        fclose((FILE*)ctx->hidden.stdio.fp);
    free(ctx);
    return 0;
}

SDL_RWops* SDL_RWFromFile(const char *file, const char *mode)
{
    FILE *fp = fopen(file, mode);
    if (!fp) return (SDL_RWops*)0;

    SDL_RWops *rw = (SDL_RWops*)malloc(sizeof(SDL_RWops));
    if (!rw) { fclose(fp); return (SDL_RWops*)0; }
    memset(rw, 0, sizeof(SDL_RWops));

    rw->size  = stdio_size;
    rw->seek  = stdio_seek;
    rw->read  = stdio_read;
    rw->write = stdio_write;
    rw->close = stdio_close;
    rw->type  = 1; /* SDL_RWOPS_STDFILE */
    rw->hidden.stdio.fp = fp;
    rw->hidden.stdio.autoclose = 1;
    return rw;
}

/* ── Memory-backed RWops ────────────────────────────────────────────────── */

static Sint64 mem_size(SDL_RWops *ctx)
{
    return (Sint64)(ctx->hidden.mem.stop - ctx->hidden.mem.base);
}

static Sint64 mem_seek(SDL_RWops *ctx, Sint64 offset, int whence)
{
    Uint8 *newpos;
    switch (whence) {
        case RW_SEEK_SET: newpos = ctx->hidden.mem.base + offset; break;
        case RW_SEEK_CUR: newpos = ctx->hidden.mem.here + offset; break;
        case RW_SEEK_END: newpos = ctx->hidden.mem.stop + offset; break;
        default: return -1;
    }
    if (newpos < ctx->hidden.mem.base) newpos = ctx->hidden.mem.base;
    if (newpos > ctx->hidden.mem.stop) newpos = ctx->hidden.mem.stop;
    ctx->hidden.mem.here = newpos;
    return (Sint64)(newpos - ctx->hidden.mem.base);
}

static size_t mem_read(SDL_RWops *ctx, void *ptr, size_t size, size_t maxnum)
{
    size_t avail = (size_t)(ctx->hidden.mem.stop - ctx->hidden.mem.here);
    size_t total = size * maxnum;
    if (total > avail) {
        maxnum = (size > 0) ? (avail / size) : 0;
        total = size * maxnum;
    }
    if (total > 0) {
        memcpy(ptr, ctx->hidden.mem.here, total);
        ctx->hidden.mem.here += total;
    }
    return maxnum;
}

static size_t mem_write_rw(SDL_RWops *ctx, const void *ptr, size_t size, size_t num)
{
    size_t avail = (size_t)(ctx->hidden.mem.stop - ctx->hidden.mem.here);
    size_t total = size * num;
    if (total > avail) {
        num = (size > 0) ? (avail / size) : 0;
        total = size * num;
    }
    if (total > 0) {
        memcpy(ctx->hidden.mem.here, ptr, total);
        ctx->hidden.mem.here += total;
    }
    return num;
}

static size_t mem_write_const(SDL_RWops *ctx, const void *ptr, size_t size, size_t num)
{
    (void)ctx; (void)ptr; (void)size; (void)num;
    return 0;
}

static int mem_close(SDL_RWops *ctx)
{
    if (ctx) free(ctx);
    return 0;
}

SDL_RWops* SDL_RWFromMem(void *mem, int size)
{
    SDL_RWops *rw = (SDL_RWops*)malloc(sizeof(SDL_RWops));
    if (!rw) return (SDL_RWops*)0;
    memset(rw, 0, sizeof(SDL_RWops));

    rw->size  = mem_size;
    rw->seek  = mem_seek;
    rw->read  = mem_read;
    rw->write = mem_write_rw;
    rw->close = mem_close;
    rw->type  = 2;
    rw->hidden.mem.base = (Uint8*)mem;
    rw->hidden.mem.here = (Uint8*)mem;
    rw->hidden.mem.stop = (Uint8*)mem + size;
    return rw;
}

SDL_RWops* SDL_RWFromConstMem(const void *mem, int size)
{
    SDL_RWops *rw = (SDL_RWops*)malloc(sizeof(SDL_RWops));
    if (!rw) return (SDL_RWops*)0;
    memset(rw, 0, sizeof(SDL_RWops));

    rw->size  = mem_size;
    rw->seek  = mem_seek;
    rw->read  = mem_read;
    rw->write = mem_write_const;
    rw->close = mem_close;
    rw->type  = 3;
    rw->hidden.mem.base = (Uint8*)(unsigned long)mem;
    rw->hidden.mem.here = (Uint8*)(unsigned long)mem;
    rw->hidden.mem.stop = (Uint8*)(unsigned long)mem + size;
    return rw;
}

/* ── Convenience wrappers ───────────────────────────────────────────────── */

SDL_RWops* SDL_AllocRW(void)
{
    SDL_RWops *rw = (SDL_RWops*)malloc(sizeof(SDL_RWops));
    if (rw) memset(rw, 0, sizeof(SDL_RWops));
    return rw;
}

void SDL_FreeRW(SDL_RWops *area)
{
    if (area) free(area);
}

Sint64 SDL_RWsize(SDL_RWops *ctx)
{
    if (!ctx || !ctx->size) return -1;
    return ctx->size(ctx);
}

Sint64 SDL_RWseek(SDL_RWops *ctx, Sint64 offset, int whence)
{
    if (!ctx || !ctx->seek) return -1;
    return ctx->seek(ctx, offset, whence);
}

size_t SDL_RWread(SDL_RWops *ctx, void *ptr, size_t size, size_t maxnum)
{
    if (!ctx || !ctx->read) return 0;
    return ctx->read(ctx, ptr, size, maxnum);
}

size_t SDL_RWwrite(SDL_RWops *ctx, const void *ptr, size_t size, size_t num)
{
    if (!ctx || !ctx->write) return 0;
    return ctx->write(ctx, ptr, size, num);
}

int SDL_RWclose(SDL_RWops *ctx)
{
    if (!ctx || !ctx->close) return -1;
    return ctx->close(ctx);
}

int SDL_SaveBMP_RW(SDL_Surface *surface, SDL_RWops *dst, int freedst)
{
    (void)surface;
    if (freedst && dst) SDL_RWclose(dst);
    return -1;
}

SDL_Surface* SDL_LoadBMP_RW(SDL_RWops *src, int freesrc)
{
    (void)src;
    if (freesrc && src) SDL_RWclose(src);
    return (SDL_Surface*)0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Task 6: SDL_image via stb_image
 * ═══════════════════════════════════════════════════════════════════════════ */

SDL_Surface* IMG_Load(const char *file)
{
    /* STBI_NO_STDIO: read file ourselves, then decode from memory */
    FILE *f = fopen(file, "rb");
    if (!f) return (SDL_Surface*)0;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return (SDL_Surface*)0; }
    unsigned char *buf = (unsigned char*)malloc((size_t)sz);
    if (!buf) { fclose(f); return (SDL_Surface*)0; }
    fread(buf, 1, (size_t)sz, f);
    fclose(f);

    int w, h, channels;
    unsigned char *data = stbi_load_from_memory(buf, (int)sz, &w, &h, &channels, 4);
    free(buf);
    if (!data) return (SDL_Surface*)0;

    /* Create ARGB8888 surface */
    SDL_Surface *surf = SDL_CreateRGBSurface(0, w, h, 32,
                                             0x00FF0000, 0x0000FF00,
                                             0x000000FF, 0xFF000000);
    if (!surf) { stbi_image_free(data); return (SDL_Surface*)0; }

    /* Swizzle RGBA (stbi) → ARGB (SDL) */
    Uint32 *dst = (Uint32*)surf->pixels;
    for (int i = 0; i < w * h; i++) {
        Uint8 r = data[i * 4 + 0];
        Uint8 g = data[i * 4 + 1];
        Uint8 b = data[i * 4 + 2];
        Uint8 a = data[i * 4 + 3];
        dst[i] = ((Uint32)a << 24) | ((Uint32)r << 16)
               | ((Uint32)g << 8) | (Uint32)b;
    }

    stbi_image_free(data);
    return surf;
}

/* stbi callbacks for SDL_RWops */
static int stbi_rw_read(void *user, char *data, int size)
{
    SDL_RWops *rw = (SDL_RWops*)user;
    return (int)SDL_RWread(rw, data, 1, (size_t)size);
}

static void stbi_rw_skip(void *user, int n)
{
    SDL_RWops *rw = (SDL_RWops*)user;
    SDL_RWseek(rw, (Sint64)n, RW_SEEK_CUR);
}

static int stbi_rw_eof(void *user)
{
    SDL_RWops *rw = (SDL_RWops*)user;
    Sint64 cur = SDL_RWseek(rw, 0, RW_SEEK_CUR);
    Sint64 end = SDL_RWsize(rw);
    return (cur >= end) ? 1 : 0;
}

SDL_Surface* IMG_Load_RW(SDL_RWops *src, int freesrc)
{
    if (!src) return (SDL_Surface*)0;

    stbi_io_callbacks cb;
    cb.read = stbi_rw_read;
    cb.skip = stbi_rw_skip;
    cb.eof  = stbi_rw_eof;

    int w, h, channels;
    unsigned char *data = stbi_load_from_callbacks(&cb, src, &w, &h, &channels, 4);

    if (freesrc) SDL_RWclose(src);
    if (!data) return (SDL_Surface*)0;

    SDL_Surface *surf = SDL_CreateRGBSurface(0, w, h, 32,
                                             0x00FF0000, 0x0000FF00,
                                             0x000000FF, 0xFF000000);
    if (!surf) { stbi_image_free(data); return (SDL_Surface*)0; }

    Uint32 *dst = (Uint32*)surf->pixels;
    for (int i = 0; i < w * h; i++) {
        Uint8 r = data[i * 4 + 0];
        Uint8 g = data[i * 4 + 1];
        Uint8 b = data[i * 4 + 2];
        Uint8 a = data[i * 4 + 3];
        dst[i] = ((Uint32)a << 24) | ((Uint32)r << 16)
               | ((Uint32)g << 8) | (Uint32)b;
    }

    stbi_image_free(data);
    return surf;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Task 7: Event Helpers (audio impls live in sdl2_thread.c)
 * ═══════════════════════════════════════════════════════════════════════════ */

int SDL_WaitEvent(SDL_Event *event)
{
    while (!SDL_PollEvent(event)) {
        sys_sleep_ms(10);
    }
    return 1;
}

int SDL_WaitEventTimeout(SDL_Event *event, int timeout_ms)
{
    int waited = 0;
    while (!SDL_PollEvent(event)) {
        if (waited >= timeout_ms) return 0;
        sys_sleep_ms(10);
        waited += 10;
    }
    return 1;
}

int SDL_PushEvent(SDL_Event *event)
{
    if (!event) return -1;
    int next = (g_push_head + 1) % PUSH_QUEUE_SIZE;
    if (next == g_push_tail) return -1;  /* queue full */
    g_push_queue[g_push_head] = *event;
    g_push_head = next;
    return 1;
}

/* g_keyboard_state declared near top of file */

const Uint8* SDL_GetKeyboardState(int *numkeys)
{
    if (numkeys) *numkeys = 512;
    return g_keyboard_state;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Task 9: SDLPoP link stubs (legacy audio API now lives in sdl2_thread.c)
 * ═══════════════════════════════════════════════════════════════════════════ */

int SDL_InitSubSystem(Uint32 flags) { (void)flags; return 0; }

const char* SDL_GetScancodeName(int scancode)
{
    (void)scancode;
    return "?";
}

Uint64 SDL_GetPerformanceCounter(void) { return SDL_GetTicks(); }
Uint64 SDL_GetPerformanceFrequency(void) { return 1000; }

void SDL_SetWindowIcon(SDL_Window *w, SDL_Surface *icon)
{
    (void)w; (void)icon;
}

Sint64 SDL_RWtell(SDL_RWops *ctx)
{
    if (!ctx) return -1;
    return SDL_RWseek(ctx, 0, RW_SEEK_CUR);
}

int IMG_SavePNG(SDL_Surface *surface, const char *file)
{
    (void)surface; (void)file;
    return -1; /* not implemented */
}

int SDL_JoystickRumble(SDL_Joystick *j, Uint16 lo, Uint16 hi, Uint32 dur)
{
    (void)j; (void)lo; (void)hi; (void)dur;
    return -1;
}

SDL_Surface* IMG_LoadPNG_RW(SDL_RWops *src)
{
    return IMG_Load_RW(src, 0);
}

int IMG_SavePNG_RW(SDL_Surface *surface, SDL_RWops *dst, int freedst)
{
    (void)surface;
    if (freedst && dst) SDL_RWclose(dst);
    return -1; /* not implemented */
}

/* Layer 1 (thread/mutex/cond/sem) and Layer 2 (audio callback) implementations
 * live in sdl2_thread.c — they include pthread.h, which conflicts with the
 * host's <pthread.h> pulled in by stb_image.h's #include <stdlib.h>. */

/* ---------------------------------------------------------------------------
 * SDL_AudioCVT — format/channel/rate conversion.
 *
 * Wolf4SDL converts every digi sound (AUDIO_U8 / 1ch / 7042 Hz) up to the
 * mixer format (AUDIO_S16SYS / 2ch / 44100 Hz) at load time, so each Mix_Chunk
 * holds output-format PCM ready to mix. Real SDL chains a filter pipeline;
 * here everything runs inline since the cases we need are bounded.
 * ------------------------------------------------------------------------- */
static int audio_bytes_per_sample(SDL_AudioFormat fmt) {
    return (fmt & 0xFF) / 8;
}

int SDL_BuildAudioCVT(SDL_AudioCVT *cvt,
    SDL_AudioFormat src_fmt, Uint8 src_ch, int src_rate,
    SDL_AudioFormat dst_fmt, Uint8 dst_ch, int dst_rate)
{
    if (!cvt) return -1;
    memset(cvt, 0, sizeof(*cvt));
    cvt->src_format = src_fmt;
    cvt->dst_format = dst_fmt;
    cvt->src_channels = src_ch ? src_ch : 1;
    cvt->dst_channels = dst_ch ? dst_ch : 1;
    cvt->src_rate = src_rate;
    cvt->dst_rate = dst_rate;
    cvt->rate_incr = (double)dst_rate / (double)src_rate;
    int src_bps = audio_bytes_per_sample(src_fmt); if (src_bps == 0) src_bps = 1;
    int dst_bps = audio_bytes_per_sample(dst_fmt); if (dst_bps == 0) dst_bps = 1;
    cvt->len_ratio = cvt->rate_incr
                   * ((double)cvt->dst_channels / (double)cvt->src_channels)
                   * ((double)dst_bps / (double)src_bps);
    int mult = (int)cvt->len_ratio + 2; /* small safety margin */
    if (mult < 2) mult = 2;
    cvt->len_mult = mult;
    cvt->needed = (src_fmt != dst_fmt
                   || src_ch  != dst_ch
                   || src_rate != dst_rate) ? 1 : 0;
    return cvt->needed;
}

static Sint16 cvt_read_sample(const Uint8 *src, int frame, int ch,
                              SDL_AudioFormat fmt, int src_ch)
{
    int idx_ch = (src_ch == 1) ? 0 : (ch < src_ch ? ch : src_ch - 1);
    int bps = audio_bytes_per_sample(fmt);
    int byte_idx = (frame * src_ch + idx_ch) * bps;
    if (bps == 1) {
        int v = (int)src[byte_idx] - 128;
        return (Sint16)(v * 256);
    }
    return *(const Sint16*)(src + byte_idx);
}

static void cvt_write_sample(Uint8 *dst, int frame, int ch,
                             SDL_AudioFormat fmt, int dst_ch, Sint16 val)
{
    int bps = audio_bytes_per_sample(fmt);
    int byte_idx = (frame * dst_ch + ch) * bps;
    if (bps == 1) {
        int v = (int)val / 256 + 128;
        if (v < 0) v = 0; else if (v > 255) v = 255;
        dst[byte_idx] = (Uint8)v;
    } else {
        *(Sint16*)(dst + byte_idx) = val;
    }
}

int SDL_ConvertAudio(SDL_AudioCVT *cvt)
{
    if (!cvt || !cvt->buf) return -1;
    if (!cvt->needed) { cvt->len_cvt = cvt->len; return 0; }

    int src_bps = audio_bytes_per_sample(cvt->src_format); if (src_bps == 0) src_bps = 1;
    int dst_bps = audio_bytes_per_sample(cvt->dst_format); if (dst_bps == 0) dst_bps = 1;
    int src_ch  = cvt->src_channels ? cvt->src_channels : 1;
    int dst_ch  = cvt->dst_channels ? cvt->dst_channels : 1;

    int src_frames = cvt->len / (src_bps * src_ch);
    if (src_frames <= 0) { cvt->len_cvt = 0; return 0; }

    int dst_frames = (int)((double)src_frames * cvt->rate_incr);
    if (dst_frames < 1) dst_frames = 1;
    int dst_size = dst_frames * dst_ch * dst_bps;
    int max_size = cvt->len * cvt->len_mult;
    if (dst_size > max_size) {
        dst_frames = max_size / (dst_ch * dst_bps);
        dst_size = dst_frames * dst_ch * dst_bps;
    }

    /* Read from a copy because we expand in-place into the same buffer. */
    Uint8 *staging = (Uint8*)malloc(cvt->len);
    if (!staging) return -1;
    memcpy(staging, cvt->buf, cvt->len);

    for (int f = 0; f < dst_frames; f++) {
        int sf = (int)((double)f / cvt->rate_incr);
        if (sf >= src_frames) sf = src_frames - 1;
        for (int c = 0; c < dst_ch; c++) {
            Sint16 v = cvt_read_sample(staging, sf, c, cvt->src_format, src_ch);
            cvt_write_sample(cvt->buf, f, c, cvt->dst_format, dst_ch, v);
        }
    }

    free(staging);
    cvt->len_cvt = dst_size;
    return 0;
}
