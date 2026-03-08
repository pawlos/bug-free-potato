#include "vterm.h"
#include "device/fbterm.h"
#include "framebuffer.h"
#include "print.h"
#include "window.h"
#include <cstdarg>

VTerm        g_vterms[VTERM_COUNT];
pt::uint32_t g_active_vt = INVALID_VT;

// Shared shadow buffer for batch diff rendering (only one VTerm batches at a time)
static VTermCell g_batch_shadow[VTERM_MAX_ROWS * VTERM_MAX_COLS];

// ── helpers ────────────────────────────────────────────────────────────

static bool is_active(pt::uint32_t id) {
    return g_active_vt == id;
}

// ── VTerm implementation ───────────────────────────────────────────────

void VTerm::init(pt::uint32_t id, pt::uint32_t cols, pt::uint32_t rows) {
    m_id   = id;
    m_cols = (cols > VTERM_MAX_COLS) ? VTERM_MAX_COLS : cols;
    m_rows = (rows > VTERM_MAX_ROWS) ? VTERM_MAX_ROWS : rows;
    // Explicitly set — global constructors don't run in freestanding.
    m_fg   = 0x00FF00;
    m_bg   = 0x000000;
    m_cur_col     = 0;
    m_cur_row     = 0;
    m_input_read  = 0;
    m_input_write = 0;
    clear();
}

void VTerm::clear() {
    for (pt::uint32_t i = 0; i < m_rows * m_cols; i++) {
        m_cells[i].ch = ' ';
        m_cells[i].fg = m_fg;
        m_cells[i].bg = m_bg;
    }
    m_cur_col = 0;
    m_cur_row = 0;
    m_ansi.state    = AnsiParser::NORMAL;
    m_ansi.n_params = 0;
    m_saved_col = 0;
    m_saved_row = 0;
}

void VTerm::render_cell(pt::uint32_t col, pt::uint32_t row) {
    if (m_batch || !is_active(m_id) || !fbterm.is_ready()) return;
    const VTermCell& c = m_cells[row * m_cols + col];
    pt::uint32_t px = col * fbterm.glyph_w();
    pt::uint32_t py = row * fbterm.glyph_h();
    fbterm.put_char_at(c.ch, px, py, c.fg, c.bg);
}

void VTerm::scroll() {
    // Shift cell rows up by 1
    for (pt::uint32_t r = 1; r < m_rows; r++) {
        for (pt::uint32_t c = 0; c < m_cols; c++) {
            m_cells[(r - 1) * m_cols + c] = m_cells[r * m_cols + c];
        }
    }
    // Clear bottom row
    for (pt::uint32_t c = 0; c < m_cols; c++) {
        m_cells[(m_rows - 1) * m_cols + c] = { ' ', m_fg, m_bg };
    }
    m_cur_row = m_rows - 1;

    if (!m_batch && is_active(m_id) && fbterm.is_ready()) {
        pt::uint32_t gw = fbterm.glyph_w();
        pt::uint32_t gh = fbterm.glyph_h();
        pt::uint32_t tw = m_cols * gw;
        pt::uint32_t th = m_rows * gh;
        fbterm.scroll_region(0, 0, tw, th, gh);
        // Render cleared bottom row
        for (pt::uint32_t c = 0; c < m_cols; c++)
            render_cell(c, m_rows - 1);
    }
}

