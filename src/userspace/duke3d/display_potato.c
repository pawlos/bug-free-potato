/* display_potato.c -- potatOS display/input/timer driver for Chocolate Duke3D
 *
 * Replaces Engine/src/display.c (the SDL driver).
 * Implements the ~30 functions declared in display.h that the BUILD engine
 * and Duke3D game code call for video, keyboard, mouse, timer, and drawing.
 */

#include "platform.h"
#include "build.h"
#include "display.h"
#include "fixedPoint_math.h"
#include "engine.h"
#include "draw.h"
#include "cache.h"

#include "libc/syscall.h"
#include "libc/string.h"
#include "libc/stdlib.h"
#include "libc/stdio.h"

/* ── globals expected by the engine ──────────────────────────────────── */

int _argc;
char **_argv;

int32_t xres, yres, bytesperline, imageSize, maxpages;
uint8_t *screen, vesachecked;
int32_t buffermode, origbuffermode, linearmode;
uint8_t permanentupdate = 0, vgacompatible;
uint8_t moustat;
int32_t *horizlookup, *horizlookup2, horizycent;
int32_t oxdimen, oviewingrange, oxyaspect;
int32_t curbrightness;
int32_t qsetmode;
int32_t pageoffset, ydim16;
uint8_t *frameplace;
uint8_t *frameoffset;
uint8_t textfont[1024], smalltextfont[1024];
uint8_t pow2char[8] = { 1, 2, 4, 8, 16, 32, 64, 128 };
int32_t stereomode, visualpage, activepage, whiteband, blackband;
int32_t searchx, searchy;
int32_t wx1, wy1, wx2, wy2, ydimen;
int32_t xdimen, xdimenrecip, halfxdimen, xdimenscale, xdimscale;
uint8_t permanentlock;

int32_t total_render_time = 1;
int32_t total_rendered_frames = 0;

/* Referenced by engine.c for palette caching */
uint8_t lastPalette[768];

/* ── internal state ──────────────────────────────────────────────────── */

/* 8-bit framebuffer that the engine writes to */
static uint8_t *framebuf = NULL;
static int fb_w = 0, fb_h = 0;

/* 256-entry palette: RGBA, expanded from BUILD's 0-63 VGA range to 0-255 */
static uint8_t palette_rgba[256 * 4];

/* RGB24 output buffer for sys_draw_pixels (at 2x scale) */
#define DUKE_SCALE 1
static uint8_t *rgb_buf = NULL;
static int disp_w = 0, disp_h = 0;

/* Screen allocation tracking (mirrors original display.c) */
static uint8_t screenalloctype = 255;

/* Window ID */
static long g_wid = -1;

/* Mouse accumulator */
static int32_t mouse_rel_x = 0, mouse_rel_y = 0;
static short mouse_btn = 0;

/* Keyboard: lastkey for keyhandler() */
static unsigned int lastkey = 0;

/* Timer state */
static int64_t timer_freq = 0;      /* platform ticks per second */
static int32_t timer_tics_per_sec;
static int32_t timer_last_sample;
static void (*usertimercallback)(void) = NULL;

/* ── timer platform functions ────────────────────────────────────────── */

int TIMER_GetPlatformTicksInOneSecond(int64_t *t)
{
    *t = 1000000;  /* we use microseconds */
    return 1;
}

void TIMER_GetPlatformTicks(int64_t *t)
{
    *t = (int64_t)sys_get_micros();
}

/* ── platform_init ───────────────────────────────────────────────────── */

extern char game_dir[512];

void _platform_init(int argc, char **argv, const char *title, const char *iconName)
{
    (void)title; (void)iconName;
    _argc = argc;
    _argv = argv;

    /* Set game_dir so the engine can find duke3d.grp.
     * Try to derive from argv[0], fall back to known install path. */
    if (argc > 0 && argv[0]) {
        strncpy(game_dir, argv[0], sizeof(game_dir) - 1);
        game_dir[sizeof(game_dir) - 1] = '\0';
        char *slash = strrchr(game_dir, '/');
        if (slash) *slash = '\0';
        else game_dir[0] = '\0';
    }
    if (game_dir[0] == '\0') {
        strcpy(game_dir, "GAMES/DUKE3D");
    }
}

