#include "window.h"
#include "device/fbterm.h"
#include "framebuffer.h"
#include "virtual.h"
#include "vterm.h"
#include "task.h"

Window       WindowManager::windows[MAX_WINDOWS];
pt::uint32_t WindowManager::focused_id = INVALID_WID;
pt::uint32_t WindowManager::focused_per_vt[VTERM_COUNT];
pt::uint32_t WindowManager::z_order[MAX_WINDOWS];
pt::uint32_t WindowManager::z_count = 0;

void WindowManager::initialize()
{
    for (pt::uint32_t i = 0; i < MAX_WINDOWS; i++) {
        windows[i].id            = INVALID_WID;
        windows[i].owner_task_id = INVALID_WID;
        windows[i].active        = false;
        windows[i].chromeless    = false;
        windows[i].screen_x      = 0;
        windows[i].screen_y      = 0;
        windows[i].total_w       = 0;
        windows[i].total_h       = 0;
        windows[i].client_ox     = 0;
        windows[i].client_oy     = 0;
        windows[i].client_w      = 0;
        windows[i].client_h      = 0;
        windows[i].pixel_buf     = nullptr;
        windows[i].ev_read       = 0;
        windows[i].ev_write      = 0;
        windows[i].text_col         = 0;
        windows[i].text_row         = 0;
        windows[i].wrap_pending     = false;
        windows[i].fg               = 0xFFFFFF;
        windows[i].bg               = 0x000000;
        windows[i].saved_col        = 0;
        windows[i].saved_row        = 0;
        windows[i].ansi.state       = AnsiParser::NORMAL;
        windows[i].ansi.n_params    = 0;
        windows[i].ansi.private_mode = false;
        windows[i].vt_id            = INVALID_VT;
    }
    focused_id = INVALID_WID;
    for (pt::uint32_t v = 0; v < VTERM_COUNT; v++)
        focused_per_vt[v] = INVALID_WID;
    z_count = 0;
}

// ── Z-order helpers ─────────────────────────────────────────────────────

void WindowManager::z_remove(pt::uint32_t wid)
{
    for (pt::uint32_t i = 0; i < z_count; i++) {
        if (z_order[i] == wid) {
            for (pt::uint32_t j = i; j + 1 < z_count; j++)
                z_order[j] = z_order[j + 1];
            z_count--;
            return;
        }
    }
}

void WindowManager::z_insert_top(pt::uint32_t wid)
{
    if (z_count >= MAX_WINDOWS) return;
    if (windows[wid].chromeless) {
        // Insert before the first non-chromeless window
        pt::uint32_t pos = 0;
        while (pos < z_count && windows[z_order[pos]].chromeless)
            pos++;
        // Shift everything from pos up
        for (pt::uint32_t j = z_count; j > pos; j--)
            z_order[j] = z_order[j - 1];
        z_order[pos] = wid;
    } else {
        // Normal window goes to the very top
        z_order[z_count] = wid;
    }
    z_count++;
}

bool WindowManager::is_on_active_vt(pt::uint32_t wid)
{
    return g_active_vt < VTERM_COUNT &&
           wid < MAX_WINDOWS && windows[wid].active && windows[wid].vt_id == g_active_vt;
}

void WindowManager::on_vt_switch()
{
    if (g_active_vt >= VTERM_COUNT) return;
    focused_id = focused_per_vt[g_active_vt];
    // Validate that saved focus is still valid
    if (focused_id != INVALID_WID &&
        (focused_id >= MAX_WINDOWS || !windows[focused_id].active ||
         windows[focused_id].vt_id != g_active_vt))
        focused_id = INVALID_WID;
}

// ── Window lifecycle ────────────────────────────────────────────────────

