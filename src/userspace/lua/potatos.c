/* potatos.c — potatOS glue for Lua: REPL input + the potatos.* module.
   Task 1 ships a stub readline (EOF); Task 2 implements the real one and
   Task 3+ fills in luaopen_potatos. */
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include <syscall.h>   /* potatOS libc syscalls */
#include <stdio.h>     /* putchar, fputs, fflush */
#include <stdlib.h>    /* malloc, free */

/* The interactive REPL runs in its own focused text window (WF_TEXT), created
   lazily on the first prompt. This is what makes the REPL usable: a focused
   window receives keystrokes via sys_read_key() (bypassing the kernel shell,
   which otherwise drains the vterm input ring), and its stdout renders into the
   window via the WF_TEXT SYS_WRITE routing. Script mode never calls readline,
   so no window is created there. */
static long g_lua_wid   = -1;
static int  g_win_tried = 0;

static void destroy_lua_window(void) {
    if (g_lua_wid >= 0) { sys_destroy_window(g_lua_wid); g_lua_wid = -1; }
}

/* Interactive REPL reader: prints the prompt, then polls the keyboard, echoing
   printable chars and handling backspace, until Enter. Mirrors the proven
   input loop in src/userspace/sh.c readline(). libc stdin is a stub, so Lua's
   default fgets(stdin) reader cannot be used. Returns 1 on a completed line,
   0 on EOF (Ctrl-D on an empty line). The line is stored NUL-terminated
   without the trailing newline. buf is the frontend's LUA_MAXINPUT buffer. */
int potato_readline(lua_State *L, char *buf, const char *prompt) {
    (void)L;
    if (!g_win_tried) {
        g_win_tried = 1;
        long w = sys_create_window_text(40, 40, 640, 400);
        if (w >= 0) {
            g_lua_wid = w;
            sys_set_window_title(w, "Lua REPL");
            atexit(destroy_lua_window);   /* return focus to shell on exit */
        }
    }
    int len = 0;
    buf[0] = '\0';
    fputs(prompt, stdout);
    fflush(stdout);
    for (;;) {
        long ch = sys_read_key();
        if (ch < 0) { sys_yield(); continue; }
        if (ch == 4) {                       /* Ctrl-D = EOF */
            if (len == 0) { putchar('\n'); return 0; }
            continue;
        }
        if (ch == '\r' || ch == '\n') {
            putchar('\n');
            buf[len] = '\0';
            return 1;
        }
        if (ch == '\b' || ch == 127) {
            if (len > 0) { len--; buf[len] = '\0';
                           putchar('\b'); putchar(' '); putchar('\b'); }
            continue;
        }
        if (ch >= 0x20 && ch < 0x7F && len < LUA_MAXINPUT - 1) {
            buf[len++] = (char)ch;
            buf[len]   = '\0';
            putchar((char)ch);
        }
    }
}

/* ── potatos.* module: thin wrappers over potatOS syscalls ───────────────── */

static int l_fb_width(lua_State *L)  { lua_pushinteger(L, sys_fb_width());  return 1; }
static int l_fb_height(lua_State *L) { lua_pushinteger(L, sys_fb_height()); return 1; }

static int l_fill_rect(lua_State *L) {
    long x = (long)luaL_checkinteger(L, 1), y = (long)luaL_checkinteger(L, 2);
    long w = (long)luaL_checkinteger(L, 3), h = (long)luaL_checkinteger(L, 4);
    long rgb = (long)luaL_checkinteger(L, 5);
    sys_fill_rect(x, y, w, h, rgb);
    return 0;
}

static int l_draw_pixels(lua_State *L) {
    size_t n;
    const char *pbuf = luaL_checklstring(L, 1, &n);   /* raw 32bpp pixel bytes */
    long x = (long)luaL_checkinteger(L, 2), y = (long)luaL_checkinteger(L, 3);
    long w = (long)luaL_checkinteger(L, 4), h = (long)luaL_checkinteger(L, 5);
    luaL_argcheck(L, w > 0 && h > 0, 4, "width/height must be positive");
    luaL_argcheck(L, n >= (size_t)(w * h * 4), 1, "pixel buffer too small");
    sys_draw_pixels(pbuf, x, y, w, h);
    return 0;
}

