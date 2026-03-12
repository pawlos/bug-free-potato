#pragma once
#include "defs.h"
#include "boot.h"
#include "virtual.h"

class Framebuffer
{
    pt::uintptr_t m_addr;       // VRAM (front buffer)
    pt::uintptr_t m_back;       // back buffer (all rendering goes here)
    pt::uintptr_t m_wallpaper;  // wallpaper buffer (persistent desktop layer)
    pt::uint32_t  m_width;
    pt::uint32_t  m_height;
    pt::uint32_t  m_bpp;
    pt::uint32_t  m_stride;
    pt::uintptr_t *vga_font;
    bool          m_dirty;      // true if back buffer has changed since last flush

    // Cursor state (composited during Flush, not drawn to back buffer)
    pt::int16_t   m_cursor_x;
    pt::int16_t   m_cursor_y;
    bool          m_cursor_visible;

    Framebuffer(const pt::uintptr_t addr, const pt::uint32_t width,
                const pt::uint32_t height, const pt::uint32_t bpp,
                const pt::uint32_t stride) : m_addr(addr), m_back(0),
                                   m_wallpaper(0), m_width(width), m_height(height),
                                   m_bpp(bpp), m_stride(stride),
                                   m_dirty(false),
                                   m_cursor_x(0), m_cursor_y(0),
                                   m_cursor_visible(false)
    {
        vga_font = static_cast<pt::uintptr_t *>(VMM::Instance()->kmalloc(0x4096));
        if (!vga_font) kernel_panic("Can't allocate memory!", NotAbleToAllocateMemory);
    }
    void PutPixel(
        pt::uint32_t x, pt::uint32_t y,
        pt::uint32_t color);
    [[nodiscard]] pt::uint32_t GetPixel(
        pt::uint32_t x, pt::uint32_t y) const;
public:
    Framebuffer()=default;
    explicit Framebuffer(const boot_framebuffer *fb) : Framebuffer(
                                         fb->framebuffer_addr,
                                         fb->framebuffer_width,
                                         fb->framebuffer_height,
                                         fb->framebuffer_bpp,
                                         fb->framebuffer_pitch)
    {}

    static void Init(const boot_framebuffer *fb);
    void InitBackBuffer();
    pt::uint32_t  get_width()  const { return m_width; }
    pt::uint32_t  get_height() const { return m_height; }
    pt::uintptr_t get_back()   const { return m_back; }
    pt::uint32_t  get_stride() const { return m_stride; }
    pt::uint32_t  get_bpp()    const { return m_bpp; }
    void Free() {
        if (m_wallpaper) {
            vmm.kfree(reinterpret_cast<void*>(m_wallpaper));
            m_wallpaper = 0;
        }
        if (m_back) {
            vmm.kfree(reinterpret_cast<void*>(m_back));
            m_back = 0;
        }
        vmm.kfree(vga_font);
        this->vga_font = nullptr;
    }
    static Framebuffer* get_instance();

    void Draw(const pt::uint8_t* what,
              pt::uint32_t x_pos, pt::uint32_t y_pos,
              pt::uint32_t width, pt::uint32_t height);

    void Clear(pt::uint8_t r, pt::uint8_t g, pt::uint8_t b);
    void FillRect(pt::uint32_t x, pt::uint32_t y,
                  pt::uint32_t w, pt::uint32_t h,
                  pt::uint8_t r, pt::uint8_t g, pt::uint8_t b);

    // Scroll a rectangular region up by `pixels` rows, clearing the vacated bottom area.
    void ScrollRegionUp(pt::uint32_t x, pt::uint32_t y,
                        pt::uint32_t w, pt::uint32_t h,
                        pt::uint32_t pixels);

    // Cursor position (composited during Flush, not drawn to back buffer)
    void set_cursor_pos(pt::int16_t x, pt::int16_t y, bool visible);

    // Allocate wallpaper buffer and fill with black.
    void InitWallpaper();

    // Draw an RGB24 image into the wallpaper buffer.
    void DrawToWallpaper(const pt::uint8_t* data,
                         pt::uint32_t x, pt::uint32_t y,
                         pt::uint32_t w, pt::uint32_t h);

    // Fill wallpaper buffer with a solid color.
    void ClearWallpaper(pt::uint8_t r, pt::uint8_t g, pt::uint8_t b);

    // Copy back buffer to VRAM (with cursor overlay). Called from timer interrupt.
    void Flush();
};
