#pragma once
#include "defs.h"
#include "framebuffer.h"

struct PSF1Header {
    pt::uint8_t  magic[2];   // 0x36, 0x04
    pt::uint8_t  mode;       // bit0=512 glyphs, bit1=unicode table
    pt::uint8_t  glyph_height;
};

constexpr pt::uint8_t PSF1_MAGIC0 = 0x36;
constexpr pt::uint8_t PSF1_MAGIC1 = 0x04;
constexpr pt::uint32_t PSF1_GLYPH_WIDTH = 8;
constexpr pt::uint32_t PSF1_HEADER_SIZE = 4;

// Left-side framebuffer terminal for klog output.
// Uses a PSF1 font; renders a fixed-width column on the left.
class FbTerm {
public:
    // Initialize with loaded PSF1 font data and the framebuffer to draw into.
    // Returns false if font data is invalid.
    bool init(const pt::uint8_t* psf_data, pt::size_t psf_size, Framebuffer* fb);

    void put_char(char c);
    void print(const char* str);

    // fg/bg colors as packed 0xRRGGBB
    void set_colors(pt::uint32_t fg, pt::uint32_t bg) { m_fg = fg; m_bg = bg; }

    bool is_ready() const { return m_ready; }

private:
    void draw_glyph(char c, pt::uint32_t px, pt::uint32_t py);
    void newline();
    void scroll();

    Framebuffer*       m_fb        = nullptr;
    const pt::uint8_t* m_glyphs    = nullptr;  // pointer into psf_data past header
    pt::uint32_t       m_glyph_h   = 0;
    pt::uint32_t       m_cols      = 0;   // terminal width in chars
    pt::uint32_t       m_rows      = 0;   // terminal height in chars
    pt::uint32_t       m_cur_col   = 0;
    pt::uint32_t       m_cur_row   = 0;
    pt::uint32_t       m_term_w    = 0;   // pixel width of the terminal column
    pt::uint32_t       m_fg        = 0x00FF00;  // green by default
    pt::uint32_t       m_bg        = 0x000000;  // black bg
    bool               m_ready     = false;
};

extern FbTerm fbterm;

// Call from klog to mirror output to the framebuffer terminal.
void fbterm_putchar(char c);
