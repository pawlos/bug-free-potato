#include "libc/syscall.h"

#define BAR_H       16      /* one glyph row */
#define GLYPH_W     8
#define GLYPH_H     16
#define DATETIME_CHARS 14   /* "DD/MM/YY HH:MM" */
#define RIGHT_W     (DATETIME_CHARS * GLYPH_W + 4) /* datetime+margin */
#define MAX_WIN     8

#define START_TEXT  "potatOS"
#define START_CHARS 7
#define START_W     (START_CHARS * GLYPH_W + 8)  /* button width with padding */
#define LOGO_PAD    8
#define TABS_X      (START_W + LOGO_PAD)

#define COLOR_BG      0x1E1E2E   /* dark blue-gray background */
#define COLOR_FG      0xCDD6F4   /* light text */
#define COLOR_FOCUSED 0x89B4FA   /* focused window highlight */
#define COLOR_NORMAL  0x585B70   /* unfocused window text */
#define COLOR_SEP     0x45475A   /* separator color */
#define COLOR_START   0x4C8C4A   /* start button green */
#define COLOR_START_H 0x6AB668   /* start button hover/active */
#define COLOR_MENU_BG 0x2D2D44   /* menu background */
#define COLOR_MENU_HL 0x45457A   /* menu highlight */
#define COLOR_MENU_FG 0xCDD6F4   /* menu text */
#define COLOR_MENU_HD 0x89B4FA   /* menu header/category */

/* ── Menu item storage ── */
#define MAX_MENU_ITEMS 40
#define MENU_W         200
#define MENU_ITEM_H    GLYPH_H
#define MENU_PAD       4

struct MenuItem {
    char label[24];     /* display name */
    char path[64];      /* full path for exec */
    int  is_header;     /* 1 = category header, 0 = launchable */
};

static struct MenuItem menu_items[MAX_MENU_ITEMS];
static int menu_count = 0;
static int menu_open = 0;
static long menu_h = 0;

static long bar_w, bar_y;
static long cur_wid = -1;   /* the single window we own */
static long bar_off = 0;    /* y-offset of bar within window (0 or menu_h) */

static char time_buf[16] = "01/01/00 00:00";
static unsigned char last_minute = 0xFF;
static unsigned char last_day = 0xFF;
static struct WinListEntry prev_wins[MAX_WIN];
static long prev_win_count = -1;
static int prev_left = 0;  /* previous left button state for edge detect */

/* ── String helpers ── */
static int str_eq(const char *a, const char *b) {
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return *a == *b;
}

static int my_strlen(const char *s) {
    int n = 0; while (s[n]) n++; return n;
}

static void my_strcpy(char *dst, const char *src) {
    while (*src) *dst++ = *src++;
    *dst = '\0';
}

static int ends_with_elf(const char *s) {
    int len = my_strlen(s);
    if (len < 4) return 0;
    const char *e = s + len - 4;
    return (e[0] == '.' &&
            (e[1] == 'E' || e[1] == 'e') &&
            (e[2] == 'L' || e[2] == 'l') &&
            (e[3] == 'F' || e[3] == 'f'));
}

/* Strip .ELF suffix and copy display name */
static void make_label(char *dst, int max, const char *name) {
    int len = my_strlen(name);
    int copy = len;
    if (ends_with_elf(name)) copy -= 4;
    if (copy > max - 1) copy = max - 1;
    for (int i = 0; i < copy; i++) dst[i] = name[i];
    dst[copy] = '\0';
}

/* ── Scan disk for programs ── */
static void add_header(const char *text) {
    if (menu_count >= MAX_MENU_ITEMS) return;
    my_strcpy(menu_items[menu_count].label, text);
    menu_items[menu_count].path[0] = '\0';
    menu_items[menu_count].is_header = 1;
    menu_count++;
}

static void add_item(const char *label, const char *path) {
    if (menu_count >= MAX_MENU_ITEMS) return;
    my_strcpy(menu_items[menu_count].label, label);
    my_strcpy(menu_items[menu_count].path, path);
    menu_items[menu_count].is_header = 0;
    menu_count++;
}

/* Scan a directory for .ELF files and add them to menu_items.
   If header is non-NULL and items are found, the header is added first. */
static int scan_dir(const char *dir, const char *header, const char *prefix) {
    char name[64];
    unsigned int size;
    unsigned char type;
    int found = 0;

    for (int idx = 0; ; idx++) {
        long r = sys_readdir_ex(idx, name, &size, dir, &type);
        if (!r) break;
        if (type == 1) continue;
        if (!ends_with_elf(name)) continue;
        if (str_eq(name, "TASKBAR.ELF")) continue;
        if (!found && header) { add_header(header); found = 1; }
        found = 1;
        char label[24];
        make_label(label, 24, name);
        char path[64];
        int pp = 0;
        for (int i = 0; prefix[i] && pp < 50; i++) path[pp++] = prefix[i];
        for (int i = 0; name[i] && pp < 62; i++) path[pp++] = name[i];
        path[pp] = '\0';
        add_item(label, path);
    }
    return found;
}

