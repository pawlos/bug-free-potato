#include "fbterm.h"
#include "kernel.h"

FbTerm fbterm;

bool FbTerm::init(const pt::uint8_t* psf_data, pt::size_t psf_size, Framebuffer* fb)
{
    if (!psf_data || psf_size < PSF1_HEADER_SIZE) return false;

    const PSF1Header* hdr = reinterpret_cast<const PSF1Header*>(psf_data);
    if (hdr->magic[0] != PSF1_MAGIC0 || hdr->magic[1] != PSF1_MAGIC1) return false;

    m_fb      = fb;
    m_glyph_h = hdr->glyph_height;
    m_glyphs  = psf_data + PSF1_HEADER_SIZE;

    // The terminal occupies the left half of the screen
    m_term_w  = fb->get_width() / 2;

    m_cols    = m_term_w / PSF1_GLYPH_WIDTH;
    m_rows    = fb->get_height() / m_glyph_h;

    m_cur_col         = 0;
    m_cur_row         = 0;
    m_ready           = true;
    m_ansi.state      = AnsiParser::NORMAL;
    m_ansi.n_params   = 0;
    m_saved_col       = 0;
    m_saved_row       = 0;
    return true;
}

void FbTerm::draw_glyph(char c, pt::uint32_t px, pt::uint32_t py)
{
    pt::uint8_t ch = static_cast<pt::uint8_t>(c);
    if (ch > 127) ch = '?';

    const pt::uint8_t* glyph = m_glyphs + ch * m_glyph_h;

    for (pt::uint32_t row = 0; row < m_glyph_h; row++) {
        pt::uint8_t bits = glyph[row];
        for (pt::uint32_t col = 0; col < PSF1_GLYPH_WIDTH; col++) {
            pt::uint32_t color = (bits & (0x80 >> col)) ? m_fg : m_bg;
            m_fb->FillRect(px + col, py + row, 1, 1,
                           static_cast<pt::uint8_t>((color >> 16) & 0xFF),
                           static_cast<pt::uint8_t>((color >>  8) & 0xFF),
                           static_cast<pt::uint8_t>( color        & 0xFF));
        }
    }
}

void FbTerm::newline()
{
    m_cur_col = 0;
    m_cur_row++;
    if (m_cur_row >= m_rows)
        scroll();
}

void FbTerm::scroll()
{
    // Scroll the terminal region up by one glyph row, clearing the bottom line.
    m_fb->ScrollRegionUp(0, 0, m_term_w, m_fb->get_height(), m_glyph_h);
    // Cursor stays on the last row (now blank after scroll)
    m_cur_row = m_rows - 1;
}