/* ── video mode ──────────────────────────────────────────────────────── */

static void init_new_res_vars(int32_t davidoption)
{
    int i, j;

    xdim = xres = fb_w;
    ydim = yres = fb_h;
    bytesperline = fb_w;
    vesachecked = 1;
    vgacompatible = 1;
    linearmode = 1;
    qsetmode = fb_h;
    activepage = visualpage = 0;

    frameoffset = frameplace = framebuf;

    if (screen != NULL) {
        if (screenalloctype == 0) kkfree((void *)screen);
        if (screenalloctype == 1) suckcache((intptr_t *)screen);
        screen = NULL;
    }

    switch (vidoption) {
    case 1: i = xdim * ydim; break;
    case 2: xdim = 320; ydim = 200; i = xdim * ydim; break;
    default: i = xdim * ydim; break;
    }

    j = ydim * 4 * sizeof(int32_t);
    if (horizlookup) free(horizlookup);
    if (horizlookup2) free(horizlookup2);
    horizlookup  = (int32_t *)malloc(j);
    horizlookup2 = (int32_t *)malloc(j);

    j = 0;
    for (i = 0; i <= ydim; i++) {
        ylookup[i] = j;
        j += bytesperline;
    }

    horizycent = ((ydim * 4) >> 1);
    oxyaspect = oxdimen = oviewingrange = -1;

    /* Tell the draw module how many bytes to skip per row */
    setBytesPerLine(bytesperline);

    /* Set 3D viewport to full screen */
    setview(0L, 0L, xdim - 1, ydim - 1);

    /* Apply initial palette so something is visible */
    setbrightness(curbrightness, palette);

    setupmouse();
    (void)davidoption;
}

int32_t _setgamemode(uint8_t davidoption, int32_t daxdim, int32_t daydim)
{
    if (daxdim > MAXXDIM || daydim > MAXYDIM) {
        daxdim = 640;
        daydim = 480;
    }

    fb_w = daxdim;
    fb_h = daydim;

    /* Allocate 8-bit framebuffer */
    if (framebuf) free(framebuf);
    framebuf = (uint8_t *)malloc(fb_w * fb_h);
    memset(framebuf, 0, fb_w * fb_h);

    /* Allocate scaled RGB24 output buffer */
    disp_w = fb_w * DUKE_SCALE;
    disp_h = fb_h * DUKE_SCALE;
    if (rgb_buf) free(rgb_buf);
    rgb_buf = (uint8_t *)malloc(disp_w * disp_h * 3);

    /* Create window */
    if (g_wid >= 0) sys_destroy_window(g_wid);
    int scr_w = (int)sys_fb_width();
    int scr_h = (int)sys_fb_height();
    int cx = (scr_w - disp_w) / 2;
    int cy = (scr_h - disp_h) / 2;
    if (cx < 0) cx = 0;
    if (cy < 18) cy = 18;  /* room for title bar */
    g_wid = sys_create_window(cx, cy, disp_w, disp_h);
    sys_set_window_title(g_wid, "Duke Nukem 3D");

    vidoption = 1;
    getvalidvesamodes();
    init_new_res_vars((int32_t)davidoption);

    qsetmode = 200;
    return 0;
}

void setvmode(int mode)
{
    /* Not needed — we set mode in _setgamemode */
    (void)mode;
}

void getvalidvesamodes(void)
{
    static int done = 0;
    if (done) return;
    done = 1;

    /* Offer a few resolutions */
    validmodecnt = 0;
    validmodexdim[validmodecnt] = 320;  validmodeydim[validmodecnt] = 200; validmodecnt++;
    validmodexdim[validmodecnt] = 320;  validmodeydim[validmodecnt] = 240; validmodecnt++;
    validmodexdim[validmodecnt] = 640;  validmodeydim[validmodecnt] = 480; validmodecnt++;
}

