#pragma once
#include "defs.h"
#include "ansi.h"

constexpr pt::uint32_t VTERM_COUNT     = 4;
constexpr pt::uint32_t VTERM_MAX_COLS  = 80;
constexpr pt::uint32_t VTERM_MAX_ROWS  = 50;
constexpr pt::uint32_t VTERM_INPUT_SZ  = 128;
constexpr pt::uint32_t INVALID_VT      = 0xFFFFFFFF;

struct VTermCell {
    char          ch;
    pt::uint32_t  fg;
    pt::uint32_t  bg;
};

class VTerm {
public:
    void init(pt::uint32_t id, pt::uint32_t cols, pt::uint32_t rows);
    void put_char(char c);
    void push_input(char c);
    char pop_input();           // returns -1 if empty
    void redraw();
    void clear();
    void begin_batch();
    void end_batch();

    pt::uint32_t id() const { return m_id; }

private:
    void scroll();
    void handle_csi(char cmd);
    void render_cell(pt::uint32_t col, pt::uint32_t row);

    pt::uint32_t m_id   = INVALID_VT;
    pt::uint32_t m_cols = 0;
    pt::uint32_t m_rows = 0;

    VTermCell    m_cells[VTERM_MAX_ROWS * VTERM_MAX_COLS];

    pt::uint32_t m_cur_col = 0;
    pt::uint32_t m_cur_row = 0;
    pt::uint32_t m_fg      = 0x00FF00;   // green
    pt::uint32_t m_bg      = 0x000000;   // black

    AnsiParser   m_ansi;
    pt::uint32_t m_saved_col = 0;
    pt::uint32_t m_saved_row = 0;
    bool         m_batch     = false;

    char         m_input[VTERM_INPUT_SZ];
    pt::uint32_t m_input_read  = 0;
    pt::uint32_t m_input_write = 0;
};

extern VTerm        g_vterms[VTERM_COUNT];
extern pt::uint32_t g_active_vt;

void   vterm_init(pt::uint32_t cols, pt::uint32_t rows);
void   vterm_switch(pt::uint32_t vt_id);
VTerm* vterm_active();
VTerm* vterm_get(pt::uint32_t vt_id);
void   vterm_printf(const char* fmt, ...);