void VTerm::handle_csi(char cmd) {
    auto P = [&](pt::uint32_t i, pt::uint32_t def) -> pt::uint32_t {
        return (i < m_ansi.n_params && m_ansi.params[i] != 0) ? m_ansi.params[i] : def;
    };

    switch (cmd) {
    case 'A': {
        pt::uint32_t n = P(0, 1);
        m_cur_row = (m_cur_row >= n) ? m_cur_row - n : 0;
        break;
    }
    case 'B': {
        pt::uint32_t n = P(0, 1);
        m_cur_row += n;
        if (m_cur_row >= m_rows) m_cur_row = m_rows - 1;
        break;
    }
    case 'C': {
        pt::uint32_t n = P(0, 1);
        m_cur_col += n;
        if (m_cur_col >= m_cols) m_cur_col = m_cols - 1;
        break;
    }
    case 'D': {
        pt::uint32_t n = P(0, 1);
        m_cur_col = (m_cur_col >= n) ? m_cur_col - n : 0;
        break;
    }
    case 'H':
    case 'f': {
        pt::uint32_t r = P(0, 1) - 1;
        pt::uint32_t c = P(1, 1) - 1;
        m_cur_row = (r < m_rows) ? r : m_rows - 1;
        m_cur_col = (c < m_cols) ? c : m_cols - 1;
        break;
    }
    case 'J': {
        pt::uint32_t mode = m_ansi.params[0];
        if (mode == 2) {
            for (pt::uint32_t i = 0; i < m_rows * m_cols; i++)
                m_cells[i] = { ' ', m_fg, m_bg };
            m_cur_col = m_cur_row = 0;
            if (!m_batch && is_active(m_id)) redraw();
        } else if (mode == 0) {
            // Clear from cursor to end
            for (pt::uint32_t c = m_cur_col; c < m_cols; c++)
                m_cells[m_cur_row * m_cols + c] = { ' ', m_fg, m_bg };
            for (pt::uint32_t r = m_cur_row + 1; r < m_rows; r++)
                for (pt::uint32_t c = 0; c < m_cols; c++)
                    m_cells[r * m_cols + c] = { ' ', m_fg, m_bg };
            if (!m_batch && is_active(m_id)) redraw();
        }
        break;
    }
    case 'K': {
        pt::uint32_t mode = m_ansi.params[0];
        if (mode == 0) {
            for (pt::uint32_t c = m_cur_col; c < m_cols; c++)
                m_cells[m_cur_row * m_cols + c] = { ' ', m_fg, m_bg };
        } else if (mode == 2) {
            for (pt::uint32_t c = 0; c < m_cols; c++)
                m_cells[m_cur_row * m_cols + c] = { ' ', m_fg, m_bg };
            m_cur_col = 0;
        }
        if (is_active(m_id)) {
            for (pt::uint32_t c = 0; c < m_cols; c++)
                render_cell(c, m_cur_row);
        }
        break;
    }
    case 's':
        m_saved_col = m_cur_col;
        m_saved_row = m_cur_row;
        break;
    case 'u':
        m_cur_col = m_saved_col;
        m_cur_row = m_saved_row;
        break;
    case 'm': {
        for (pt::uint32_t i = 0; i < m_ansi.n_params; i++) {
            pt::uint32_t v = m_ansi.params[i];
            if (v == 0)                    { m_fg = 0x00FF00; m_bg = 0x000000; }
            else if (v >= 30 && v <= 37)   m_fg = ansi_color(v - 30);
            else if (v >= 40 && v <= 47)   m_bg = ansi_color(v - 40);
            else if (v >= 90 && v <= 97)   m_fg = ansi_color(v - 90 + 8);
            else if (v >= 100 && v <= 107) m_bg = ansi_color(v - 100 + 8);
        }
        break;
    }
    }
}

void VTerm::put_char(char c) {
    char final_byte = 0;
    bool complete   = m_ansi.feed(c, final_byte);

    if (complete) {
        if (!m_ansi.private_mode)
            handle_csi(final_byte);
        m_ansi.private_mode = false;
        return;
    }
    if (m_ansi.state != AnsiParser::NORMAL) return;

    if (c == '\n') {
        m_cur_col = 0;
        m_cur_row++;
        if (m_cur_row >= m_rows) scroll();
        return;
    }
    if (c == '\r') {
        m_cur_col = 0;
        return;
    }
    if (c == '\b') {
        if (m_cur_col > 0) m_cur_col--;
        return;
    }
    if (c == '\t') {
        m_cur_col = (m_cur_col + 4) & ~3u;
        if (m_cur_col >= m_cols) {
            m_cur_col = 0;
            m_cur_row++;
            if (m_cur_row >= m_rows) scroll();
        }
        return;
    }

    // Printable character
    VTermCell& cell = m_cells[m_cur_row * m_cols + m_cur_col];
    cell.ch = c;
    cell.fg = m_fg;
    cell.bg = m_bg;
    render_cell(m_cur_col, m_cur_row);

    m_cur_col++;
    if (m_cur_col >= m_cols) {
        m_cur_col = 0;
        m_cur_row++;
        if (m_cur_row >= m_rows) scroll();
    }
}

void VTerm::push_input(char c) {
    m_input[m_input_write % VTERM_INPUT_SZ] = c;
    m_input_write++;
}

char VTerm::pop_input() {
    if (m_input_read >= m_input_write) return -1;
    char c = m_input[m_input_read % VTERM_INPUT_SZ];
    m_input_read++;
    return c;
}

void VTerm::redraw() {
    if (!fbterm.is_ready()) return;

    pt::uint32_t gw = fbterm.glyph_w();
    pt::uint32_t gh = fbterm.glyph_h();

    // Render every cell — blanks are drawn as bg-colored rectangles,
    // which implicitly clears whatever was on screen before.
    for (pt::uint32_t r = 0; r < m_rows; r++)
        for (pt::uint32_t c = 0; c < m_cols; c++) {
            const VTermCell& cell = m_cells[r * m_cols + c];
            fbterm.put_char_at(cell.ch, c * gw, r * gh, cell.fg, cell.bg);
        }
}

