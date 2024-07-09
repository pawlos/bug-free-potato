#include "framebuffer.h"

void Framebuffer::Draw(const pt::uint8_t* what,
                       pt::uint32_t x_pos, pt::uint32_t y_pos,
                       pt::uint32_t width, pt::uint32_t height)
{
    pt::uintptr_t fb_addr   = this->m_addr;
    pt::uint32_t  fb_width  = this->m_width;
    pt::uint32_t  fb_stride = this->m_stride;
    pt::uint8_t   fb_bytes  = this->m_bpp / 8;

    for (pt::uint32_t y = y_pos; y < y_pos + height; y++)
    {
        for (pt::uint32_t x = x_pos; x < x_pos + width ; x++)
        {
            pt::uint32_t pos = y*3*width + x*3;
            pt::uint32_t c =  (what[pos] << 16)
                        | (what[pos + 1] << 8)
                        | (what[pos + 2]);
            this->PutPixel(x, y, c);
        }
    }
}

void Framebuffer::PutPixel(
    pt::uint32_t x,
    pt::uint32_t y,
    pt::uint32_t color) {
        pt::uintptr_t fb_addr   = this->m_addr;
        pt::uint32_t  fb_stride = this->m_stride;
        pt::uint8_t   fb_bytes  = this->m_bpp / 8;
        *(pt::uint32_t* )(fb_addr + x*fb_bytes + y * fb_stride) = color;
}

pt::uint32_t Framebuffer::GetPixel(
    pt::uint32_t x,
    pt::uint32_t y) {
        pt::uintptr_t fb_addr   = this->m_addr;
        pt::uint32_t  fb_stride = this->m_stride;
        pt::uint8_t   fb_bytes  = this->m_bpp / 8;
        return *(pt::uint32_t* )(fb_addr + x*fb_bytes + y*fb_stride);
}

constexpr pt::uint8_t cursor_size = 64;
constexpr pt::uint8_t cursor_width = 8;
constexpr bool normal_cursor_mask[cursor_size] =
                            {0,0,0,0,0,0,0,0,
                             0,1,1,1,1,1,0,0,
                             0,1,1,1,0,0,0,0,
                             0,1,1,1,0,0,0,0,
                             0,1,0,0,1,0,0,0,
                             0,1,0,0,0,1,0,0,
                             0,0,0,0,0,0,0,0,
                             0,0,0,0,0,0,0,0};
static bool captured = false;
static pt::uint64_t prevPixel[cursor_size] = {0x000000};
void Framebuffer::DrawCursor(pt::uint32_t x_pos, pt::uint32_t y_pos)
{
    if (!captured) captured = true;

    for (pt::uint8_t i = 0; i < cursor_width; i++) {
        for (pt::uint8_t j = 0; j < cursor_width; j++) {
            int pos = i*cursor_width+j;
            prevPixel[pos] = this->GetPixel((x_pos+j), (y_pos+i));
            if (normal_cursor_mask[pos] != 0) {
                this->PutPixel((x_pos+j), (y_pos+i), 0xff0000);
            }
        }
    }
}

void Framebuffer::EraseCursor(pt::uint32_t x_pos, pt::uint32_t y_pos)
{
    if (!captured) return;
    for (pt::uint8_t i = 0; i < cursor_width; i++) {
        for (pt::uint8_t j = 0; j < cursor_width; j++) {
            int pos = i*cursor_width+j;
            this->PutPixel((x_pos+j), (y_pos+i), prevPixel[pos]);
        }
    }
}

void Framebuffer::Clear(pt::uint8_t r, pt::uint8_t g, pt::uint8_t b)
{
    pt::uint32_t  fb_width  = this->m_width;
    pt::uint32_t  fb_height  = this->m_height;
    pt::uint32_t c =  (r << 16)
                | (g << 8)
                | (b);

    for (pt::uint32_t y = 0; y < fb_height; y++) {
        for (pt::uint32_t x = 0; x < fb_width ; x++) {
            this->PutPixel(x, y, c);
        }
    }
}