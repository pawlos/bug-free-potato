/* quakegeneric_potato.c — potatOS platform layer for quakegeneric
 *
 * Implements all QG_* callbacks required by quakegeneric plus main().
 *
 * Display pipeline:
 *   Quake 8-bit indexed frame (320×240)
 *   → palette lookup → packed RGB24
 *   → SYS_DRAW_PIXELS → windowed framebuffer
 *
 * Timing: main loop measures real elapsed microseconds via sys_get_micros()
 *         and passes a precise dt to QG_Tick() each frame.
 *
 * Assets: Pass "-basedir /" so Quake constructs "//id1/pak0.pak".
 *         VFS strips path to basename "pak0.pak" searched in the root dir.
 *         Place PAK0.PAK (uppercase) on the FAT32 disk before booting.
 */

#include "quakegeneric.h"
#include <stddef.h>   /* NULL */
#include <stdarg.h>   /* va_list for __wrap_Sys_Error */

/* Use libc/ headers via the -I src/userspace include path */
#include "libc/syscall.h"
#include "libc/string.h"
#include "libc/stdio.h"   /* printf, vsnprintf */
#include "libc/stdlib.h"  /* atexit, exit */

/* ── display ─────────────────────────────────────────────────────────────── */

#define QUAKE_W  QUAKEGENERIC_RES_X   /* 320 */
#define QUAKE_H  QUAKEGENERIC_RES_Y   /* 240 */

/* 256-entry RGB palette (set by Quake engine via QG_SetPalette) */
static unsigned char g_palette[768];

/* Packed RGB24 output buffer (320 × 240 × 3 = 230 400 bytes) */
static unsigned char rgb_buf[QUAKE_W * QUAKE_H * 3];

/* Window ID for this Quake instance (-1 = no window yet) */
static long g_wid = -1;

/* Leave room for the title bar (TITLE_BAR_H=16, BORDER_W=1) */
#define MIN_CLIENT_Y 18

static void create_quake_window(void)
{
    if (g_wid >= 0) return;   /* already created */
    int fb_w = (int)sys_fb_width();
    int fb_h = (int)sys_fb_height();
    int cx = (fb_w - QUAKE_W) / 2;
    int cy = (fb_h - QUAKE_H) / 2;
    if (cx < 0) cx = 0;
    if (cy < MIN_CLIENT_Y) cy = MIN_CLIENT_Y;
    g_wid = sys_create_window(cx, cy, QUAKE_W, QUAKE_H);
}

/* ── error visibility ─────────────────────────────────────────────────────── */

/* Registered as atexit handler: keep the window open so errors remain readable
 * before the task is torn down.  Registered FIRST so it runs LAST (LIFO). */
static void exit_pause(void)
{
    printf("\n[potatOS] Quake exited — window closes in 15 s\n");
    sys_sleep_ms(15000);
}

/* Write a string to COM1 serial (QEMU -serial stdio). */
static void serial_puts(const char *s, int len)
{
    if (len > 0)
        sys_write_serial(s, (size_t)len);
}

/* Hunk_AllocName override via --wrap=Hunk_AllocName.
 * Logs every hunk allocation to serial so we can see exactly which allocation
 * triggers the Hunk_Alloc: failed error (both name and size). */
extern void *__real_Hunk_AllocName(int size, char *name);
void *__wrap_Hunk_AllocName(int size, char *name)
{
    char buf[128];
    int n = snprintf(buf, sizeof(buf), "[HUNK] %7d  %s\n", size, name ? name : "(null)");
    serial_puts(buf, n);
    return __real_Hunk_AllocName(size, name);
}

/* Sys_Printf override via linker --wrap=Sys_Printf.
 * Routes all engine log output to both the window and serial. */
void __wrap_Sys_Printf(char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0) {
        printf("%s", buf);
        serial_puts(buf, n);
    }
}

/* Sys_Error override via linker --wrap=Sys_Error.
 * Formats the engine error message, then falls through to exit(1) which
 * calls exit_pause (via atexit) so the user can read the error. */
void __wrap_Sys_Error(char *error, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, error);
    vsnprintf(buf, sizeof(buf), error, ap);
    va_end(ap);
    const char *prefix = "\n** QUAKE ERROR: ";
    const char *suffix = " **\n";
    serial_puts(prefix, 17);
    serial_puts(buf, (int)strlen(buf));
    serial_puts(suffix, 4);
    printf("\n** QUAKE ERROR: %s **\n", buf);
    exit(1);   /* triggers atexit → exit_pause */
}