pt::uint32_t WindowManager::create_window(pt::uint32_t x, pt::uint32_t y,
                                           pt::uint32_t w, pt::uint32_t h,
                                           pt::uint32_t owner_task_id,
                                           pt::uint32_t flags)
{
    // Find first inactive slot
    pt::uint32_t wid = INVALID_WID;
    for (pt::uint32_t i = 0; i < MAX_WINDOWS; i++) {
        if (!windows[i].active) {
            wid = i;
            break;
        }
    }
    if (wid == INVALID_WID) return INVALID_WID;

    Window* win        = &windows[wid];
    win->id            = wid;
    win->owner_task_id = owner_task_id;
    win->active        = true;
    win->chromeless    = (flags & WF_CHROMELESS) != 0;

    if (win->chromeless) {
        win->screen_x  = x;
        win->screen_y  = y;
        win->total_w   = w;
        win->total_h   = h;
    } else {
        win->screen_x  = x - BORDER_W;
        win->screen_y  = y - BORDER_W - TITLE_BAR_H;
        win->total_w   = BORDER_W + w + BORDER_W;
        win->total_h   = BORDER_W + TITLE_BAR_H + h + BORDER_W;
    }
    win->client_ox = x;
    win->client_oy = y;
    win->client_w  = w;
    win->client_h  = h;

    // Allocate per-window pixel buffer
    pt::size_t buf_size = (pt::size_t)w * h * sizeof(pt::uint32_t);
    win->pixel_buf = reinterpret_cast<pt::uint32_t*>(vmm.kcalloc(buf_size));

    win->ev_read            = 0;
    win->ev_write           = 0;
    win->text_col           = 0;
    win->text_row           = 0;
    win->wrap_pending       = false;
    win->fg                 = 0xFFFFFF;
    win->bg                 = 0x000000;
    win->saved_col          = 0;
    win->saved_row          = 0;
    win->ansi.state         = AnsiParser::NORMAL;
    win->ansi.n_params      = 0;
    win->ansi.private_mode  = false;
    win->title[0]           = '\0';
    win->vt_id              = g_active_vt;

    z_insert_top(wid);

    // Chromeless windows never hold keyboard focus.
    if (!win->chromeless) {
        focused_id = wid;
        if (g_active_vt < VTERM_COUNT)
            focused_per_vt[g_active_vt] = wid;
    }

    return wid;
}

void WindowManager::destroy_window(pt::uint32_t wid)
{
    if (wid >= MAX_WINDOWS) return;
    Window* win = &windows[wid];
    if (!win->active) return;

    pt::uint32_t dead_vt = win->vt_id;
    // Free pixel buffer (compositor stops drawing this window next frame)
    if (win->pixel_buf) {
        vmm.kfree(win->pixel_buf);
        win->pixel_buf = nullptr;
    }

    win->active        = false;
    win->owner_task_id = INVALID_WID;
    z_remove(wid);

    // Update per-VT focus for the dead window's VT — pick topmost in z_order
    if (dead_vt < VTERM_COUNT && focused_per_vt[dead_vt] == wid) {
        focused_per_vt[dead_vt] = INVALID_WID;
        for (pt::uint32_t zi = z_count; zi-- > 0; ) {
            pt::uint32_t i = z_order[zi];
            if (windows[i].active && !windows[i].chromeless && windows[i].vt_id == dead_vt) {
                focused_per_vt[dead_vt] = i;
                break;
            }
        }
    }

    // Update global focused_id if the destroyed window was focused
    if (focused_id == wid)
        focused_id = (dead_vt == g_active_vt) ? focused_per_vt[dead_vt] : INVALID_WID;
}

// ── Chrome drawing (writes to back buffer, called from composite) ───────

