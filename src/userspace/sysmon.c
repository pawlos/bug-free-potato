#include "libc/syscall.h"

#define WIN_W       280
#define WIN_H       240
#define GLYPH_W     8
#define GLYPH_H     16
#define MAX_TASKS   32
#define PAD         4

#define COLOR_BG      0x1E1E2E
#define COLOR_FG      0xCDD6F4
#define COLOR_HEADER  0x89B4FA
#define COLOR_DIM     0x585B70
#define COLOR_GREEN   0xA6E3A1
#define COLOR_YELLOW  0xF9E2AF
#define COLOR_RED     0xF38BA8
#define COLOR_SEP     0x45475A

static long wid;

/* ── Previous-sample state for delta CPU% ── */
static unsigned long long prev_ticks[MAX_TASKS];  /* per-task ticks at last sample */
static unsigned int       prev_ids[MAX_TASKS];    /* task IDs at last sample */
static long               prev_count = 0;
static unsigned long long prev_global = 0;        /* global tick at last sample */

static int my_strlen(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

static void my_strcpy(char *dst, const char *src) {
    while (*src) *dst++ = *src++;
    *dst = '\0';
}

static const char* basename(const char *path) {
    const char *last = path;
    for (const char *p = path; *p; p++)
        if (*p == '/') last = p + 1;
    return last;
}

static void strip_elf(char *s) {
    int len = my_strlen(s);
    if (len >= 4) {
        char *suf = s + len - 4;
        if ((suf[0] == '.') &&
            (suf[1] == 'E' || suf[1] == 'e') &&
            (suf[2] == 'L' || suf[2] == 'l') &&
            (suf[3] == 'F' || suf[3] == 'f'))
            suf[0] = '\0';
    }
}

static void fmt_uint(char *buf, unsigned long long val) {
    char tmp[20];
    int i = 0;
    if (val == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    while (val > 0) {
        tmp[i++] = '0' + (val % 10);
        val /= 10;
    }
    int j = 0;
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = '\0';
}

static void fmt_size(char *buf, unsigned long long bytes) {
    if (bytes >= 1048576) {
        unsigned long long mb = bytes / 1048576;
        unsigned long long frac = (bytes % 1048576) * 10 / 1048576;
        fmt_uint(buf, mb);
        int len = my_strlen(buf);
        buf[len] = '.';
        buf[len+1] = '0' + (char)frac;
        buf[len+2] = ' ';
        buf[len+3] = 'M';
        buf[len+4] = 'B';
        buf[len+5] = '\0';
    } else if (bytes >= 1024) {
        unsigned long long kb = bytes / 1024;
        fmt_uint(buf, kb);
        int len = my_strlen(buf);
        buf[len] = ' ';
        buf[len+1] = 'K';
        buf[len+2] = 'B';
        buf[len+3] = '\0';
    } else {
        fmt_uint(buf, bytes);
        int len = my_strlen(buf);
        buf[len] = ' ';
        buf[len+1] = 'B';
        buf[len+2] = '\0';
    }
}

/* Look up previous ticks for a task ID; returns 0 if not found (new task). */
static unsigned long long find_prev_ticks(unsigned int id) {
    for (long i = 0; i < prev_count; i++)
        if (prev_ids[i] == id) return prev_ticks[i];
    return 0;
}

/* Format CPU% — e.g. " 42%" or "  0%" right-aligned in 4 chars */
static void fmt_cpu(char *buf, unsigned long long dtask, unsigned long long dglobal) {
    unsigned long long pct = 0;
    if (dglobal > 0) pct = dtask * 100 / dglobal;
    if (pct > 100) pct = 100;
    /* Right-align in 4 chars: "XXX%" or " XX%" or "  X%" */
    int i = 0;
    if (pct >= 100) { buf[i++] = '1'; buf[i++] = '0'; buf[i++] = '0'; }
    else if (pct >= 10) { buf[i++] = ' '; buf[i++] = '0' + (char)(pct/10); buf[i++] = '0' + (char)(pct%10); }
    else { buf[i++] = ' '; buf[i++] = ' '; buf[i++] = '0' + (char)pct; }
    buf[i++] = '%';
    buf[i] = '\0';
}

static unsigned int cpu_color(unsigned long long dtask, unsigned long long dglobal) {
    unsigned long long pct = dglobal > 0 ? dtask * 100 / dglobal : 0;
    if (pct >= 50) return COLOR_RED;
    if (pct >= 10) return COLOR_YELLOW;
    return COLOR_GREEN;
}

static void draw_hsep(int y) {
    sys_fill_rect(0, y, WIN_W, 1, COLOR_SEP);
}

static void draw_text_right(int x, int y, const char *s, int field_w, unsigned int fg) {
    int text_w = my_strlen(s) * GLYPH_W;
    int offset = field_w - text_w;
    if (offset < 0) offset = 0;
    sys_draw_text(x + offset, y, s, fg, COLOR_BG);
}

static void draw_frame(void) {
    sys_fill_rect(0, 0, WIN_W, WIN_H, COLOR_BG);

    int y = PAD;

    /* ── Header: Uptime ── */
    unsigned long long micros = sys_get_micros();
    unsigned long long secs = micros / 1000000;
    unsigned long long mins = secs / 60;
    unsigned long long hrs  = mins / 60;
    char uptime[32] = "Uptime: ";
    int pos = 8;
    char tmp[12];
    fmt_uint(tmp, hrs);
    for (int i = 0; tmp[i]; i++) uptime[pos++] = tmp[i];
    uptime[pos++] = ':';
    unsigned long long mn = mins % 60;
    uptime[pos++] = '0' + (char)(mn / 10);
    uptime[pos++] = '0' + (char)(mn % 10);
    uptime[pos++] = ':';
    unsigned long long sc = secs % 60;
    uptime[pos++] = '0' + (char)(sc / 10);
    uptime[pos++] = '0' + (char)(sc % 10);
    uptime[pos] = '\0';
    sys_draw_text(PAD, y, uptime, COLOR_HEADER, COLOR_BG);
    y += GLYPH_H;

    /* ── Memory ── */
    unsigned long long free_mem = (unsigned long long)sys_mem_free();
    char mem_line[48] = "Free RAM: ";
    char size_buf[20];
    fmt_size(size_buf, free_mem);
    int ml = 10;
    for (int i = 0; size_buf[i]; i++) mem_line[ml++] = size_buf[i];
    mem_line[ml] = '\0';
    sys_draw_text(PAD, y, mem_line, COLOR_FG, COLOR_BG);
    y += GLYPH_H;

    draw_hsep(y + 2);
    y += PAD + 2;

    /* ── Task list header ── */
    /* Columns: PID(3) NAME(14) CPU%(5) TICKS(rest) */
    sys_draw_text(PAD,              y, "PID", COLOR_HEADER, COLOR_BG);
    sys_draw_text(PAD + 4*GLYPH_W, y, "NAME",  COLOR_HEADER, COLOR_BG);
    sys_draw_text(PAD + 18*GLYPH_W, y, "CPU%", COLOR_HEADER, COLOR_BG);
    sys_draw_text(PAD + 24*GLYPH_W, y, "TICKS", COLOR_HEADER, COLOR_BG);
    y += GLYPH_H;

    draw_hsep(y);
    y += 2;

    /* ── Snapshot current tasks ── */
    struct TaskListEntry tasks[MAX_TASKS];
    long count = sys_list_tasks(tasks, MAX_TASKS);
    unsigned long long global_now = (unsigned long long)sys_get_ticks();
    unsigned long long dglobal = global_now - prev_global;

    for (long i = 0; i < count && y + GLYPH_H <= WIN_H; i++) {
        /* PID */
        char pid_str[8];
        fmt_uint(pid_str, tasks[i].id);
        draw_text_right(PAD, y, pid_str, 3 * GLYPH_W, COLOR_FG);

        /* Name */
        char name[16];
        if (tasks[i].name[0]) {
            my_strcpy(name, basename(tasks[i].name));
            strip_elf(name);
        } else {
            my_strcpy(name, "<kernel>");
        }
        if (my_strlen(name) > 13) name[13] = '\0';
        sys_draw_text(PAD + 4*GLYPH_W, y, name, COLOR_FG, COLOR_BG);

        /* CPU% (delta since last sample) */
        unsigned long long pticks = find_prev_ticks(tasks[i].id);
        unsigned long long dtask = tasks[i].ticks - pticks;
        char cpu_str[8];
        fmt_cpu(cpu_str, dtask, dglobal);
        sys_draw_text(PAD + 18*GLYPH_W, y, cpu_str,
                      cpu_color(dtask, dglobal), COLOR_BG);

        /* Ticks (cumulative) */
        char tick_str[16];
        fmt_uint(tick_str, tasks[i].ticks);
        draw_text_right(PAD + 24*GLYPH_W, y, tick_str, 9 * GLYPH_W, COLOR_FG);

        y += GLYPH_H;
    }

    /* Save current snapshot for next delta */
    prev_global = global_now;
    prev_count = count;
    for (long i = 0; i < count; i++) {
        prev_ids[i]   = tasks[i].id;
        prev_ticks[i] = tasks[i].ticks;
    }

    /* Task count footer */
    y = WIN_H - GLYPH_H - PAD;
    draw_hsep(y - 2);
    char footer[32] = "Tasks: ";
    char cnt[8];
    fmt_uint(cnt, (unsigned long long)count);
    int fl = 7;
    for (int i = 0; cnt[i]; i++) footer[fl++] = cnt[i];
    footer[fl] = '\0';
    sys_draw_text(PAD, y, footer, COLOR_DIM, COLOR_BG);
}

int main(void) {
    wid = sys_create_window(60, 40, WIN_W, WIN_H);
    if (wid < 0) sys_exit(1);
    sys_set_window_title(wid, "System Monitor");

    /* Take initial snapshot so first frame has a baseline */
    prev_global = (unsigned long long)sys_get_ticks();
    {
        struct TaskListEntry tasks[MAX_TASKS];
        prev_count = sys_list_tasks(tasks, MAX_TASKS);
        for (long i = 0; i < prev_count; i++) {
            prev_ids[i]   = tasks[i].id;
            prev_ticks[i] = tasks[i].ticks;
        }
    }

    for (;;) {
        sys_sleep_ms(1000);
        draw_frame();

        long ev = sys_get_window_event(wid);
        if (ev != 0) {
            int pressed = (ev & 0x100) != 0;
            int scancode = (int)(ev & 0xFF);
            if (pressed && scancode == 0x01) {
                sys_destroy_window(wid);
                sys_exit(0);
            }
        }
    }
}