/* Called by quakegeneric before Host_Init */
void QG_Init(void)
{
    atexit(exit_pause);      /* register early so it runs last on exit */
    create_quake_window();   /* idempotent */
}

void QG_Quit(void)
{
    exit(0);   /* go through atexit → exit_pause, then sys_exit */
}

/* Store Quake's 768-byte (256 × RGB) palette */
void QG_SetPalette(unsigned char palette[768])
{
    int i;
    for (i = 0; i < 768; i++)
        g_palette[i] = palette[i];
}

/* Blit an 8-bit indexed 320×240 frame to the window */
void QG_DrawFrame(void *pixels)
{
    const unsigned char *src = (const unsigned char *)pixels;
    unsigned char *dst = rgb_buf;
    int n = QUAKE_W * QUAKE_H;
    while (n--) {
        unsigned int idx = (unsigned int)(*src++) * 3u;
        *dst++ = g_palette[idx + 0];   /* R */
        *dst++ = g_palette[idx + 1];   /* G */
        *dst++ = g_palette[idx + 2];   /* B */
    }
    /* SYS_DRAW_PIXELS blits to (0,0) in the window client area */
    sys_draw_pixels(rgb_buf, 0, 0, QUAKE_W, QUAKE_H);
}

/* ── keyboard ────────────────────────────────────────────────────────────── */

/* Map PS/2 set-1 make-code → Quake K_* value.  Returns 0 for unmapped. */
static int sc_to_quake(int sc)
{
    switch (sc) {
    /* Arrow keys */
    case 0x48: return K_UPARROW;
    case 0x50: return K_DOWNARROW;
    case 0x4B: return K_LEFTARROW;
    case 0x4D: return K_RIGHTARROW;

    /* Core keys */
    case 0x01: return K_ESCAPE;
    case 0x1C: return K_ENTER;
    case 0x0F: return K_TAB;
    case 0x39: return K_SPACE;
    case 0x0E: return K_BACKSPACE;
    case 0x1D: return K_CTRL;
    case 0x38: return K_ALT;
    case 0x2A: case 0x36: return K_SHIFT;

    /* F-keys */
    case 0x3B: return K_F1;  case 0x3C: return K_F2;
    case 0x3D: return K_F3;  case 0x3E: return K_F4;
    case 0x3F: return K_F5;  case 0x40: return K_F6;
    case 0x41: return K_F7;  case 0x42: return K_F8;
    case 0x43: return K_F9;  case 0x44: return K_F10;
    case 0x57: return K_F11; case 0x58: return K_F12;

    /* Navigation */
    case 0x47: return K_HOME;
    case 0x4F: return K_END;
    case 0x49: return K_PGUP;
    case 0x51: return K_PGDN;

    /* Letters (cheats / console) */
    case 0x10: return 'q'; case 0x11: return 'w'; case 0x12: return 'e';
    case 0x13: return 'r'; case 0x14: return 't'; case 0x15: return 'y';
    case 0x16: return 'u'; case 0x17: return 'i'; case 0x18: return 'o';
    case 0x19: return 'p'; case 0x1E: return 'a'; case 0x1F: return 's';
    case 0x20: return 'd'; case 0x21: return 'f'; case 0x22: return 'g';
    case 0x23: return 'h'; case 0x24: return 'j'; case 0x25: return 'k';
    case 0x26: return 'l'; case 0x2C: return 'z'; case 0x2D: return 'x';
    case 0x2E: return 'c'; case 0x2F: return 'v'; case 0x30: return 'b';
    case 0x31: return 'n'; case 0x32: return 'm';

    /* Number row */
    case 0x02: return '1'; case 0x03: return '2'; case 0x04: return '3';
    case 0x05: return '4'; case 0x06: return '5'; case 0x07: return '6';
    case 0x08: return '7'; case 0x09: return '8'; case 0x0A: return '9';
    case 0x0B: return '0';
    case 0x0C: return '-'; case 0x0D: return '=';
    case 0x1A: return '['; case 0x1B: return ']';
    case 0x27: return ';'; case 0x28: return '\'';
    case 0x29: return '`'; case 0x2B: return '\\';
    case 0x33: return ','; case 0x34: return '.'; case 0x35: return '/';

    default:   return 0;
    }
}