void _uninitengine(void)
{
    if (g_wid >= 0) { sys_destroy_window(g_wid); g_wid = -1; }
    if (framebuf) { free(framebuf); framebuf = NULL; }
    if (rgb_buf) { free(rgb_buf); rgb_buf = NULL; }
    /* On potatOS, terminate immediately — the remaining shutdown code
       (CONFIG_WriteSetup, uninitgroupfile, etc.) is not essential and
       the game loop can resume before exit() is reached otherwise. */
    exit(0);
}

/* ── serial debug helper ─────────────────────────────────────────────── */
void dbg(const char *msg)
{
    int len = 0;
    while (msg[len]) len++;
    sys_write_serial(msg, len);
}

void dbg_int(const char *prefix, int val)
{
    dbg(prefix);
    char buf[16];
    int neg = 0, i = 0;
    if (val < 0) { neg = 1; val = -val; }
    if (val == 0) buf[i++] = '0';
    else while (val > 0) { buf[i++] = '0' + (val % 10); val /= 10; }
    char out[20];
    int o = 0;
    if (neg) out[o++] = '-';
    while (i > 0) out[o++] = buf[--i];
    out[o++] = '\n';
    out[o] = 0;
    sys_write_serial(out, o);
}

/* ── frame presentation ──────────────────────────────────────────────── */

static int frame_counter = 0;

void _nextpage(void)
{
    uint32_t ticks;

    _handle_events();
    frame_counter++;
    if ((frame_counter & 0x3F) == 0) /* every 64 frames */
        dbg_int("F:", frame_counter);

    if (!framebuf || !rgb_buf || g_wid < 0) return;

    /* Convert 8-bit paletted framebuffer → RGB24 at 2x scale */
    int sy, sx;
    for (sy = 0; sy < fb_h; sy++) {
        uint8_t *row = rgb_buf + (sy * DUKE_SCALE) * disp_w * 3;
        uint8_t *rp = row;
        for (sx = 0; sx < fb_w; sx++) {
            unsigned int idx = (unsigned int)framebuf[sy * fb_w + sx] * 4u;
            uint8_t r = palette_rgba[idx + 0];
            uint8_t g = palette_rgba[idx + 1];
            uint8_t b = palette_rgba[idx + 2];
            int s;
            for (s = 0; s < DUKE_SCALE; s++) {
                *rp++ = r; *rp++ = g; *rp++ = b;
            }
        }
        /* Duplicate row for vertical scaling */
        int row_bytes = disp_w * 3;
        int s;
        for (s = 1; s < DUKE_SCALE; s++)
            memcpy(row + s * row_bytes, row, row_bytes);
    }

    sys_draw_pixels(rgb_buf, 0, 0, disp_w, disp_h);

    ticks = getticks();
    total_render_time = (ticks - total_rendered_frames);
    if (total_render_time > 1000) {
        total_rendered_frames = 0;
        total_render_time = 1;
    }
    total_rendered_frames++;
}

void _updateScreenRect(int32_t x, int32_t y, int32_t w, int32_t h)
{
    /* We do a full-screen blit in _nextpage, ignore partial updates */
    (void)x; (void)y; (void)w; (void)h;
}

void *_getVideoBase(void)
{
    return framebuf;
}

void clear2dscreen(void)
{
    if (framebuf)
        memset(framebuf, 0, fb_w * fb_h);
}

/* ── palette ─────────────────────────────────────────────────────────── */

int VBE_setPalette(uint8_t *palettebuffer)
{
    /* BUILD palette format: 256 entries × 4 bytes (B, G, R, unused), range 0-63 */
    uint8_t *p = palettebuffer;
    int i;
    for (i = 0; i < 256; i++) {
        uint8_t b = (uint8_t)((((float)*p++) / 63.0f) * 255.0f);
        uint8_t g = (uint8_t)((((float)*p++) / 63.0f) * 255.0f);
        uint8_t r = (uint8_t)((((float)*p++) / 63.0f) * 255.0f);
        p++;  /* skip unused byte */
        palette_rgba[i * 4 + 0] = r;
        palette_rgba[i * 4 + 1] = g;
        palette_rgba[i * 4 + 2] = b;
        palette_rgba[i * 4 + 3] = 255;
    }
    return 1;
}