void WindowManager::draw_chrome(pt::uint32_t wid, bool active)
{
    if (wid >= MAX_WINDOWS) return;
    Window* win = &windows[wid];
    if (!win->active) return;
    if (win->chromeless) return;

    Framebuffer* fb = Framebuffer::get_instance();
    if (!fb) return;

    // Unpack border color
    pt::uint8_t br = (pt::uint8_t)(COLOR_BORDER >> 16);
    pt::uint8_t bg = (pt::uint8_t)(COLOR_BORDER >> 8);
    pt::uint8_t bb = (pt::uint8_t)(COLOR_BORDER);

    // 1-px border: top, bottom, left, right
    fb->FillRect(win->screen_x, win->screen_y,
                 win->total_w, BORDER_W, br, bg, bb);
    fb->FillRect(win->screen_x, win->screen_y + win->total_h - BORDER_W,
                 win->total_w, BORDER_W, br, bg, bb);
    fb->FillRect(win->screen_x, win->screen_y + BORDER_W,
                 BORDER_W, win->total_h - 2 * BORDER_W, br, bg, bb);
    fb->FillRect(win->screen_x + win->total_w - BORDER_W, win->screen_y + BORDER_W,
                 BORDER_W, win->total_h - 2 * BORDER_W, br, bg, bb);

    // Title bar
    pt::uint32_t tc = active ? COLOR_TITLE_ACT : COLOR_TITLE_INF;
    pt::uint8_t tr = (pt::uint8_t)(tc >> 16);
    pt::uint8_t tg = (pt::uint8_t)(tc >> 8);
    pt::uint8_t tb = (pt::uint8_t)(tc);
    fb->FillRect(win->screen_x + BORDER_W, win->screen_y + BORDER_W,
                 win->total_w - 2 * BORDER_W, TITLE_BAR_H, tr, tg, tb);

    // Render title text over the title bar
    if (win->title[0] && fbterm.is_ready()) {
        pt::uint32_t tx = win->screen_x + BORDER_W + 4;
        pt::uint32_t ty = win->screen_y + BORDER_W;
        fbterm.draw_at(tx, ty, win->title, 0xFFFFFF, tc);
    }
}

// ── Compositor: blit all visible windows to back buffer ─────────────────

void WindowManager::composite(Framebuffer* fb)
{
    if (!fb) return;
    pt::uintptr_t back = fb->get_back();
    if (!back) return;

    pt::uint32_t fb_w      = fb->get_width();
    pt::uint32_t fb_h      = fb->get_height();
    pt::uint32_t fb_stride = fb->get_stride();
    pt::uint32_t fb_bytes  = fb->get_bpp() / 8;

    // Blit windows in z-order (back to front)
    for (pt::uint32_t zi = 0; zi < z_count; zi++) {
        pt::uint32_t wid = z_order[zi];
        Window* win = &windows[wid];
        if (!win->active || !win->pixel_buf) continue;
        if (!is_on_active_vt(wid)) continue;

        // Clip to screen bounds (client_ox/oy may wrap negative via uint32)
        pt::int32_t ox = (pt::int32_t)win->client_ox;
        pt::int32_t oy = (pt::int32_t)win->client_oy;
        pt::int32_t src_x = 0, src_y = 0;
        pt::int32_t dst_x = ox, dst_y = oy;
        if (dst_x < 0) { src_x = -dst_x; dst_x = 0; }
        if (dst_y < 0) { src_y = -dst_y; dst_y = 0; }
        if (src_x >= (pt::int32_t)win->client_w ||
            src_y >= (pt::int32_t)win->client_h) continue;

        pt::uint32_t blit_w = win->client_w - (pt::uint32_t)src_x;
        pt::uint32_t blit_h = win->client_h - (pt::uint32_t)src_y;
        if ((pt::uint32_t)dst_x + blit_w > fb_w) blit_w = fb_w - (pt::uint32_t)dst_x;
        if ((pt::uint32_t)dst_y + blit_h > fb_h) blit_h = fb_h - (pt::uint32_t)dst_y;

        // Blit pixel_buf rows to back buffer
        for (pt::uint32_t y = 0; y < blit_h; y++) {
            pt::uint32_t* src = &win->pixel_buf[((pt::uint32_t)src_y + y) * win->client_w
                                                 + (pt::uint32_t)src_x];
            pt::uint32_t* dst = reinterpret_cast<pt::uint32_t*>(
                back + (pt::uint32_t)dst_x * fb_bytes
                     + ((pt::uint32_t)dst_y + y) * fb_stride);
            for (pt::uint32_t x = 0; x < blit_w; x++)
                dst[x] = src[x];
        }

        // Draw chrome for non-chromeless windows
        if (!win->chromeless)
            draw_chrome(wid, focused_id == wid);
    }
}

// ── Per-window drawing (writes to pixel_buf) ────────────────────────────