void VTerm::begin_batch() {
    pt::uint32_t total = m_rows * m_cols;
    for (pt::uint32_t i = 0; i < total; i++)
        g_batch_shadow[i] = m_cells[i];
    m_batch = true;
}

void VTerm::end_batch() {
    m_batch = false;
    if (!is_active(m_id) || !fbterm.is_ready()) return;

    pt::uint32_t gw = fbterm.glyph_w();
    pt::uint32_t gh = fbterm.glyph_h();
    pt::uint32_t total = m_rows * m_cols;
    for (pt::uint32_t i = 0; i < total; i++) {
        const VTermCell& cur = m_cells[i];
        const VTermCell& old = g_batch_shadow[i];
        if (cur.ch != old.ch || cur.fg != old.fg || cur.bg != old.bg) {
            pt::uint32_t col = i % m_cols;
            pt::uint32_t row = i / m_cols;
            fbterm.put_char_at(cur.ch, col * gw, row * gh, cur.fg, cur.bg);
        }
    }
}

// ── global functions ───────────────────────────────────────────────────

void vterm_init(pt::uint32_t cols, pt::uint32_t rows) {
    for (pt::uint32_t i = 0; i < VTERM_COUNT; i++)
        g_vterms[i].init(i, cols, rows);
    g_active_vt = 0;
}

void vterm_switch(pt::uint32_t vt_id) {
    if (vt_id >= VTERM_COUNT || vt_id == g_active_vt) return;
    // Save outgoing VT's focus
    if (g_active_vt < VTERM_COUNT)
        WindowManager::focused_per_vt[g_active_vt] = WindowManager::focused_id;
    g_active_vt = vt_id;
    // Clear entire screen, redraw text + windows for new VT
    Framebuffer* fb = Framebuffer::get_instance();
    if (fb) fb->Clear(0, 0, 0);
    g_vterms[vt_id].redraw();
    WindowManager::on_vt_switch();
}

VTerm* vterm_active() {
    if (g_active_vt >= VTERM_COUNT) return nullptr;
    return &g_vterms[g_active_vt];
}

VTerm* vterm_get(pt::uint32_t vt_id) {
    if (vt_id >= VTERM_COUNT) return nullptr;
    return &g_vterms[vt_id];
}

// vterm_printf — printf-style output to the active VTerm.
// Uses the same format specifiers as ComDevice::print_str.
static void vterm_putstr(const char* s) {
    if (g_active_vt >= VTERM_COUNT) return;
    while (*s) g_vterms[g_active_vt].put_char(*s++);
}

void vterm_printf(const char* fmt, ...) {
    if (g_active_vt >= VTERM_COUNT) return;

    va_list args;
    va_start(args, fmt);

    for (pt::size_t i = 0; fmt[i] != '\0'; i++) {
        if (fmt[i] != '%') {
            g_vterms[g_active_vt].put_char(fmt[i]);
            continue;
        }
        char next = fmt[i + 1];
        switch (next) {
        case '%':
            g_vterms[g_active_vt].put_char('%');
            i++;
            break;
        case 'i':
        case 'd': {
            int a = va_arg(args, int);
            vterm_putstr(decToString(a));
            i++;
            break;
        }
        case 'l': {
            pt::uint64_t a = va_arg(args, pt::uint64_t);
            pt::size_t skip = 1;
            char spec = fmt[i + 2];
            if (spec == 'l') { skip = 2; spec = fmt[i + 3]; }
            switch (spec) {
                case 'x': vterm_putstr("0x"); vterm_putstr(hexToString(a, false)); break;
                case 'X': vterm_putstr("0x"); vterm_putstr(hexToString(a, true));  break;
                default:  vterm_putstr(decToString(a)); break;
            }
            i += skip + 1;
            break;
        }
        case 'p': {
            pt::size_t ptr = va_arg(args, pt::size_t);
            vterm_putstr("0x");
            vterm_putstr(hexToString(ptr, false));
            i++;
            break;
        }
        case 's': {
            const char* a = va_arg(args, const char*);
            if (a) vterm_putstr(a);
            i++;
            break;
        }
        case 'c': {
            char a = (char)va_arg(args, int);
            g_vterms[g_active_vt].put_char(a);
            i++;
            break;
        }
        case 'x': {
            pt::uint64_t a = va_arg(args, pt::uint64_t);
            vterm_putstr("0x");
            vterm_putstr(hexToString(a, false));
            i++;
            break;
        }
        case 'X': {
            pt::uint64_t a = va_arg(args, pt::uint64_t);
            vterm_putstr("0x");
            vterm_putstr(hexToString(a, true));
            i++;
            break;
        }
        default:
            g_vterms[g_active_vt].put_char(fmt[i]);
            break;
        }
    }

    va_end(args);
}
