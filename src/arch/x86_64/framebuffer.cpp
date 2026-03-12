#include "framebuffer.h"
#include "window.h"
#include "vterm.h"
#include "device/fbterm.h"

Framebuffer buffer;

Framebuffer* Framebuffer::get_instance() {
    return &buffer;
}

void Framebuffer::Init(const boot_framebuffer *fb) {
    buffer = Framebuffer{fb};
}

void Framebuffer::InitBackBuffer() {
    pt::size_t size = (pt::size_t)m_stride * m_height;
    void* buf = vmm.kmalloc(size);
    if (!buf) kernel_panic("Can't allocate back buffer!", NotAbleToAllocateMemory);
    m_back = reinterpret_cast<pt::uintptr_t>(buf);
    // Copy current VRAM contents to back buffer so we start in sync
    pt::uint64_t* dst = reinterpret_cast<pt::uint64_t*>(m_back);
    pt::uint64_t* src = reinterpret_cast<pt::uint64_t*>(m_addr);
    pt::size_t qwords = size / 8;
    for (pt::size_t i = 0; i < qwords; i++)
        dst[i] = src[i];
    m_dirty = false;
}

void Framebuffer::Draw(const pt::uint8_t* what,
                       const pt::uint32_t x_pos,
                       const pt::uint32_t y_pos,
                       const pt::uint32_t width,
                       const pt::uint32_t height) {
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
    const pt::uint32_t color) {
        if (x >= m_width || y >= m_height) return;
        const pt::uintptr_t fb_addr = m_back ? m_back : m_addr;
        const pt::uint32_t  fb_stride = this->m_stride;
        const pt::uint8_t   fb_bytes  = this->m_bpp / 8;
        *reinterpret_cast<pt::uint32_t *>(fb_addr + x * fb_bytes + y * fb_stride) = color;
        m_dirty = true;
}

pt::uint32_t Framebuffer::GetPixel(
    const pt::uint32_t x,
    const pt::uint32_t y) const {
        const pt::uintptr_t fb_addr = m_back ? m_back : m_addr;
        const pt::uint32_t  fb_stride = this->m_stride;
        const pt::uint8_t   fb_bytes  = this->m_bpp / 8;
        return *reinterpret_cast<pt::uint32_t *>(fb_addr + x * fb_bytes + y * fb_stride);
}

// ── Cursor mask ─────────────────────────────────────────────────────────
constexpr pt::uint16_t cursor_size = 256;
constexpr pt::uint8_t cursor_width = 16;
constexpr pt::uint8_t normal_cursor_mask[cursor_size] = {
    2,2,2,2,2,2,2,2,2,2,2,2,2,0,0,0,
    2,1,1,1,1,1,1,1,1,1,1,2,0,0,0,0,
    2,1,1,1,1,1,1,1,1,1,2,0,0,0,0,0,
    2,1,1,1,1,1,1,1,1,2,0,0,0,0,0,0,
    2,1,1,1,1,1,1,1,2,0,0,0,0,0,0,0,
    2,1,1,1,1,1,1,2,0,0,0,0,0,0,0,0,
    2,1,1,1,1,1,1,2,0,0,0,0,0,0,0,0,
    2,1,1,1,1,1,1,1,2,0,0,0,0,0,0,0,
    2,1,1,1,2,1,1,1,1,2,0,0,0,0,0,0,
    2,1,1,2,0,2,1,1,1,1,2,0,0,0,0,0,
    2,1,2,0,0,0,2,1,1,1,1,2,0,0,0,0,
    2,2,0,0,0,0,0,2,1,1,1,1,2,0,0,0,
    2,0,0,0,0,0,0,0,2,1,1,1,1,1,2,0,
    0,0,0,0,0,0,0,0,0,2,1,1,1,2,0,0,
    0,0,0,0,0,0,0,0,0,0,2,1,2,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,2,0,0,0,0
};

void Framebuffer::InitWallpaper() {
    pt::size_t size = (pt::size_t)m_stride * m_height;
    void* buf = vmm.kcalloc(size);
    if (!buf) kernel_panic("Can't allocate wallpaper buffer!", NotAbleToAllocateMemory);
    m_wallpaper = reinterpret_cast<pt::uintptr_t>(buf);
}

void Framebuffer::DrawToWallpaper(const pt::uint8_t* data,
                                   pt::uint32_t x_pos, pt::uint32_t y_pos,
                                   pt::uint32_t width, pt::uint32_t height) {
    if (!m_wallpaper) return;
    const pt::uint32_t fb_bytes = m_bpp / 8;
    for (pt::uint32_t y = 0; y < height; y++) {
        for (pt::uint32_t x = 0; x < width; x++) {
            pt::uint32_t px = x_pos + x;
            pt::uint32_t py = y_pos + y;
            if (px >= m_width || py >= m_height) continue;
            pt::uint32_t pos = y * 3 * width + x * 3;
            pt::uint32_t color = (pt::uint32_t)data[pos] << 16
                               | (pt::uint32_t)data[pos + 1] << 8
                               | (pt::uint32_t)data[pos + 2];
            *reinterpret_cast<pt::uint32_t*>(m_wallpaper + py * m_stride + px * fb_bytes) = color;
        }
    }
}

void Framebuffer::ClearWallpaper(pt::uint8_t r, pt::uint8_t g, pt::uint8_t b) {
    if (!m_wallpaper) return;
    const pt::uint32_t color = (pt::uint32_t)r << 16 | (pt::uint32_t)g << 8 | (pt::uint32_t)b;
    const pt::uint32_t fb_bytes = m_bpp / 8;
    for (pt::uint32_t y = 0; y < m_height; y++)
        for (pt::uint32_t x = 0; x < m_width; x++)
            *reinterpret_cast<pt::uint32_t*>(m_wallpaper + y * m_stride + x * fb_bytes) = color;
}

