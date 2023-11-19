#include "framebuffer.h"

void Framebuffer::Draw(const uint8_t* what,
                       uint32_t x_pos, uint32_t y_pos,
                       uint32_t width, uint32_t height)
{
    uintptr_t fb_addr   = this->m_addr;
    uint32_t  fb_width  = this->m_width;
    uint32_t  fb_stride = this->m_stride;
    uint8_t   fb_bytes  = this->m_bpp / 8;

    for (uint32_t y = y_pos; y < y_pos + height; y++)
    {
        for (uint32_t x = x_pos; x < x_pos + width ; x++)
        {
            uint32_t pos = y*3*width + x*3;
            uint32_t c =  (what[pos] << 16)
                        | (what[pos + 1] << 8)
                        | (what[pos + 2]);
            this->PutPixel(x, y, c);
        }
    }
}

void Framebuffer::PutPixel(
    uint32_t x,
    uint32_t y,
    uint32_t color) {
        uintptr_t fb_addr   = this->m_addr;
        uint32_t  fb_stride = this->m_stride;
        uint8_t   fb_bytes  = this->m_bpp / 8;
        *(uint32_t* )(fb_addr + x*fb_bytes + y * fb_stride) = color;
}

uint32_t Framebuffer::GetPixel(
    uint32_t x,
    uint32_t y) {
        uintptr_t fb_addr   = this->m_addr;
        uint32_t  fb_stride = this->m_stride;
        uint8_t   fb_bytes  = this->m_bpp / 8;
        return *(uint32_t* )(fb_addr + x*fb_bytes + y*fb_stride);
}

constexpr uint8_t cursor_size = 64;
constexpr uint8_t cursor_width = 8;
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
static uint64_t prevPixel[cursor_size] = {0x000000};
void Framebuffer::DrawCursor(uint32_t x_pos, uint32_t y_pos)
{
    if (!captured) captured = true;

    for (uint8_t i = 0; i < cursor_width; i++) {
        for (uint8_t j = 0; j < cursor_width; j++) {
            int pos = i*cursor_width+j;
            prevPixel[pos] = this->GetPixel((x_pos+j), (y_pos+i));
            if (normal_cursor_mask[pos] != 0) {
                this->PutPixel((x_pos+j), (y_pos+i), 0xff0000);
            }
        }
    }
}

void Framebuffer::EraseCursor(uint32_t x_pos, uint32_t y_pos)
{
    if (!captured) return;
    for (uint8_t i = 0; i < cursor_width; i++) {
        for (uint8_t j = 0; j < cursor_width; j++) {
            int pos = i*cursor_width+j;
            this->PutPixel((x_pos+j), (y_pos+i), prevPixel[pos]);
        }
    }
}

void Framebuffer::Clear(uint8_t r, uint8_t g, uint8_t b)
{
    uint32_t  fb_width  = this->m_width;
    uint32_t  fb_height  = this->m_height;
    uint32_t c =  (r << 16)
                | (g << 8)
                | (b);

    for (uint32_t y = 0; y < fb_height; y++) {
        for (uint32_t x = 0; x < fb_width ; x++) {
            this->PutPixel(x, y, c);
        }
    }
}