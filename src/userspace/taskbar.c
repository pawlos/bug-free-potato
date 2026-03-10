#include "libc/syscall.h"

#define BAR_H       16      /* one glyph row */
#define GLYPH_W     8
#define GLYPH_H     16
#define DOT_PAD     0
#define DATETIME_CHARS 14   /* "DD/MM/YY HH:MM" */
#define RIGHT_W     (DATETIME_CHARS * GLYPH_W + 4) /* datetime+margin */
#define MAX_WIN     8
#define LOGO_TEXT   "potatOS"
#define LOGO_CHARS  7
#define LOGO_W      (LOGO_CHARS * GLYPH_W)  /* 56 px */
#define LOGO_PAD    12                        /* gap after logo before separator */
#define TABS_X      (4 + LOGO_W + LOGO_PAD)  /* where window tabs start */

#define COLOR_BG      0x1E1E2E   /* dark blue-gray background */
#define COLOR_FG      0xCDD6F4   /* light text */
#define COLOR_FOCUSED 0x89B4FA   /* focused window highlight */
#define COLOR_NORMAL  0x585B70   /* unfocused window text */
#define COLOR_SEP     0x45475A   /* separator color */

static char time_buf[16] = "01/01/00 00:00";
static unsigned char last_minute = 0xFF;
static unsigned char last_day = 0xFF;
static struct WinListEntry prev_wins[MAX_WIN];
static long prev_win_count = -1;
static long bar_w;

static int str_eq(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return *a == *b;
}

static void draw_bar_bg(void) {
    sys_fill_rect(0, 0, bar_w, BAR_H, COLOR_BG);
}

static void draw_separator(long x) {
    sys_fill_rect(x, 2, 1, BAR_H - 4, COLOR_SEP);
}

static void draw_clock(void) {
    long time = sys_get_time();
    unsigned char minutes = time & 0xFF;
    unsigned char hours   = (time >> 8) & 0xFF;
    unsigned char month   = (time >> 16) & 0xFF;
    unsigned char year    = (time >> 24) & 0xFF;
    unsigned char day     = (time >> 32) & 0xFF;

    if (minutes == last_minute && day == last_day) return;
    last_minute = minutes;
    last_day = day;

    /* Format DD/MM/YY HH:MM */
    time_buf[0]  = '0' + day / 10;
    time_buf[1]  = '0' + day % 10;
    time_buf[2]  = '/';
    time_buf[3]  = '0' + month / 10;
    time_buf[4]  = '0' + month % 10;
    time_buf[5]  = '/';
    time_buf[6]  = '0' + year / 10;
    time_buf[7]  = '0' + year % 10;
    time_buf[8]  = ' ';
    time_buf[9]  = '0' + hours / 10;
    time_buf[10] = '0' + hours % 10;
    time_buf[11] = ':';
    time_buf[12] = '0' + minutes / 10;
    time_buf[13] = '0' + minutes % 10;
    time_buf[14] = '\0';

    long clock_x = bar_w - RIGHT_W + 2;
    sys_draw_text(clock_x, 0, time_buf, COLOR_FG, COLOR_BG);
}

static void draw_window_list(void) {
    struct WinListEntry wins[MAX_WIN];
    long count = sys_list_windows(wins, MAX_WIN);

    /* Check if anything changed */
    int changed = (count != prev_win_count);
    if (!changed) {
        for (long i = 0; i < count; i++) {
            if (wins[i].wid != prev_wins[i].wid ||
                wins[i].flags != prev_wins[i].flags ||
                !str_eq(wins[i].title, prev_wins[i].title)) {
                changed = 1;
                break;
            }
        }
    }
    if (!changed) return;

    /* Save state */
    prev_win_count = count;
    for (long i = 0; i < count; i++)
        prev_wins[i] = wins[i];

    /* Always draw the logo on the left */
    sys_draw_text(4, 0, LOGO_TEXT, COLOR_FG, COLOR_BG);

    /* Clear only the window tab area (after logo) */
    long tabs_end = bar_w - RIGHT_W - 4;
    long tabs_w = tabs_end - TABS_X;
    if (tabs_w > 0)
        sys_fill_rect(TABS_X, 0, tabs_w, BAR_H, COLOR_BG);

    if (count == 0) {
        return;
    }

    /* Draw separator between logo and tabs */
    draw_separator(TABS_X - LOGO_PAD / 2);

    /* Draw each window as a tab */
    long x = TABS_X;
    long max_tab_w = tabs_w / (count > 0 ? count : 1);
    if (max_tab_w > 160) max_tab_w = 160;  /* cap tab width */

    for (long i = 0; i < count; i++) {
        long fg = (wins[i].flags & 1) ? COLOR_FOCUSED : COLOR_NORMAL;
        const char *title = wins[i].title[0] ? wins[i].title : "???";

        /* Truncate title to fit tab */
        char trunc[20];
        long max_chars = (max_tab_w - 8) / GLYPH_W;
        if (max_chars > 19) max_chars = 19;
        if (max_chars < 3) max_chars = 3;
        long j = 0;
        while (j < max_chars && title[j]) {
            trunc[j] = title[j];
            j++;
        }
        trunc[j] = '\0';

        sys_draw_text(x, 0, trunc, fg, COLOR_BG);
        x += (j + 1) * GLYPH_W;

        /* Draw separator between tabs */
        if (i + 1 < count)
            draw_separator(x - GLYPH_W / 2);
    }
}

int main(void) {
    long fb_w = sys_fb_width();
    long fb_h = sys_fb_height();
    bar_w = fb_w;

    /* Create chromeless window at the bottom of the screen */
    sys_create_window_chromeless(0, fb_h - BAR_H, fb_w, BAR_H);

    /* Initial draw */
    draw_bar_bg();

    /* Draw a separator line between clock area and window list */
    draw_separator(bar_w - RIGHT_W - 2);

    /* Force first draw */
    last_minute = 0xFF;
    last_day = 0xFF;

    for (;;) {
        draw_clock();
        draw_window_list();
        sys_sleep_ms(500);
    }
}