void WindowManager::win_fill_rect(pt::uint32_t wid, pt::uint32_t x, pt::uint32_t y,
                                   pt::uint32_t w, pt::uint32_t h, pt::uint32_t color)
{
    Window* win = get_window(wid);
    if (!win || !win->pixel_buf) return;
    // Clip to client area
    if (x >= win->client_w || y >= win->client_h) return;
    if (x + w > win->client_w) w = win->client_w - x;
    if (y + h > win->client_h) h = win->client_h - y;
    for (pt::uint32_t row = y; row < y + h; row++)
        for (pt::uint32_t col = x; col < x + w; col++)
            win->pixel_buf[row * win->client_w + col] = color;
}

void WindowManager::win_draw_pixels(pt::uint32_t wid, const pt::uint8_t* data,
                                     pt::uint32_t x, pt::uint32_t y,
                                     pt::uint32_t w, pt::uint32_t h)
{
    Window* win = get_window(wid);
    if (!win || !win->pixel_buf || !data) return;
    for (pt::uint32_t dy = 0; dy < h; dy++) {
        if (y + dy >= win->client_h) break;
        for (pt::uint32_t dx = 0; dx < w; dx++) {
            if (x + dx >= win->client_w) break;
            pt::uint32_t src_off = (dy * w + dx) * 3;
            pt::uint32_t color = (pt::uint32_t)data[src_off] << 16
                               | (pt::uint32_t)data[src_off + 1] << 8
                               | (pt::uint32_t)data[src_off + 2];
            win->pixel_buf[(y + dy) * win->client_w + (x + dx)] = color;
        }
    }
}

void WindowManager::win_draw_text(pt::uint32_t wid, pt::uint32_t x, pt::uint32_t y,
                                   const char* str, pt::uint32_t fg, pt::uint32_t bg)
{
    Window* win = get_window(wid);
    if (!win || !win->pixel_buf || !str) return;
    if (!fbterm.is_ready()) return;
    while (*str) {
        fbterm.render_glyph_to_buf(*str, win->pixel_buf,
                                    win->client_w, win->client_h,
                                    x, y, fg, bg);
        x += fbterm.glyph_w();
        str++;
    }
}

void WindowManager::win_put_glyph(pt::uint32_t wid, char c,
                                   pt::uint32_t px, pt::uint32_t py,
                                   pt::uint32_t fg, pt::uint32_t bg)
{
    Window* win = get_window(wid);
    if (!win || !win->pixel_buf) return;
    if (!fbterm.is_ready()) return;
    fbterm.render_glyph_to_buf(c, win->pixel_buf,
                                win->client_w, win->client_h,
                                px, py, fg, bg);
}

void WindowManager::win_scroll_up(pt::uint32_t wid, pt::uint32_t pixels)
{
    Window* win = get_window(wid);
    if (!win || !win->pixel_buf || pixels == 0) return;

    pt::uint32_t w = win->client_w;
    pt::uint32_t h = win->client_h;

    // Shift rows up
    for (pt::uint32_t y = pixels; y < h; y++) {
        pt::uint32_t* src = &win->pixel_buf[y * w];
        pt::uint32_t* dst = &win->pixel_buf[(y - pixels) * w];
        for (pt::uint32_t x = 0; x < w; x++)
            dst[x] = src[x];
    }
    // Clear vacated bottom rows
    pt::uint32_t clear_start = (h > pixels) ? (h - pixels) : 0;
    for (pt::uint32_t y = clear_start; y < h; y++)
        for (pt::uint32_t x = 0; x < w; x++)
            win->pixel_buf[y * w + x] = 0x000000;
}

// ── Event handling ──────────────────────────────────────────────────────

void WindowManager::push_key_event(pt::uint64_t ev)
{
    if (focused_id == INVALID_WID) return;
    Window* win = &windows[focused_id];
    if (!win->active) return;
    // Drop if ring is full
    if (win->ev_write - win->ev_read >= EVENT_CAP) return;
    win->events[win->ev_write % EVENT_CAP] = ev;
    win->ev_write++;
}

pt::uint64_t WindowManager::poll_event(pt::uint32_t wid)
{
    if (wid >= MAX_WINDOWS) return 0;
    Window* win = &windows[wid];
    if (!win->active) return 0;
    if (win->ev_read == win->ev_write) return 0;
    pt::uint64_t ev = win->events[win->ev_read % EVENT_CAP];
    win->ev_read++;
    return ev;
}

