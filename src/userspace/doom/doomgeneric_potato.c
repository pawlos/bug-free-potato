/* doomgeneric_potato.c — potatOS platform layer for doomgeneric
 *
 * Implements the six DG_* callbacks that doomgeneric requires plus main().
 * The WAD is read from the FAT32 disk as GAMES/DOOM/DOOM1.WAD.
 */

#include "doomgeneric.h"
#include "doomkeys.h"
#include <stddef.h>   /* NULL */

/* Use libc/ headers via the -I src/userspace include path */
#include "libc/syscall.h"
#include "libc/string.h"

/* ── display ─────────────────────────────────────────────────────────────── */
#define SCREEN_W   DOOMGENERIC_RESX   /* default 640 */
#define SCREEN_H   DOOMGENERIC_RESY   /* default 400 */

/* Packed RGB24 output buffer in .bss (640×400×3 = 768 000 bytes) */
static unsigned char rgb_buf[SCREEN_W * SCREEN_H * 3];

/* Window ID for this Doom instance (-1 = no window) */
static long g_wid = -1;

/* Minimum client_y to leave room for the title bar (TITLE_BAR_H=16, BORDER_W=1) */
#define MIN_CLIENT_Y 18

static void create_doom_window(void)
{
    if (g_wid >= 0) return;  /* already created */
    int fb_w = (int)sys_fb_width();
    int fb_h = (int)sys_fb_height();
    int cx = (fb_w - SCREEN_W) / 2;
    int cy = (fb_h - SCREEN_H) / 2;
    if (cx < 0) cx = 0;
    if (cy < MIN_CLIENT_Y) cy = MIN_CLIENT_Y;
    g_wid = sys_create_window(cx, cy, SCREEN_W, SCREEN_H);
}

void DG_Init(void)
{
    /* Window was already created in main() before doomgeneric_Create(),
       so all early Doom startup drawing is clipped to it.  No-op here. */
    create_doom_window();  /* idempotent guard just in case */
}

void DG_DrawFrame(void)
{
    /* DG_ScreenBuffer = SCREEN_W * SCREEN_H * uint32_t (XRGB8888).
       Convert to packed RGB24 for sys_draw_pixels. */
    pixel_t       *src = DG_ScreenBuffer;
    unsigned char *dst = rgb_buf;
    int n = SCREEN_W * SCREEN_H;
    while (n--) {
        unsigned int px = (unsigned int)*src++;
        *dst++ = (px >> 16) & 0xFF; /* R */
        *dst++ = (px >>  8) & 0xFF; /* G */
        *dst++ =  px        & 0xFF; /* B */
    }
    /* Draw at (0,0) relative to the window client area; the kernel translates
       to screen-absolute coordinates via SYS_DRAW_PIXELS window clipping. */
    sys_draw_pixels(rgb_buf, 0, 0, SCREEN_W, SCREEN_H);
}

/* ── timing ──────────────────────────────────────────────────────────────── */

void DG_SleepMs(uint32_t ms)
{
    if (!ms) return;
    sys_sleep_ms(ms);
}

uint32_t DG_GetTicksMs(void)
{
    /* Sub-tick precision via PIT latch — avoids 20 ms quantisation. */
    return (uint32_t)(sys_get_micros() / 1000ULL);
}

/* ── keyboard ────────────────────────────────────────────────────────────── */

/* Map PS/2 set-1 make-code → Doom KEY_* value. Returns 0 for unmapped. */
static unsigned char sc_to_doom(int sc)
{
    switch (sc) {
    /* Arrow keys */
    case 0x48: return KEY_UPARROW;
    case 0x50: return KEY_DOWNARROW;
    case 0x4B: return KEY_LEFTARROW;
    case 0x4D: return KEY_RIGHTARROW;

    /* Core game controls */
    case 0x01: return KEY_ESCAPE;
    case 0x1C: return KEY_ENTER;
    case 0x0F: return KEY_TAB;
    case 0x39: return KEY_USE;          /* Space  */
    case 0x1D: return KEY_FIRE;         /* LCtrl → fire */
    case 0x38: return KEY_RALT;         /* LAlt  → strafe */
    case 0x2A: case 0x36: return KEY_RSHIFT; /* Shift → run */
    case 0x0E: return KEY_BACKSPACE;

    /* Number row (weapon select) */
    case 0x02: return '1'; case 0x03: return '2'; case 0x04: return '3';
    case 0x05: return '4'; case 0x06: return '5'; case 0x07: return '6';
    case 0x08: return '7'; case 0x09: return '8'; case 0x0A: return '9';
    case 0x0B: return '0';
    case 0x0C: return KEY_MINUS;
    case 0x0D: return KEY_EQUALS;

    /* Letters (cheats / automap) */
    case 0x10: return 'q'; case 0x11: return 'w'; case 0x12: return 'e';
    case 0x13: return 'r'; case 0x14: return 't'; case 0x15: return 'y';
    case 0x16: return 'u'; case 0x17: return 'i'; case 0x18: return 'o';
    case 0x19: return 'p'; case 0x1E: return 'a'; case 0x1F: return 's';
    case 0x20: return 'd'; case 0x21: return 'f'; case 0x22: return 'g';
    case 0x23: return 'h'; case 0x24: return 'j'; case 0x25: return 'k';
    case 0x26: return 'l'; case 0x2C: return 'z'; case 0x2D: return 'x';
    case 0x2E: return 'c'; case 0x2F: return 'v'; case 0x30: return 'b';
    case 0x31: return 'n'; case 0x32: return 'm';

    /* F-keys */
    case 0x3B: return KEY_F1;  case 0x3C: return KEY_F2;
    case 0x3D: return KEY_F3;  case 0x3E: return KEY_F4;
    case 0x3F: return KEY_F5;  case 0x40: return KEY_F6;
    case 0x41: return KEY_F7;  case 0x42: return KEY_F8;
    case 0x43: return KEY_F9;  case 0x44: return KEY_F10;
    case 0x57: return KEY_F11; case 0x58: return KEY_F12;

    /* Navigation / extra */
    case 0x47: return KEY_HOME;
    case 0x4F: return KEY_END;
    case 0x49: return KEY_PGUP;
    case 0x51: return KEY_PGDN;

    default:   return 0;
    }
}

int DG_GetKey(int *pressed, unsigned char *doomKey)
{
    for (;;) {
        /* Poll the window's event ring so we only receive keys when focused. */
        long ev = (g_wid >= 0) ? sys_get_window_event(g_wid) : 0;
        if (ev == 0) return 0;
        int sc = (int)(ev & 0xFF);
        unsigned char dk = sc_to_doom(sc);
        if (!dk) continue;          /* ignore unmapped keys */
        *pressed = (ev & 0x100) ? 1 : 0;
        *doomKey  = dk;
        return 1;
    }
}

void DG_SetWindowTitle(const char *title) { sys_set_window_title(g_wid, title); }

/* ── entry point ─────────────────────────────────────────────────────────── */

int main(void)
{
    /* Create the window BEFORE doomgeneric_Create() so that all early Doom
       startup drawing (text, loading screens) is clipped to our window. */
    create_doom_window();

    /* Hardcode the IWAD path so Doom finds it on our FAT32 disk.
       The Makefile copies doom1.wad into GAMES/DOOM/ on the disk. */
    char *argv[] = { "doom", "-iwad", "GAMES/DOOM/DOOM1.WAD", NULL };
    doomgeneric_Create(3, argv);
    for (;;)
        doomgeneric_Tick();
    return 0;
}