void Framebuffer::set_cursor_pos(pt::int16_t x, pt::int16_t y, bool visible) {
    m_cursor_x = x;
    m_cursor_y = y;
    m_cursor_visible = visible;
    m_dirty = true;
}

void Framebuffer::Flush() {
    if (!m_back || !m_addr || !m_width) return;

    const pt::uint32_t fb_bytes = m_bpp / 8;
    const pt::size_t fb_size = (pt::size_t)m_stride * m_height;
    const pt::size_t qwords = fb_size / 8;

    // Layer 0: Copy wallpaper → back buffer (reset to clean state every frame)
    if (m_wallpaper) {
        pt::uint64_t* dst = reinterpret_cast<pt::uint64_t*>(m_back);
        pt::uint64_t* src = reinterpret_cast<pt::uint64_t*>(m_wallpaper);
        for (pt::size_t i = 0; i < qwords; i++)
            dst[i] = src[i];
    }

    // Layer 1: Render active VTerm cells onto back buffer
    {
        VTerm* vt = vterm_active();
        if (vt && fbterm.is_ready()) {
            const VTermCell* cells = vt->get_cells();
            pt::uint32_t cols = vt->get_cols();
            pt::uint32_t rows = vt->get_rows();
            pt::uint32_t gw = fbterm.glyph_w();
            pt::uint32_t gh = fbterm.glyph_h();
            for (pt::uint32_t r = 0; r < rows; r++)
                for (pt::uint32_t c = 0; c < cols; c++) {
                    const VTermCell& cell = cells[r * cols + c];
                    // Skip transparent cells (space with black bg) so wallpaper shows
                    if (cell.ch == ' ' && cell.bg == 0x000000) continue;
                    fbterm.put_char_at(cell.ch, c * gw, r * gh, cell.fg, cell.bg);
                }
        }
    }

    // Layer 2+: Composite windows + chrome onto back buffer
    WindowManager::composite(this);

    m_dirty = false;

    // Flip: copy back buffer → VRAM
    {
        pt::uint64_t* src = reinterpret_cast<pt::uint64_t*>(m_back);
        pt::uint64_t* dst = reinterpret_cast<pt::uint64_t*>(m_addr);
        for (pt::size_t i = 0; i < qwords; i++)
            dst[i] = src[i];
    }

    // Top layer: cursor on VRAM (not in back buffer)
    if (m_cursor_visible) {
        for (pt::uint8_t i = 0; i < cursor_width; i++) {
            for (pt::uint8_t j = 0; j < cursor_width; j++) {
                pt::int32_t px = (pt::int32_t)m_cursor_x + j;
                pt::int32_t py = (pt::int32_t)m_cursor_y + i;
                if (px < 0 || py < 0) continue;
                if ((pt::uint32_t)px >= m_width || (pt::uint32_t)py >= m_height) continue;
                const int pos = i * cursor_width + j;
                if (normal_cursor_mask[pos] != 0) {
                    pt::uint32_t color = normal_cursor_mask[pos] == 1 ? 0xffffff : 0x808080;
                    *reinterpret_cast<pt::uint32_t*>(m_addr + (pt::uint32_t)px * fb_bytes + (pt::uint32_t)py * m_stride) = color;
                }
            }
        }
    }
}

void Framebuffer::ScrollRegionUp(const pt::uint32_t x, const pt::uint32_t y,
                                 const pt::uint32_t w, const pt::uint32_t h,
                                 const pt::uint32_t pixels) {
    const pt::uintptr_t base = m_back ? m_back : m_addr;
    const pt::uint32_t fb_bytes = m_bpp / 8;
    // Copy rows upward one at a time
    for (pt::uint32_t src_row = y + pixels; src_row < y + h && src_row < m_height; src_row++) {
        const pt::uint32_t dst_row = src_row - pixels;
        pt::uint8_t* src = reinterpret_cast<pt::uint8_t*>(base + src_row * m_stride + x * fb_bytes);
        pt::uint8_t* dst = reinterpret_cast<pt::uint8_t*>(base + dst_row * m_stride + x * fb_bytes);
        for (pt::uint32_t i = 0; i < w * fb_bytes; i++)
            dst[i] = src[i];
    }
    // Clear the vacated bottom rows
    const pt::uint32_t clear_start = (y + h > pixels) ? (y + h - pixels) : y;
    for (pt::uint32_t row = clear_start; row < y + h && row < m_height; row++)
        for (pt::uint32_t col = x; col < x + w && col < m_width; col++)
            PutPixel(col, row, 0x000000);
}

void Framebuffer::FillRect(const pt::uint32_t x, const pt::uint32_t y,
                           const pt::uint32_t w, const pt::uint32_t h,
                           const pt::uint8_t r, const pt::uint8_t g, const pt::uint8_t b) {
    const pt::uint32_t color = static_cast<pt::uint32_t>(r) << 16
                             | static_cast<pt::uint32_t>(g) << 8
                             | static_cast<pt::uint32_t>(b);
    for (pt::uint32_t row = y; row < y + h && row < m_height; row++)
        for (pt::uint32_t col = x; col < x + w && col < m_width; col++)
            this->PutPixel(col, row, color);
}

void Framebuffer::Clear(const pt::uint8_t r, const pt::uint8_t g, const pt::uint8_t b) {
    // Clear the wallpaper — compositor will repaint from it next frame
    ClearWallpaper(r, g, b);
    m_dirty = true;
}