Window* WindowManager::get_window(pt::uint32_t wid)
{
    if (wid >= MAX_WINDOWS) return nullptr;
    if (!windows[wid].active) return nullptr;
    return &windows[wid];
}

pt::uint32_t WindowManager::get_task_window(pt::uint32_t task_id)
{
    for (pt::uint32_t i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i].active && windows[i].owner_task_id == task_id)
            return windows[i].id;
    }
    return INVALID_WID;
}

// ── ANSI CSI handling (renders to pixel_buf) ────────────────────────────

static void handle_csi(Window* win, char cmd,
                       const pt::uint32_t* p, pt::uint32_t np,
                       pt::uint32_t cols, pt::uint32_t rows,
                       pt::uint32_t gw, pt::uint32_t gh)
{
    auto P = [&](pt::uint32_t i, pt::uint32_t def) -> pt::uint32_t {
        return (i < np && p[i] != 0) ? p[i] : def;
    };

    // Any CSI command cancels deferred wrap state.
    win->wrap_pending = false;

    switch (cmd) {
    case 'A': {  // cursor up
        pt::uint32_t n = P(0, 1);
        win->text_row = (win->text_row >= n) ? win->text_row - n : 0;
        break;
    }
    case 'B': {  // cursor down
        pt::uint32_t n = P(0, 1);
        win->text_row += n;
        if (win->text_row >= rows) win->text_row = rows - 1;
        break;
    }
    case 'C': {  // cursor forward
        pt::uint32_t n = P(0, 1);
        win->text_col += n;
        if (win->text_col >= cols) win->text_col = cols - 1;
        break;
    }
    case 'D': {  // cursor back
        pt::uint32_t n = P(0, 1);
        win->text_col = (win->text_col >= n) ? win->text_col - n : 0;
        break;
    }
    case 'H':
    case 'f': {  // cursor position (1-based)
        pt::uint32_t r = P(0, 1) - 1;
        pt::uint32_t c = P(1, 1) - 1;
        win->text_row = (r < rows) ? r : rows - 1;
        win->text_col = (c < cols) ? c : cols - 1;
        break;
    }
    case 'J': {  // erase display
        pt::uint32_t mode = p[0];
        if (mode == 2) {
            WindowManager::win_fill_rect(win->id, 0, 0,
                                          win->client_w, win->client_h, win->bg);
            win->text_col = win->text_row = 0;
        } else if (mode == 0) {
            pt::uint32_t py = win->text_row * gh;
            pt::uint32_t px = win->text_col * gw;
            WindowManager::win_fill_rect(win->id, px, py,
                                          win->client_w - px, gh, win->bg);
            if (win->text_row + 1 < rows)
                WindowManager::win_fill_rect(win->id, 0, py + gh,
                                              win->client_w,
                                              win->client_h - (win->text_row + 1) * gh,
                                              win->bg);
        }
        break;
    }
    case 'K': {  // erase line
        pt::uint32_t mode = p[0];
        pt::uint32_t py = win->text_row * gh;
        if (mode == 0) {  // to end of line
            pt::uint32_t px = win->text_col * gw;
            WindowManager::win_fill_rect(win->id, px, py,
                                          win->client_w - px, gh, win->bg);
        } else if (mode == 2) {  // whole line
            WindowManager::win_fill_rect(win->id, 0, py, win->client_w, gh, win->bg);
        }
        if (mode == 2) win->text_col = 0;
        break;
    }
    case 's':
        win->saved_col = win->text_col;
        win->saved_row = win->text_row;
        break;
    case 'u':
        win->text_col = win->saved_col;
        win->text_row = win->saved_row;
        break;
    case 'm': {  // SGR — may have multiple params
        for (pt::uint32_t i = 0; i < np; i++) {
            pt::uint32_t v = p[i];
            if (v == 0)                   { win->fg = 0xFFFFFF; win->bg = 0x000000; }
            else if (v >= 30 && v <= 37)  win->fg = ansi_color(v - 30);
            else if (v >= 40 && v <= 47)  win->bg = ansi_color(v - 40);
            else if (v >= 90 && v <= 97)  win->fg = ansi_color(v - 90 + 8);
            else if (v >= 100 && v <= 107) win->bg = ansi_color(v - 100 + 8);
        }
        break;
    }
    }
}

