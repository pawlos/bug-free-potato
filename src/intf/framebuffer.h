#ifndef _FRAMEBUFFER_H_
#define _FRAMEBUFFER_H_

#include "defs.h"
#include "boot.h"
#include "virtual.h"

class Framebuffer
{
    pt::uintptr_t m_addr;
    pt::uint32_t  m_width;
    pt::uint32_t  m_height;
    pt::uint32_t  m_bpp;
    pt::uint32_t  m_stride;
    pt::uintptr_t *vga_font;
    Framebuffer(pt::uintptr_t addr, pt::uint32_t width,
                pt::uint32_t height, pt::uint32_t bpp,
                pt::uint32_t stride) : m_addr(addr), m_width(width),
                                   m_height(height), m_bpp(bpp),
                                   m_stride(stride)
    {
        vga_font = static_cast<pt::uintptr_t *>(VMM::Instance()->kmalloc(0x4096));
        if (!vga_font) kernel_panic("Can't allocate memory!", NotAbleToAllocateMemory);
    }
    void PutPixel(
        pt::uint32_t x, pt::uint32_t y,
        pt::uint32_t color) const;
    [[nodiscard]] pt::uint32_t GetPixel(
        pt::uint32_t x, pt::uint32_t y) const;
public:
    Framebuffer(const boot_framebuffer *fb) : Framebuffer(
                                             fb->framebuffer_addr,
                                             fb->framebuffer_width,
                                             fb->framebuffer_height,
                                             fb->framebuffer_bpp,
                                             fb->framebuffer_pitch)
    {}

    void Draw(const pt::uint8_t* what,
              pt::uint32_t x_pos, pt::uint32_t y_pos,
              pt::uint32_t width, pt::uint32_t height) const;

    void Clear(pt::uint8_t r, pt::uint8_t g, pt::uint8_t b) const;

    void DrawCursor(pt::uint32_t x_pos, pt::uint32_t y_pos) const;
    void EraseCursor(pt::uint32_t x_pos, pt::uint32_t y_pos) const;
};
#endif