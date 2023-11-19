#ifndef _FRAMEBUFFER_H_
#define _FRAMEBUFFER_H_

#include "defs.h"
#include "boot.h"
#include "virtual.h"

class Framebuffer
{
private:
    uintptr_t m_addr;
    uint32_t  m_width;
    uint32_t  m_height;
    uint32_t  m_bpp;
    uint32_t  m_stride;
    uintptr_t *vga_font;
    Framebuffer(uintptr_t addr, uint32_t width,
                uint32_t height, uint32_t bpp,
                uint32_t stride) : m_addr(addr), m_width(width),
                                   m_height(height), m_bpp(bpp),
                                   m_stride(stride)
    {
        vga_font = (uintptr_t *)VMM::Instance()->kmalloc(0x4096);
        if (!vga_font) kernel_panic("Can't allocate memory!", NotAbleToAllocateMemory);
    }
    void PutPixel(
        uint32_t x, uint32_t y,
        uint32_t color);
    uint32_t GetPixel(
        uint32_t x, uint32_t y);
public:
    Framebuffer(boot_framebuffer *fb) : Framebuffer(
                                             fb->framebuffer_addr,
                                             fb->framebuffer_width,
                                             fb->framebuffer_height,
                                             fb->framebuffer_bpp,
                                             fb->framebuffer_pitch)
    {}

    void Draw(const uint8_t* what,
              uint32_t width, uint32_t height,
              uint32_t x_pos, uint32_t y_pos);

    void Clear(uint8_t r, uint8_t g, uint8_t b);

    void DrawCursor(uint32_t x_pos, uint32_t y_pos);
    void EraseCursor(uint32_t x_pos, uint32_t y_pos);
};
#endif