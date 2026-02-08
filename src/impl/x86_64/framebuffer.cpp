#include "framebuffer.h"

Framebuffer buffer;

Framebuffer* Framebuffer::get_instance() {
    return &buffer;
}

void Framebuffer::Init(const boot_framebuffer *fb) {
    buffer = Framebuffer{fb};
}


void Framebuffer::Draw(const pt::uint8_t* what,
                       const pt::uint32_t x_pos,
                       const pt::uint32_t y_pos,
                       const pt::uint32_t width,
                       const pt::uint32_t height) const {
    for (pt::uint32_t y = y_pos; y < y_pos + height; y++)
    {
        for (pt::uint32_t x = x_pos; x < x_pos + width ; x++)
        {
            const pt::uint32_t src_x = x - x_pos;
            const pt::uint32_t src_y = y - y_pos;
            const pt::uint32_t pos = src_y * 3 * width + src_x * 3;
            const pt::uint32_t c =  what[pos] << 16
                        | what[pos + 1] << 8
                        | what[pos + 2];
            this->PutPixel(x, y, c);
        }
    }
}

void Framebuffer::PutPixel(
    const pt::uint32_t x,
    const pt::uint32_t y,
    const pt::uint32_t color) const {
        const pt::uintptr_t fb_addr   = this->m_addr;
        const pt::uint32_t  fb_stride = this->m_stride;
        const pt::uint8_t   fb_bytes  = this->m_bpp / 8;
        *reinterpret_cast<pt::uint32_t *>(fb_addr + x * fb_bytes + y * fb_stride) = color;
}

pt::uint32_t Framebuffer::GetPixel(
    const pt::uint32_t x,
    const pt::uint32_t y) const {
        const pt::uintptr_t fb_addr   = this->m_addr;
        const pt::uint32_t  fb_stride = this->m_stride;
        const pt::uint8_t   fb_bytes  = this->m_bpp / 8;
        return *reinterpret_cast<pt::uint32_t *>(fb_addr + x * fb_bytes + y * fb_stride);
}

constexpr pt::uint16_t cursor_size = 256;
constexpr pt::uint8_t cursor_width = 16;
constexpr pt::uint8_t normal_cursor_mask[cursor_size] = {
    1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,
    1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,
    1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,
    1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,
    1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,
    1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,
    1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,
    1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,
    1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,
    1,1,1,1,0,1,1,1,1,1,1,0,0,0,0,0,
    1,1,1,0,0,0,1,1,1,1,1,1,0,0,0,0,
    1,1,0,0,0,0,0,1,1,1,1,1,1,0,0,0,
    1,0,0,0,0,0,0,0,1,1,1,1,1,1,0,0,
    0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,0,
    0,0,0,0,0,0,0,0,0,0,1,1,1,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0
};
static bool captured = false;
static pt::uint64_t prevPixel[cursor_size] = {};
void Framebuffer::DrawCursor(const pt::uint32_t x_pos, const pt::uint32_t y_pos) const {
    if (!captured) captured = true;

    for (pt::uint8_t i = 0; i < cursor_width; i++) {
        for (pt::uint8_t j = 0; j < cursor_width; j++) {
            if (y_pos + i >= this->m_height) { continue; }
            if (x_pos + j >= this->m_width) { continue; }
            const int pos = i*cursor_width+j;
            prevPixel[pos] = this->GetPixel(x_pos+j, y_pos+i);
            if (normal_cursor_mask[pos] != 0) {
                this->PutPixel(x_pos+j, y_pos+i, 0xffffff);
            }
        }
    }
}

void Framebuffer::EraseCursor(const pt::uint32_t x_pos, const pt::uint32_t y_pos) const {
    if (!captured) return;
    for (pt::uint8_t i = 0; i < cursor_width; i++) {
        for (pt::uint8_t j = 0; j < cursor_width; j++) {
            const int pos = i*cursor_width+j;
            this->PutPixel((x_pos+j), (y_pos+i), prevPixel[pos]);
        }
    }
}

void Framebuffer::Clear(const pt::uint8_t r, const pt::uint8_t g, const pt::uint8_t b) const {
    const pt::uint32_t  fb_width  = this->m_width;
    const pt::uint32_t  fb_height  = this->m_height;
    const pt::uint32_t c =  r << 16
                | g << 8
                | b;

    for (pt::uint32_t y = 0; y < fb_height; y++) {
        for (pt::uint32_t x = 0; x < fb_width ; x++) {
            this->PutPixel(x, y, c);
        }
    }
}