void WindowManager::put_char(pt::uint32_t wid, char c)
{
    Window* win = get_window(wid);
    if (!win) return;

    const pt::uint32_t gw   = fbterm.glyph_w();
    const pt::uint32_t gh   = fbterm.glyph_h();
    if (gw == 0 || gh == 0) return;

    const pt::uint32_t cols = win->client_w / gw;
    const pt::uint32_t rows = win->client_h / gh;
    if (cols == 0 || rows == 0) return;

    // ANSI escape sequence handling
    char final_byte = 0;
    bool complete   = win->ansi.feed(c, final_byte);

    if (complete) {
        if (!win->ansi.private_mode)
            handle_csi(win, final_byte,
                       win->ansi.params, win->ansi.n_params,
                       cols, rows, gw, gh);
        win->ansi.private_mode = false;
        return;
    }
    // Char was consumed by parser if state is not (and was not) NORMAL
    if (win->ansi.state != AnsiParser::NORMAL) return;

    // Normal character handling
    if (c == '\f') {
        // Form feed: clear client area and home the cursor.
        win_fill_rect(wid, 0, 0, win->client_w, win->client_h, 0x000000);
        win->text_col = 0;
        win->text_row = 0;
        win->wrap_pending = false;
        return;
    }
    if (c == '\r') {
        win->text_col = 0;
        win->wrap_pending = false;
        return;
    }
    if (c == '\b') {
        if (win->text_col > 0)
            win->text_col--;
        win->wrap_pending = false;
        return;
    }
    // Deferred wrap: if the previous character landed on the last column,
    // wrap now before processing \n, \t, or the next printable character.
    if (win->wrap_pending) {
        win->wrap_pending = false;
        win->text_col = 0;
        win->text_row++;
    }
    if (c == '\n') {
        win->text_col = 0;
        win->text_row++;
    } else if (c == '\t') {
        win->text_col = (win->text_col + 4) & ~3u;
        if (win->text_col >= cols) {
            win->text_col = 0;
            win->text_row++;
        }
    } else {
        // Render glyph into pixel_buf
        pt::uint32_t px = win->text_col * gw;
        pt::uint32_t py = win->text_row * gh;
        win_put_glyph(wid, c, px, py, win->fg, win->bg);
        win->text_col++;
        if (win->text_col >= cols) {
            // Don't wrap yet — defer until next character arrives.
            win->text_col = cols - 1;
            win->wrap_pending = true;
        }
    }

    // Scroll if we've passed the last row
    if (win->text_row >= rows) {
        win_scroll_up(wid, gh);
        win->text_row = rows - 1;
    }
}

// ── Focus & raise (compositor handles visual updates) ───────────────────

void WindowManager::raise_window(pt::uint32_t wid)
{
    if (wid >= MAX_WINDOWS || !windows[wid].active) return;
    if (windows[wid].chromeless) return;
    if (!is_on_active_vt(wid)) return;
    // Already at top?
    if (z_count > 0 && z_order[z_count - 1] == wid) return;

    z_remove(wid);
    z_insert_top(wid);
}

void WindowManager::set_focus(pt::uint32_t wid)
{
    // INVALID_WID means "unfocus all" — click on the desktop.
    if (wid == INVALID_WID) {
        focused_id = INVALID_WID;
        if (g_active_vt < VTERM_COUNT)
            focused_per_vt[g_active_vt] = INVALID_WID;
        return;
    }
    if (wid >= MAX_WINDOWS || !windows[wid].active) return;
    if (windows[wid].chromeless) return;  // background widgets are not focusable
    if (!is_on_active_vt(wid)) return;    // reject windows on other VTs

    // Always raise on click, even if already focused
    raise_window(wid);

    if (focused_id == wid) return;

    focused_id = wid;
    if (g_active_vt < VTERM_COUNT)
        focused_per_vt[g_active_vt] = wid;
}

