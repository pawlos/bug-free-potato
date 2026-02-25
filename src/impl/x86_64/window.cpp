#include "window.h"
#include "fbterm.h"
#include "framebuffer.h"

Window       WindowManager::windows[MAX_WINDOWS];
pt::uint32_t WindowManager::focused_id = INVALID_WID;

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
        windows[i].ev_read       = 0;
        windows[i].ev_write      = 0;
        windows[i].text_col      = 0;
        windows[i].text_row      = 0;
    }
    focused_id = INVALID_WID;
}

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
        // No border or title bar: the specified rect IS the client area.
        win->screen_x  = x;
        win->screen_y  = y;
        win->total_w   = w;
        win->total_h   = h;
    } else {
        // x, y are the client area screen-absolute origin; chrome surrounds it.
        win->screen_x  = x - BORDER_W;
        win->screen_y  = y - BORDER_W - TITLE_BAR_H;
        win->total_w   = BORDER_W + w + BORDER_W;
        win->total_h   = BORDER_W + TITLE_BAR_H + h + BORDER_W;
    }
    win->client_ox = x;
    win->client_oy = y;
    win->client_w  = w;
    win->client_h  = h;
    win->ev_read   = 0;
    win->ev_write  = 0;
    win->text_col  = 0;
    win->text_row  = 0;

    // Chromeless windows (background widgets) never hold keyboard focus.
    if (!win->chromeless) {
        if (focused_id != INVALID_WID)
            draw_chrome(focused_id, false);
        focused_id = wid;
        draw_chrome(wid, true);
    }

    return wid;
}

void WindowManager::destroy_window(pt::uint32_t wid)
{
    if (wid >= MAX_WINDOWS) return;
    Window* win = &windows[wid];
    if (!win->active) return;

    // Erase chrome + client area from screen
    Framebuffer* fb = Framebuffer::get_instance();
    if (fb)
        fb->FillRect(win->screen_x, win->screen_y,
                     win->total_w, win->total_h, 0, 0, 0);

    win->active        = false;
    win->owner_task_id = INVALID_WID;

    if (focused_id == wid) {
        focused_id = INVALID_WID;
        // Scan from highest index downward for the last active window
        for (pt::uint32_t i = MAX_WINDOWS; i-- > 0; ) {
            if (windows[i].active) {
                focused_id = i;
                draw_chrome(i, true);
                break;
            }
        }
    }
}

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
}

bool WindowManager::translate_rect(pt::uint32_t wid,
                                    pt::uint32_t rx, pt::uint32_t ry,
                                    pt::uint32_t rw, pt::uint32_t rh,
                                    pt::uint32_t& sx, pt::uint32_t& sy,
                                    pt::uint32_t& sw, pt::uint32_t& sh)
{
    Window* win = get_window(wid);
    if (!win) return false;
    if (rx >= win->client_w || ry >= win->client_h) return false;

    sx = win->client_ox + rx;
    sy = win->client_oy + ry;
    sw = (rw > win->client_w - rx) ? (win->client_w - rx) : rw;
    sh = (rh > win->client_h - ry) ? (win->client_h - ry) : rh;

    return sw > 0 && sh > 0;
}

bool WindowManager::translate_point(pt::uint32_t wid,
                                     pt::uint32_t rx, pt::uint32_t ry,
                                     pt::uint32_t& sx, pt::uint32_t& sy)
{
    Window* win = get_window(wid);
    if (!win) return false;
    if (rx >= win->client_w || ry >= win->client_h) return false;

    sx = win->client_ox + rx;
    sy = win->client_oy + ry;
    return true;
}

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

    if (c == '\f') {
        // Form feed: clear client area and home the cursor.
        Framebuffer* fb = Framebuffer::get_instance();
        if (fb) fb->FillRect(win->client_ox, win->client_oy,
                             win->client_w,  win->client_h, 0, 0, 0);
        win->text_col = 0;
        win->text_row = 0;
        return;
    }
    if (c == '\r') {
        win->text_col = 0;
        return;
    }
    if (c == '\b') {
        if (win->text_col > 0)
            win->text_col--;
        return;
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
        // Render glyph at current cursor
        pt::uint32_t px = win->client_ox + win->text_col * gw;
        pt::uint32_t py = win->client_oy + win->text_row * gh;
        fbterm.put_char_at(c, px, py, 0xFFFFFF, 0x000000);
        win->text_col++;
        if (win->text_col >= cols) {
            win->text_col = 0;
            win->text_row++;
        }
    }

    // Scroll if we've passed the last row
    if (win->text_row >= rows) {
        fbterm.scroll_region(win->client_ox, win->client_oy,
                             win->client_w, win->client_h, gh);
        win->text_row = rows - 1;
    }
}

void WindowManager::set_focus(pt::uint32_t wid)
{
    if (wid >= MAX_WINDOWS || !windows[wid].active) return;
    if (focused_id == wid) return;

    if (focused_id != INVALID_WID)
        draw_chrome(focused_id, false);

    focused_id = wid;
    draw_chrome(wid, true);
}

pt::uint32_t WindowManager::window_at(pt::int16_t px, pt::int16_t py)
{
    if (px < 0 || py < 0) return INVALID_WID;
    pt::uint32_t ux = (pt::uint32_t)px;
    pt::uint32_t uy = (pt::uint32_t)py;
    for (pt::uint32_t i = 0; i < MAX_WINDOWS; i++) {
        Window* w = &windows[i];
        if (!w->active) continue;
        if (ux >= w->screen_x && ux < w->screen_x + w->total_w &&
            uy >= w->screen_y && uy < w->screen_y + w->total_h)
            return i;
    }
    return INVALID_WID;
}

void WindowManager::redraw_all_chrome()
{
    for (pt::uint32_t i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i].active && !windows[i].chromeless)
            draw_chrome(i, focused_id == i);
    }
}

void wm_route_key_event(pt::uint64_t encoded_event)
{
    WindowManager::push_key_event(encoded_event);
}