int VBE_getPalette(int32_t start, int32_t num, uint8_t *dapal)
{
    uint8_t *p = dapal + (start * 4);
    int i;
    for (i = 0; i < num; i++) {
        int idx = (start + i) * 4;
        *p++ = (uint8_t)((((float)palette_rgba[idx + 2]) / 255.0f) * 63.0f); /* B */
        *p++ = (uint8_t)((((float)palette_rgba[idx + 1]) / 255.0f) * 63.0f); /* G */
        *p++ = (uint8_t)((((float)palette_rgba[idx + 0]) / 255.0f) * 63.0f); /* R */
        *p++ = 0;
    }
    return 1;
}

/* ── keyboard input ──────────────────────────────────────────────────── */

/* The BUILD engine keyboard system works like this:
 * - _handle_events() polls input and sets `lastkey` to a DOS scancode
 * - keyhandler() (in Game/src/keyboard.c) reads it via _readlastkeyhit()
 * - For key release, lastkey = scancode | 0x80
 * - Extended keys: send 0xE0 first, then the base scancode
 *
 * Our PS/2 set-1 scancodes ARE DOS scancodes, so no translation needed!
 * We just need to handle extended keys (arrows, home, end, etc.) by
 * sending the 0xE0 prefix.
 */

/* Scancodes that need E0 prefix in DOS convention */
static int is_extended_key(int sc)
{
    switch (sc) {
    case 0x47: /* Home */
    case 0x48: /* Up */
    case 0x49: /* PgUp */
    case 0x4B: /* Left */
    case 0x4D: /* Right */
    case 0x4F: /* End */
    case 0x50: /* Down */
    case 0x51: /* PgDn */
    case 0x52: /* Insert */
    case 0x53: /* Delete */
        return 1;
    default:
        return 0;
    }
}

void _handle_events(void)
{
    /* Poll keyboard */
    for (;;) {
        long ev = (g_wid >= 0) ? sys_get_window_event(g_wid) : sys_get_key_event();
        if (ev == 0 || ev == -1) break;

        int pressed  = (ev & 0x100) != 0;
        int scancode = ev & 0xFF;

        if (is_extended_key(scancode)) {
            /* Send E0 prefix first */
            lastkey = 0xE0;
            keyhandler();
        }

        lastkey = scancode;
        if (!pressed) lastkey += 128;  /* bit 7 = released in DOS */
        keyhandler();
    }

    /* Poll mouse */
    for (;;) {
        long mev = sys_get_mouse_event();
        if (mev == -1) break;
        int dx = (signed char)(mev & 0xFF);
        int dy = (signed char)((mev >> 8) & 0xFF);
        int lb = (int)((mev >> 16) & 1);
        int rb = (int)((mev >> 17) & 1);
        mouse_rel_x += dx;
        mouse_rel_y += dy;
        mouse_btn = 0;
        if (lb) mouse_btn |= 1;
        if (rb) mouse_btn |= 2;
    }

    /* Drive timer (call timerhandler for accumulated ticks) */
    sampletimer();

    /* Pump audio mixer — no separate audio thread on potatOS */
    extern void DSL_PumpAudio(void);
    DSL_PumpAudio();
}

void initkeys(void) { }
void uninitkeys(void) { }

uint8_t _readlastkeyhit(void)
{
    return (uint8_t)lastkey;
}

void keyhandler(void);  /* defined in Game/src/keyboard.c */

/* ── mouse ───────────────────────────────────────────────────────────── */

int setupmouse(void)
{
    mouse_rel_x = mouse_rel_y = 0;
    moustat = 1;
    return 1;
}

void readmousexy(short *x, short *y)
{
    if (x) *x = (short)(mouse_rel_x << 2);
    if (y) *y = (short)(mouse_rel_y << 2);
    mouse_rel_x = mouse_rel_y = 0;
}

void readmousebstatus(short *bstatus)
{
    if (bstatus) *bstatus = mouse_btn;
}

/* ── timer ───────────────────────────────────────────────────────────── */

