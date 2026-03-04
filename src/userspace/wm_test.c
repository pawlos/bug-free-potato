#include "libc/stdio.h"
#include "libc/syscall.h"

/* PS/2 set-1 scancodes */
#define SC_ESC   0x01
#define SC_Q     0x10
#define SC_UP    0x48
#define SC_DOWN  0x50
#define SC_LEFT  0x4B
#define SC_RIGHT 0x4D

/* Window client area */
#define WIN_X  50
#define WIN_Y  50
#define WIN_W  300
#define WIN_H  200

/* Colors (0xRRGGBB) */
#define COL_BG      0x002244L
#define COL_TEXT_FG 0xFFFFFFL
#define COL_TEXT_BG 0x002244L
#define COL_IND     0x00FF00L

/* Indicator size */
#define IND_W 10
#define IND_H 10

static void draw_indicator(int x, int y, long color)
{
    sys_fill_rect(x, y, IND_W, IND_H, color);
}

int main(void)
{
    long wid = sys_create_window(WIN_X, WIN_Y, WIN_W, WIN_H);
    if (wid < 0) {
        puts("wm_test: failed to create window");
        return 1;
    }
    sys_set_window_title(wid, "WM Test");

    /* Draw background */
    sys_fill_rect(0, 0, WIN_W, WIN_H, COL_BG);

    /* Draw title text inside window */
    sys_draw_text(4, 2,  "potat OS windowing v1", COL_TEXT_FG, COL_TEXT_BG);
    sys_draw_text(4, 20, "Arrows: move  Esc/Q: quit", COL_TEXT_FG, COL_TEXT_BG);

    /* Separator line */
    sys_fill_rect(0, 36, WIN_W, 1, 0x888888L);

    /* Initial indicator position (rough center) */
    int ind_x = (WIN_W - IND_W) / 2;
    int ind_y = 36 + (WIN_H - 36 - IND_H) / 2;
    draw_indicator(ind_x, ind_y, COL_IND);

    while (1) {
        long ev = sys_get_window_event(wid);
        if (ev == 0) {
            sys_yield();
            continue;
        }

        int pressed  = (ev & 0x100) != 0;
        int sc       = (int)(ev & 0xFF);

        if (!pressed) continue;

        /* Exit on Esc or Q */
        if (sc == SC_ESC || sc == SC_Q) break;

        int nx = ind_x;
        int ny = ind_y;

        if (sc == SC_UP)    ny -= 10;
        if (sc == SC_DOWN)  ny += 10;
        if (sc == SC_LEFT)  nx -= 10;
        if (sc == SC_RIGHT) nx += 10;

        /* Clamp to client area */
        if (nx < 0)             nx = 0;
        if (nx > WIN_W - IND_W) nx = WIN_W - IND_W;
        if (ny < 0)             ny = 0;
        if (ny > WIN_H - IND_H) ny = WIN_H - IND_H;

        if (nx != ind_x || ny != ind_y) {
            draw_indicator(ind_x, ind_y, COL_BG);   /* erase old */
            ind_x = nx;
            ind_y = ny;
            draw_indicator(ind_x, ind_y, COL_IND);  /* draw new */
        }
    }

    sys_destroy_window(wid);
    puts("wm_test: done");
    return 0;
}