static void scan_programs(void) {
    menu_count = 0;

    /* ── Programs (BIN/) ── */
    scan_dir("BIN", "Programs", "BIN/");

    /* ── Games (GAMES/subdir/) ── */
    int games_header_added = 0;
    char gname[64];
    unsigned int gsize;
    unsigned char gtype;
    for (int gidx = 0; ; gidx++) {
        long r = sys_readdir_ex(gidx, gname, &gsize, "GAMES", &gtype);
        if (!r) break;
        if (gtype != 1) continue;  /* only subdirs */
        char subdir[64] = "GAMES/";
        int sd = 6;
        for (int i = 0; gname[i] && sd < 50; i++) subdir[sd++] = gname[i];
        subdir[sd] = '\0';
        char prefix[64];
        int pp = 0;
        for (int i = 0; subdir[i] && pp < 50; i++) prefix[pp++] = subdir[i];
        prefix[pp++] = '/';
        prefix[pp] = '\0';
        /* Add "Games" header only once, before the first game found */
        const char *hdr = games_header_added ? (const char *)0 : "Games";
        if (scan_dir(subdir, hdr, prefix))
            games_header_added = 1;
    }
}

/* ── Window management ── */

/* Pre-compute max menu height so we can allocate once at startup. */
static long max_menu_h = 0;

static void resize_to_bar(void) {
    sys_resize_window(0, bar_y, bar_w, BAR_H);
}

static void resize_to_menu(long mh) {
    long win_h = mh + BAR_H;
    long win_y = bar_y - mh;
    if (win_y < 0) win_y = 0;
    /* Only MENU_W wide — menu column + start button; bar hidden while menu is open */
    sys_resize_window(0, win_y, MENU_W, win_h);
}

/* ── Drawing ── */
/* All bar drawing uses bar_off as y-offset within the window */

static void draw_separator(long x) {
    sys_fill_rect(x, bar_off + 2, 1, BAR_H - 4, COLOR_SEP);
}

static void draw_start_button(int active) {
    long color = active ? COLOR_START_H : COLOR_START;
    sys_fill_rect(0, bar_off, START_W, BAR_H, color);
    sys_draw_text(4, bar_off, START_TEXT, COLOR_FG, color);
}

static void draw_bar_bg(void) {
    sys_fill_rect(0, bar_off, bar_w, BAR_H, COLOR_BG);
    draw_start_button(menu_open ? 1 : 0);
    draw_separator(bar_w - RIGHT_W - 2);
    /* Force clock and window list redraw */
    last_minute = 0xFF;
    last_day = 0xFF;
    prev_win_count = -1;
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
    sys_draw_text(clock_x, bar_off, time_buf, COLOR_FG, COLOR_BG);
}

static void draw_window_list(void) {
    struct WinListEntry wins[MAX_WIN];
    long count = sys_list_windows(wins, MAX_WIN);

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

    prev_win_count = count;
    for (long i = 0; i < count; i++)
        prev_wins[i] = wins[i];

    /* Clear tab area */
    long tabs_end = bar_w - RIGHT_W - 4;
    long tabs_w = tabs_end - TABS_X;
    if (tabs_w > 0)
        sys_fill_rect(TABS_X, bar_off, tabs_w, BAR_H, COLOR_BG);

    if (count == 0) return;

    draw_separator(TABS_X - LOGO_PAD / 2);

    long x = TABS_X;
    long max_tab_w = tabs_w / (count > 0 ? count : 1);
    if (max_tab_w > 160) max_tab_w = 160;

    for (long i = 0; i < count; i++) {
        long fg = (wins[i].flags & 1) ? COLOR_FOCUSED : COLOR_NORMAL;
        const char *title = wins[i].task_name[0] ? wins[i].task_name
                          : wins[i].title[0]     ? wins[i].title
                          : "???";
        char trunc[20];
        long max_chars = (max_tab_w - 8) / GLYPH_W;
        if (max_chars > 19) max_chars = 19;
        if (max_chars < 3) max_chars = 3;
        long j = 0;
        while (j < max_chars && title[j]) { trunc[j] = title[j]; j++; }
        trunc[j] = '\0';

        sys_draw_text(x, bar_off, trunc, fg, COLOR_BG);
        x += (j + 1) * GLYPH_W;
        if (i + 1 < count)
            draw_separator(x - GLYPH_W / 2);
    }
}

/* ── Menu popup ── */
static void draw_menu_items(void) {
    /* Draw menu background in the top portion of the tall window */
    sys_fill_rect(0, 0, MENU_W, menu_h, COLOR_MENU_BG);
    /* Border */
    sys_fill_rect(0, 0, MENU_W, 1, COLOR_SEP);
    sys_fill_rect(MENU_W - 1, 0, 1, menu_h, COLOR_SEP);

    /* Draw items */
    for (int i = 0; i < menu_count; i++) {
        int iy = MENU_PAD + i * MENU_ITEM_H;
        if (menu_items[i].is_header) {
            sys_draw_text(MENU_PAD, iy, menu_items[i].label,
                          COLOR_MENU_HD, COLOR_MENU_BG);
            /* Underline */
            sys_fill_rect(MENU_PAD, iy + GLYPH_H - 1,
                          my_strlen(menu_items[i].label) * GLYPH_W, 1,
                          COLOR_SEP);
        } else {
            sys_draw_text(MENU_PAD + GLYPH_W, iy, menu_items[i].label,
                          COLOR_MENU_FG, COLOR_MENU_BG);
        }
    }
}