/* draw_text(x, y, s, fg [, bg])  — bg defaults to 0 (black). */
static int l_draw_text(lua_State *L) {
    long x = (long)luaL_checkinteger(L, 1), y = (long)luaL_checkinteger(L, 2);
    const char *s = luaL_checkstring(L, 3);
    long fg = (long)luaL_checkinteger(L, 4);
    long bg = (long)luaL_optinteger(L, 5, 0);
    sys_draw_text(x, y, s, fg, bg);
    return 0;
}

static int l_read_key(lua_State *L) {
    long ch = sys_read_key();
    if (ch < 0) lua_pushnil(L); else lua_pushinteger(L, ch);
    return 1;
}

static int l_ticks(lua_State *L)  { lua_pushinteger(L, (lua_Integer)sys_get_ticks());  return 1; }
static int l_micros(lua_State *L) { lua_pushinteger(L, (lua_Integer)sys_get_micros()); return 1; }
static int l_sleep(lua_State *L)  { sys_sleep_ms((unsigned long)luaL_checkinteger(L, 1)); return 0; }

/* beep(freq_hz, ms) — generate a 16-bit signed stereo square wave and submit
   it to the AC97 mixer. Returns true if accepted, false if busy/absent. */
static int l_beep(lua_State *L) {
    long freq = (long)luaL_checkinteger(L, 1);
    long ms   = (long)luaL_checkinteger(L, 2);
    const unsigned rate = 22050;
    if (freq < 20)   freq = 20;
    if (ms   < 1)    ms   = 1;
    if (ms   > 2000) ms   = 2000;               /* cap buffer size */
    long frames = ((long)rate * ms) / 1000;
    short *pcm = (short *)malloc((size_t)frames * 2 * sizeof(short));  /* L+R */
    if (!pcm) { lua_pushboolean(L, 0); return 1; }
    long period = (long)rate / freq;            /* square-wave period, frames */
    if (period < 2) period = 2;
    for (long i = 0; i < frames; i++) {
        short s = ((i % period) < (period / 2)) ? 12000 : -12000;
        pcm[2 * i]     = s;                      /* left  */
        pcm[2 * i + 1] = s;                      /* right */
    }
    /* SYS_AUDIO_WRITE returns bytes queued (>0 ok, 0 busy, -1 no device). */
    long rc = sys_audio_write(pcm, (unsigned long)frames * 2 * sizeof(short), rate);
    free(pcm);
    lua_pushboolean(L, rc > 0);
    return 1;
}

/* ── graphics window management ───────────────────────────────────────────
   Drawing (fill_rect/draw_pixels/draw_text) and read_key only work against a
   focused window: raw-framebuffer draws are overwritten by the compositor, and
   a non-windowed task's keystrokes are stolen by the kernel shell. open() gives
   a script a focused window; fb_width/fb_height then report its client size. */
static long g_gfx_wid = -1;

static void destroy_gfx_window(void) {
    if (g_gfx_wid >= 0) { sys_destroy_window(g_gfx_wid); g_gfx_wid = -1; }
}

/* potatos.open(w, h [, title]) -> boolean. Creates the script's window once. */
static int l_open(lua_State *L) {
    long w = (long)luaL_checkinteger(L, 1);
    long h = (long)luaL_checkinteger(L, 2);
    const char *title = luaL_optstring(L, 3, "Lua");
    if (g_gfx_wid < 0) {
        /* Text window: the script's stdout (print) renders into the window via
           WF_TEXT routing; fill_rect/draw_pixels still draw into the same
           pixel buffer, so this serves both text and graphics scripts. */
        long wid = sys_create_window_text(40, 40, w, h);
        if (wid < 0) { lua_pushboolean(L, 0); return 1; }
        g_gfx_wid = wid;
        sys_set_window_title(wid, title);
        atexit(destroy_gfx_window);   /* return focus to shell on exit */
    }
    lua_pushboolean(L, 1);
    return 1;
}

static const luaL_Reg potatos_funcs[] = {
    {"open",        l_open},
    {"fb_width",    l_fb_width},
    {"fb_height",   l_fb_height},
    {"fill_rect",   l_fill_rect},
    {"draw_pixels", l_draw_pixels},
    {"draw_text",   l_draw_text},
    {"read_key",    l_read_key},
    {"ticks",       l_ticks},
    {"micros",      l_micros},
    {"sleep",       l_sleep},
    {"beep",        l_beep},
    {NULL, NULL},
};

int luaopen_potatos(lua_State *L) {
    luaL_newlib(L, potatos_funcs);
    return 1;
}
