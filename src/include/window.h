#pragma once
#include "defs.h"
#include "ansi.h"
#include "vterm.h"

class Framebuffer;

constexpr pt::uint32_t MAX_WINDOWS  = 8;
constexpr pt::uint32_t TITLE_BAR_H  = 16;  // px; matches PSF1 glyph height
constexpr pt::uint32_t BORDER_W     = 1;   // px; 1-px border on all sides
constexpr pt::uint32_t EVENT_CAP    = 32;  // per-window ring size (power of 2)
constexpr pt::uint32_t INVALID_WID  = 0xFFFFFFFF;

// Flags for create_window
constexpr pt::uint32_t WF_CHROMELESS = 1u; // no border or title bar; client = full rect

// 64-bit event encoding: bit 8 = pressed, bits 7:0 = PS/2 set-1 scancode.
// 0 is the sentinel for "no event" (scancode 0 is never emitted by PS/2).
constexpr pt::uint64_t WEV_KEY_PRESS_BIT = (pt::uint64_t)1 << 8;

inline pt::uint64_t wev_make_key(pt::uint8_t sc, bool pressed) {
    return (pt::uint64_t)sc | (pressed ? WEV_KEY_PRESS_BIT : 0u);
}

struct Window {
    pt::uint32_t id;
    pt::uint32_t owner_task_id;   // INVALID_WID = free slot
    pt::uint32_t vt_id;           // VT that owns this window
    bool         active;
    bool         chromeless;      // no border/title bar; client area = full rect

    // Outer frame position and size (includes border + title bar)
    pt::uint32_t screen_x, screen_y;
    pt::uint32_t total_w,  total_h;

    // Client area origin (screen-absolute, pre-computed for hot path)
    // client_ox = screen_x + BORDER_W
    // client_oy = screen_y + BORDER_W + TITLE_BAR_H
    pt::uint32_t client_ox, client_oy;
    pt::uint32_t client_w,  client_h;

    // Per-window pixel buffer (client_w × client_h, ARGB32).
    // All window rendering goes here; compositor blits to back buffer.
    pt::uint32_t* pixel_buf;

    // Per-window event ring (ev_read == ev_write → empty)
    pt::uint64_t events[EVENT_CAP];
    pt::uint32_t ev_read, ev_write;

    // Text cursor for stdout rendering inside the client area (in character units)
    pt::uint32_t text_col, text_row;
    bool         wrap_pending;      // deferred wrap: wrote to last column, wrap on next printable

    // ANSI parser state
    AnsiParser   ansi;
    pt::uint32_t fg, bg;                // current text colors (default 0xFFFFFF, 0x000000)
    pt::uint32_t saved_col, saved_row;  // for \x1b[s / \x1b[u

    char title[32];   // window title (null-terminated, set via SYS_SET_WINDOW_TITLE)
};

class WindowManager {
public:
    static void       initialize();
    // x, y, w, h are client-area coords.  For normal windows, border + titlebar
    // are added around them.  Pass WF_CHROMELESS in flags to skip chrome entirely
    // (client area = the exact rect specified).
    static pt::uint32_t create_window(pt::uint32_t x, pt::uint32_t y,
                                      pt::uint32_t w, pt::uint32_t h,
                                      pt::uint32_t owner_task_id,
                                      pt::uint32_t flags = 0);
    static void       destroy_window(pt::uint32_t wid);

    // ── Compositor: blit all visible windows to back buffer ──
    static void composite(Framebuffer* fb);

    // ── Per-window drawing (writes to pixel_buf, not framebuffer) ──
    static void win_fill_rect(pt::uint32_t wid, pt::uint32_t x, pt::uint32_t y,
                              pt::uint32_t w, pt::uint32_t h, pt::uint32_t color);
    static void win_draw_pixels(pt::uint32_t wid, const pt::uint8_t* data,
                                pt::uint32_t x, pt::uint32_t y,
                                pt::uint32_t w, pt::uint32_t h);
    static void win_draw_text(pt::uint32_t wid, pt::uint32_t x, pt::uint32_t y,
                              const char* str, pt::uint32_t fg, pt::uint32_t bg);
    static void win_put_glyph(pt::uint32_t wid, char c,
                              pt::uint32_t px, pt::uint32_t py,
                              pt::uint32_t fg, pt::uint32_t bg);
    static void win_scroll_up(pt::uint32_t wid, pt::uint32_t pixels);

    static void        push_key_event(pt::uint64_t ev);  // routes to focused window
    static pt::uint64_t poll_event(pt::uint32_t wid);    // returns 0 if empty
    static Window*     get_window(pt::uint32_t wid);
    static pt::uint32_t get_task_window(pt::uint32_t task_id);

    // Switch focus to wid (dims old focused window, highlights new one).
    static void        set_focus(pt::uint32_t wid);

    // Repaint the chrome of every normal (non-chromeless) window.
    // Called after any chromeless window draws so that chrome always sits on top.
    static void        redraw_all_chrome();

    // Returns the wid of the window whose outer frame contains (px, py),
    // or INVALID_WID if none.
    static pt::uint32_t window_at(pt::int16_t px, pt::int16_t py);

    // Render a character into the window's client area, advancing the text cursor.
    // Handles \n, \r, \t and wrapping/scrolling within the client area.
    static void        put_char(pt::uint32_t wid, char c);

    static pt::uint32_t focused_id;
    static pt::uint32_t focused_per_vt[VTERM_COUNT];  // per-VT focus tracking
    static bool        is_focused(pt::uint32_t wid) { return focused_id == wid; }
    static bool        is_on_active_vt(pt::uint32_t wid);
    static void        on_vt_switch();  // called by vterm_switch()
    static void        draw_chrome(pt::uint32_t wid, bool active);
    static void        move_window(pt::uint32_t wid, pt::int32_t new_x, pt::int32_t new_y);
    static bool        hit_title_bar(pt::uint32_t wid, pt::int16_t px, pt::int16_t py);
    static void        raise_window(pt::uint32_t wid);

    // Fill buf with info about active non-chromeless windows. Returns count written.
    struct WinListEntry {
        pt::uint8_t wid;
        pt::uint8_t flags;   // bit 0 = focused
        char title[30];
    };
    static pt::uint32_t list_windows(WinListEntry* buf, pt::uint32_t max_entries);

private:
    static Window windows[MAX_WINDOWS];
    static pt::uint32_t z_order[MAX_WINDOWS];  // wids, back-to-front (0=bottom)
    static pt::uint32_t z_count;

    static void z_remove(pt::uint32_t wid);
    static void z_insert_top(pt::uint32_t wid);

    static constexpr pt::uint32_t COLOR_BORDER    = 0x404040;
    static constexpr pt::uint32_t COLOR_TITLE_ACT = 0x0055AA;
    static constexpr pt::uint32_t COLOR_TITLE_INF = 0x303030;
};

// Free function called from keyboard.cpp (avoids circular include issues)
void wm_route_key_event(pt::uint64_t encoded_event);
