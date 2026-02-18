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

    m_cur_col = 0;
    m_cur_row = 0;
    m_ready   = true;
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

void FbTerm::put_char(char c)
{
    if (!m_ready) return;

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

void fbterm_putchar(char c)
{
    fbterm.put_char(c);
}