pt::uint32_t WindowManager::window_at(pt::int16_t px, pt::int16_t py)
{
    if (px < 0 || py < 0) return INVALID_WID;
    pt::uint32_t ux = (pt::uint32_t)px;
    pt::uint32_t uy = (pt::uint32_t)py;
    // Iterate front-to-back (topmost window first)
    for (pt::uint32_t zi = z_count; zi-- > 0; ) {
        pt::uint32_t i = z_order[zi];
        Window* w = &windows[i];
        if (!w->active) continue;
        if (!is_on_active_vt(i)) continue;
        if (ux >= w->screen_x && ux < w->screen_x + w->total_w &&
            uy >= w->screen_y && uy < w->screen_y + w->total_h)
            return i;
    }
    return INVALID_WID;
}

void WindowManager::redraw_all_chrome()
{
    // No-op: compositor handles chrome drawing during composite().
}

bool WindowManager::hit_title_bar(pt::uint32_t wid, pt::int16_t px, pt::int16_t py)
{
    if (wid >= MAX_WINDOWS) return false;
    Window* w = &windows[wid];
    if (!w->active || w->chromeless) return false;
    pt::uint32_t ux = (pt::uint32_t)px;
    pt::uint32_t uy = (pt::uint32_t)py;
    return ux >= w->screen_x + BORDER_W &&
           ux <  w->screen_x + w->total_w - BORDER_W &&
           uy >= w->screen_y + BORDER_W &&
           uy <  w->screen_y + BORDER_W + TITLE_BAR_H;
}

void WindowManager::move_window(pt::uint32_t wid, pt::int32_t new_x, pt::int32_t new_y)
{
    if (wid >= MAX_WINDOWS) return;
    Window* win = &windows[wid];
    if (!win->active || win->chromeless) return;
    if (!is_on_active_vt(wid)) return;

    Framebuffer* fb = Framebuffer::get_instance();
    if (!fb) return;

    // Clamp so the window stays on-screen (at least title bar visible)
    pt::int32_t min_x = -(pt::int32_t)(win->total_w - BORDER_W - 32);
    pt::int32_t min_y = 0;
    pt::int32_t max_x = (pt::int32_t)fb->get_width() - 32;
    pt::int32_t max_y = (pt::int32_t)fb->get_height() - BORDER_W - TITLE_BAR_H;
    if (new_x < min_x) new_x = min_x;
    if (new_x > max_x) new_x = max_x;
    if (new_y < min_y) new_y = min_y;
    if (new_y > max_y) new_y = max_y;

    // Compositor repaints from wallpaper+VTerm next frame — no restore needed.

    // Update position (screen_x/y is the outer frame origin)
    win->screen_x = (pt::uint32_t)new_x;
    win->screen_y = (pt::uint32_t)new_y;
    win->client_ox = win->screen_x + BORDER_W;
    win->client_oy = win->screen_y + BORDER_W + TITLE_BAR_H;
    // pixel_buf content is preserved — no text cursor reset needed
}

pt::uint32_t WindowManager::list_windows(WinListEntry* buf, pt::uint32_t max_entries)
{
    pt::uint32_t count = 0;
    for (pt::uint32_t i = 0; i < MAX_WINDOWS && count < max_entries; i++) {
        if (!windows[i].active) continue;
        if (windows[i].chromeless) continue;
        buf[count].wid = (pt::uint8_t)i;
        buf[count].flags = (focused_id == i) ? 1 : 0;
        // Copy title
        pt::uint32_t j = 0;
        while (j < 29 && windows[i].title[j]) {
            buf[count].title[j] = windows[i].title[j];
            j++;
        }
        buf[count].title[j] = '\0';
        // Copy task name from owning task
        buf[count].task_name[0] = '\0';
        {
            const char* tname = TaskScheduler::get_task_name(windows[i].owner_task_id);
            if (tname) {
                pt::uint32_t k = 0;
                while (k < 15 && tname[k]) {
                    buf[count].task_name[k] = tname[k];
                    k++;
                }
                buf[count].task_name[k] = '\0';
            }
        }
        count++;
    }
    return count;
}

void wm_route_key_event(pt::uint64_t encoded_event)
{
    WindowManager::push_key_event(encoded_event);
}