int inittimer(int tickspersecond)
{
    int64_t t;
    if (timer_freq) return 0;

    if (!TIMER_GetPlatformTicksInOneSecond(&t))
        return -1;

    timer_freq = t;
    timer_tics_per_sec = tickspersecond;
    TIMER_GetPlatformTicks(&t);
    timer_last_sample = (int32_t)(t * timer_tics_per_sec / timer_freq);
    usertimercallback = NULL;
    return 0;
}

void uninittimer(void)
{
    if (!timer_freq) return;
    timer_freq = 0;
    timer_tics_per_sec = 0;
}

void sampletimer(void)
{
    int64_t i;
    int32_t n;
    if (!timer_freq) return;

    TIMER_GetPlatformTicks(&i);
    n = (int32_t)(i * timer_tics_per_sec / timer_freq) - timer_last_sample;
    if (n > 0) {
        totalclock += n;
        timer_last_sample += n;
    }
    static int32_t last_reported_tc = -1;
    if ((totalclock & 0xFF) == 0 && totalclock != last_reported_tc) {
        last_reported_tc = totalclock;
        dbg_int("TC:", (int)totalclock);
    }
    if (usertimercallback) {
        for (; n > 0; n--)
            usertimercallback();
    }
}

uint32_t getticks(void)
{
    int64_t i;
    TIMER_GetPlatformTicks(&i);
    return (uint32_t)(i * (int64_t)1000 / timer_freq);
}

int gettimerfreq(void)
{
    return timer_tics_per_sec;
}

/* ── joystick stubs ──────────────────────────────────────────────────── */

void _joystick_init(void) { }
void _joystick_deinit(void) { }
int _joystick_update(void) { return 0; }
int _joystick_axis(int axis) { (void)axis; return 0; }
int _joystick_hat(int hat) { (void)hat; return -1; }
int _joystick_button(int button) { (void)button; return 0; }

/* ── drawing primitives (used by BUILD map editor, minimal for game) ─ */

static uint8_t drawpixel_color = 0;

uint8_t readpixel(uint8_t *offset)
{
    return *offset;
}

void drawpixel(uint8_t *location, uint8_t pixel)
{
    *location = pixel;
}

void setcolor16(uint8_t col)
{
    drawpixel_color = col;
}

void drawpixel16(int32_t offset)
{
    if (framebuf)
        framebuf[offset] = drawpixel_color;
}

void fillscreen16(int32_t offset, int32_t color, int32_t blocksize)
{
    if (framebuf)
        memset(framebuf + offset, (uint8_t)color, blocksize);
}

void drawline16(int32_t XStart, int32_t YStart, int32_t XEnd, int32_t YEnd, uint8_t Color)
{
    /* Bresenham line -- needed for map editor, minimal for game */
    (void)XStart; (void)YStart; (void)XEnd; (void)YEnd; (void)Color;
}

/* Override FixFilePath: our FAT32 is case-insensitive, no fixup needed.
 * The original scans directories to case-correct each path component,
 * but our access() and opendir stubs cause it to mangle paths instead. */
void FixFilePath(char *filename) { (void)filename; }

/* Override findGRPToUse: skip directory scanning, just construct the path */
void findGRPToUse(char *groupfilefullpath)
{
    if (game_dir[0] != '\0')
        sprintf(groupfilefullpath, "%s/DUKE3D.GRP", game_dir);
    else
        strcpy(groupfilefullpath, "DUKE3D.GRP");
}

int screencapture(char *filename, uint8_t inverseit)
{
    /* No screenshot support on potatOS */
    (void)filename; (void)inverseit;
    return -1;
}

/* ── idle ────────────────────────────────────────────────────────────── */

void _idle(void)
{
    _handle_events();
    sys_yield();
}

/* ── misc stubs ──────────────────────────────────────────────────────── */

void initmultiplayers(uint8_t damultioption, uint8_t dacomrateoption, uint8_t dapriority)
{
    (void)damultioption; (void)dacomrateoption; (void)dapriority;
}

/* strcasecmp for the compat layer */
int strcasecmp(const char *a, const char *b)
{
    while (*a && *b) {
        int ca = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a;
        int cb = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}