static void handle_fbterm_csi(FbTerm* t, char cmd,
                              const pt::uint32_t* p, pt::uint32_t np,
                              pt::uint32_t cols, pt::uint32_t rows,
                              pt::uint32_t gw, pt::uint32_t gh,
                              pt::uint32_t term_w, pt::uint32_t screen_h,
                              pt::uint32_t& cur_col, pt::uint32_t& cur_row,
                              pt::uint32_t& saved_col, pt::uint32_t& saved_row,
                              pt::uint32_t& fg, pt::uint32_t& bg,
                              Framebuffer* fb)
{
    auto P = [&](pt::uint32_t i, pt::uint32_t def) -> pt::uint32_t {
        return (i < np && p[i] != 0) ? p[i] : def;
    };

    switch (cmd) {
    case 'A': {
        pt::uint32_t n = P(0, 1);
        cur_row = (cur_row >= n) ? cur_row - n : 0;
        break;
    }
    case 'B': {
        pt::uint32_t n = P(0, 1);
        cur_row += n;
        if (cur_row >= rows) cur_row = rows - 1;
        break;
    }
    case 'C': {
        pt::uint32_t n = P(0, 1);
        cur_col += n;
        if (cur_col >= cols) cur_col = cols - 1;
        break;
    }
    case 'D': {
        pt::uint32_t n = P(0, 1);
        cur_col = (cur_col >= n) ? cur_col - n : 0;
        break;
    }
    case 'H':
    case 'f': {
        pt::uint32_t r = P(0, 1) - 1;
        pt::uint32_t c = P(1, 1) - 1;
        cur_row = (r < rows) ? r : rows - 1;
        cur_col = (c < cols) ? c : cols - 1;
        break;
    }
    case 'J': {
        if (fb) {
            pt::uint32_t mode = p[0];
            pt::uint8_t bgr = (pt::uint8_t)((bg >> 16) & 0xFF);
            pt::uint8_t bgg = (pt::uint8_t)((bg >>  8) & 0xFF);
            pt::uint8_t bgb = (pt::uint8_t)( bg        & 0xFF);
            if (mode == 2) {
                fb->FillRect(0, 0, term_w, screen_h, bgr, bgg, bgb);
                cur_col = cur_row = 0;
            } else if (mode == 0) {
                pt::uint32_t py = cur_row * gh;
                pt::uint32_t px = cur_col * gw;
                fb->FillRect(px, py, term_w - cur_col * gw, gh, bgr, bgg, bgb);
                if (cur_row + 1 < rows)
                    fb->FillRect(0, py + gh, term_w,
                                 screen_h - (cur_row + 1) * gh, bgr, bgg, bgb);
            }
        }
        break;
    }
    case 'K': {
        if (fb) {
            pt::uint32_t mode = p[0];
            pt::uint32_t py = cur_row * gh;
            pt::uint8_t bgr = (pt::uint8_t)((bg >> 16) & 0xFF);
            pt::uint8_t bgg = (pt::uint8_t)((bg >>  8) & 0xFF);
            pt::uint8_t bgb = (pt::uint8_t)( bg        & 0xFF);
            if (mode == 0) {
                fb->FillRect(cur_col * gw, py, term_w - cur_col * gw, gh,
                             bgr, bgg, bgb);
            } else if (mode == 2) {
                fb->FillRect(0, py, term_w, gh, bgr, bgg, bgb);
                cur_col = 0;
            }
        }
        break;
    }
    case 's':
        saved_col = cur_col;
        saved_row = cur_row;
        break;
    case 'u':
        cur_col = saved_col;
        cur_row = saved_row;
        break;
    case 'm': {
        for (pt::uint32_t i = 0; i < np; i++) {
            pt::uint32_t v = p[i];
            if (v == 0)                    { fg = 0x00FF00; bg = 0x000000; }
            else if (v >= 30 && v <= 37)   fg = ansi_color(v - 30);
            else if (v >= 40 && v <= 47)   bg = ansi_color(v - 40);
            else if (v >= 90 && v <= 97)   fg = ansi_color(v - 90 + 8);
            else if (v >= 100 && v <= 107) bg = ansi_color(v - 100 + 8);
        }
        break;
    }
    }
    // suppress unused param warning
    (void)t;
}

void FbTerm::put_char(char c)
{
    if (!m_ready) return;

    char final_byte = 0;
    bool complete   = m_ansi.feed(c, final_byte);

    if (complete) {
        handle_fbterm_csi(this, final_byte,
                          m_ansi.params, m_ansi.n_params,
                          m_cols, m_rows,
                          PSF1_GLYPH_WIDTH, m_glyph_h,
                          m_term_w, m_fb->get_height(),
                          m_cur_col, m_cur_row,
                          m_saved_col, m_saved_row,
                          m_fg, m_bg,
                          m_fb);
        return;
    }
    if (m_ansi.state != AnsiParser::NORMAL) return;

    if (c == '\n') {
        newline();
        return;
    }
    if (c == '\r') {
        m_cur_col = 0;
        return;
    }
    if (c == '\t') {
        // Advance to next 4-column tab stop
        m_cur_col = (m_cur_col + 4) & ~3u;
        if (m_cur_col >= m_cols) newline();
        return;
    }

    draw_glyph(c, m_cur_col * PSF1_GLYPH_WIDTH, m_cur_row * m_glyph_h);
    m_cur_col++;
    if (m_cur_col >= m_cols)
        newline();
}

void FbTerm::print(const char* str)
{
    while (*str) put_char(*str++);
}

void FbTerm::draw_at(pt::uint32_t px, pt::uint32_t py, const char* str,
                     pt::uint32_t fg, pt::uint32_t bg)
{
    if (!m_ready) return;
    pt::uint32_t saved_fg = m_fg, saved_bg = m_bg;
    m_fg = fg; m_bg = bg;
    while (*str) {
        draw_glyph(*str++, px, py);
        px += PSF1_GLYPH_WIDTH;
    }
    m_fg = saved_fg; m_bg = saved_bg;
}

void FbTerm::put_char_at(char c, pt::uint32_t px, pt::uint32_t py,
                         pt::uint32_t fg, pt::uint32_t bg)
{
    if (!m_ready) return;
    pt::uint32_t saved_fg = m_fg, saved_bg = m_bg;
    m_fg = fg; m_bg = bg;
    draw_glyph(c, px, py);
    m_fg = saved_fg; m_bg = saved_bg;
}

void FbTerm::scroll_region(pt::uint32_t x, pt::uint32_t y,
                            pt::uint32_t w, pt::uint32_t h,
                            pt::uint32_t lines)
{
    if (!m_ready) return;
    m_fb->ScrollRegionUp(x, y, w, h, lines);
}

void fbterm_putchar(char c)
{
    fbterm.put_char(c);
}
