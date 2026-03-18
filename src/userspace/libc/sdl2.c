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

int SDL_UpdateTexture(SDL_Texture *texture, const SDL_Rect *rect,
                      const void *pixels, int pitch)
{
    if (!texture || !pixels) return -1;
    const unsigned char *src = (const unsigned char *)pixels;

    if (!rect) {
        /* Full texture update */
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

int SDL_PollEvent(SDL_Event *event)
{
    if (!event) return 0;

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