static void open_menu(void) {
    if (menu_open) return;
    scan_programs();
    if (menu_count == 0) return;

    menu_h = menu_count * MENU_ITEM_H + 2 * MENU_PAD;

    /* Resize window to cover menu + bar (no realloc if fits in pre-allocated buf) */
    resize_to_menu(menu_h);

    menu_open = 1;
    bar_off = menu_h;  /* bar draws below menu area */

    /* Clear entire window area first (prevents stale pixels from previous layout) */
    sys_fill_rect(0, 0, MENU_W, menu_h + BAR_H, 0x000000);

    /* Draw menu items in the upper portion */
    draw_menu_items();

    /* Redraw full bar in the lower portion */
    draw_bar_bg();
}

static void close_menu(void) {
    if (!menu_open) return;
    menu_open = 0;

    /* Resize back to bar-only (no realloc — buffer was allocated at max size) */
    resize_to_bar();
    bar_off = 0;

    /* Redraw bar */
    draw_bar_bg();
}

static void launch_item(int idx) {
    if (idx < 0 || idx >= menu_count) return;
    if (menu_items[idx].is_header) return;
    if (menu_items[idx].path[0] == '\0') return;

    close_menu();

    /* Fire-and-forget: fork + exec, no waitpid */
    long child = sys_fork();
    if (child == 0) {
        const char *path = menu_items[idx].path;
        const char *argv[2];
        argv[0] = path;
        argv[1] = (const char *)0;
        sys_exec(path, 1, argv);
        sys_exit(1);  /* exec failed */
    }
    /* Parent continues — don't wait */
}

/* Check if absolute screen coords (mx, my) hit a menu item.
   Returns item index or -1. */
static int menu_hit(int mx, int my) {
    if (!menu_open) return -1;
    /* Menu occupies screen (0, bar_y - menu_h) to (MENU_W, bar_y) */
    long menu_screen_y = bar_y - menu_h;
    int local_x = mx;
    int local_y = my - (int)menu_screen_y;
    if (local_x < 0 || local_x >= MENU_W) return -1;
    if (local_y < MENU_PAD || local_y >= (int)menu_h - MENU_PAD) return -1;
    int idx = (local_y - MENU_PAD) / MENU_ITEM_H;
    if (idx < 0 || idx >= menu_count) return -1;
    return idx;
}

/* ── Main ── */
int main(void) {
    long fb_w = sys_fb_width();
    long fb_h = sys_fb_height();
    bar_w = fb_w;
    bar_y = fb_h - BAR_H;

    /* Pre-scan to determine max menu height, then allocate window at max size.
       This avoids repeated 1.8MB alloc/free on every menu open/close. */
    scan_programs();
    max_menu_h = menu_count * MENU_ITEM_H + 2 * MENU_PAD;
    {
        /* Pre-allocate at the max dimensions across both layouts:
           - Bar mode: bar_w × BAR_H (wide but short)
           - Menu mode: MENU_W × (menu_h + BAR_H) (narrow but tall)
           Create at bar_w × (menu_h + BAR_H) to cover both strides. */
        long max_win_h = max_menu_h + BAR_H;
        long max_win_y = bar_y - max_menu_h;
        if (max_win_y < 0) max_win_y = 0;
        cur_wid = sys_create_window_chromeless(0, max_win_y, fb_w, max_win_h);
    }
    /* Immediately resize down to just the bar (reuses the large buffer) */
    resize_to_bar();
    bar_off = 0;

    /* Initial draw */
    draw_bar_bg();

    for (;;) {
        draw_clock();
        draw_window_list();

        /* Poll absolute mouse position + button state (not event queue,
           which can be drained by other tasks like games). */
        long mpos = sys_get_mouse_pos();
        int mx = (int)(short)(mpos & 0xFFFF);
        int my = (int)(short)((mpos >> 16) & 0xFFFF);
        int cur_left = (int)((mpos >> 32) & 1);
        int clicked = (cur_left && !prev_left);
        prev_left = cur_left;

        /* Windows/Super key toggles the start menu */
        if (sys_poll_start_key()) {
            if (menu_open)
                close_menu();
            else
                open_menu();
            /* Don't process mouse clicks in the same frame —
               a stale click could immediately close the menu. */
            goto next_frame;
        }

        if (clicked) {

            if (menu_open) {
                int hit = menu_hit(mx, my);
                if (hit >= 0 && !menu_items[hit].is_header) {
                    launch_item(hit);
                } else {
                    close_menu();
                }
            } else {
                /* Check if click is on the Start button */
                if (mx >= 0 && mx < START_W &&
                    my >= (int)bar_y && my < (int)bar_y + BAR_H) {
                    open_menu();
                }
            }
        }

next_frame:
        sys_sleep_ms(50);
    }
}