/* ── mouse ────────────────────────────────────────────────────────────────── */

/* Accumulated mouse deltas — drained by QG_GetMouseMove each frame */
static int g_mouse_dx = 0;
static int g_mouse_dy = 0;

/* Current mouse button states */
static int g_mb_left  = 0;
static int g_mb_right = 0;

/* Small queue for mouse button key-press/release events */
#define MBTQ_CAP 8
static struct { int key; int down; } g_mbtq[MBTQ_CAP];
static int g_mbtq_head = 0;
static int g_mbtq_tail = 0;

static void mbtq_push(int key, int down)
{
    int next = (g_mbtq_tail + 1) % MBTQ_CAP;
    if (next != g_mbtq_head) {   /* drop if full */
        g_mbtq[g_mbtq_tail].key  = key;
        g_mbtq[g_mbtq_tail].down = down;
        g_mbtq_tail = next;
    }
}

/* Drain all pending mouse events; update dx/dy and button queue */
static void poll_mouse(void)
{
    for (;;) {
        long mev = sys_get_mouse_event();
        if (mev == -1) break;
        int dx = (signed char)(mev & 0xFF);
        int dy = (signed char)((mev >> 8) & 0xFF);
        int lb  = (int)((mev >> 16) & 1);
        int rb  = (int)((mev >> 17) & 1);

        g_mouse_dx += dx;
        g_mouse_dy -= dy;   /* Quake: positive dy = look down */

        if (lb != g_mb_left)  { g_mb_left  = lb; mbtq_push(K_MOUSE1, lb); }
        if (rb != g_mb_right) { g_mb_right = rb; mbtq_push(K_MOUSE2, rb); }
    }
}

/* ── QG callbacks ────────────────────────────────────────────────────────── */

/*
 * QG_GetKey: return the next pending input event (keyboard or mouse button).
 * Returns 1 if an event was available, 0 if the queue is empty.
 * Called repeatedly by the engine until it returns 0.
 */
int QG_GetKey(int *down, int *key)
{
    /* First flush mouse events into the button queue + movement accumulators */
    poll_mouse();

    /* Return any queued mouse button event */
    if (g_mbtq_head != g_mbtq_tail) {
        *down = g_mbtq[g_mbtq_head].down;
        *key  = g_mbtq[g_mbtq_head].key;
        g_mbtq_head = (g_mbtq_head + 1) % MBTQ_CAP;
        return 1;
    }

    /* Then return one window keyboard event */
    for (;;) {
        long ev = (g_wid >= 0) ? sys_get_window_event(g_wid) : 0;
        if (ev == 0) return 0;
        int sc = (int)(ev & 0xFF);
        int qk = sc_to_quake(sc);
        if (!qk) continue;            /* skip unmapped scancodes */
        *down = (ev & 0x100) ? 1 : 0;
        *key  = qk;
        return 1;
    }
}

/* QG_GetMouseMove: drain accumulated mouse deltas for this frame */
void QG_GetMouseMove(int *x, int *y)
{
    poll_mouse();
    *x = g_mouse_dx;
    *y = g_mouse_dy;
    g_mouse_dx = 0;
    g_mouse_dy = 0;
}

/* QG_GetJoyAxes: no joystick hardware — return zeros */
void QG_GetJoyAxes(float *axes)
{
    int i;
    for (i = 0; i < QUAKEGENERIC_JOY_MAX_AXES; i++)
        axes[i] = 0.0f;
}

/* ── entry point ─────────────────────────────────────────────────────────── */

int main(void)
{
    /* Create window BEFORE QG_Create so early startup drawing is clipped */
    create_quake_window();

    /* "-basedir /" → Quake looks for "//id1/pak0.pak".
       VFS strips path to basename "pak0.pak" in the FAT32 root directory.
       Copy PAK0.PAK to the disk root before booting. */
    char *argv[] = { "quake", "-basedir", "/", NULL };
    QG_Create(3, argv);

    /* Main loop: measure real elapsed time in microseconds */
    unsigned long long last_us = sys_get_micros();
    for (;;) {
        unsigned long long now_us = sys_get_micros();
        double dt = (double)(now_us - last_us) * 0.000001;
        last_us = now_us;
        /* Cap dt to avoid spiral-of-death on long frames */
        if (dt > 0.2)  dt = 0.2;
        if (dt < 0.001) dt = 0.001;
        QG_Tick(dt);
    }
    return 0;
}
