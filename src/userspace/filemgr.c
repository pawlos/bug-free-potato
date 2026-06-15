/*
 * filemgr.c — dual-pane (Midnight-Commander-style) file manager for potat OS.
 *
 * Single-file freestanding userspace app. Two directory panes, switched with
 * Tab; navigation, launch (fork+exec), copy (F5), move/rename (F6),
 * mkdir (F7), delete (F8). Input arrives as PS/2 set-1 scancodes via
 * sys_get_window_event; UI drawn with sys_fill_rect + sys_draw_text.
 */
#include "libc/syscall.h"

/* ── Layout / colors ──────────────────────────────────────────────────── */
#define WIN_W       640
#define WIN_H       448
#define GLYPH_W     8
#define GLYPH_H     16
#define PAD         4
#define PANE_W      (WIN_W / 2)              /* 320 */
#define HEADER_Y    PAD                      /* path line */
#define LIST_Y      (PAD + GLYPH_H + 4)      /* first entry row */
#define FOOTER_Y    (WIN_H - GLYPH_H - PAD)  /* status/hints line */
#define VISIBLE_ROWS ((FOOTER_Y - LIST_Y) / GLYPH_H)
#define NAME_COLS   30                       /* chars of filename shown */
#define MAX_ENTRIES 256
#define MAX_NAME    64
#define MAX_PATH    128

#define COLOR_BG      0x1E1E2E
#define COLOR_FG      0xCDD6F4
#define COLOR_DIR     0x89B4FA
#define COLOR_DIM     0x585B70
#define COLOR_SEL_BG  0x45475A   /* selected row, active pane */
#define COLOR_HDR_ACT 0xA6E3A1   /* active pane header */
#define COLOR_HDR_INA 0x585B70   /* inactive pane header */
#define COLOR_SEP     0x45475A

/* PS/2 set-1 scancode → ASCII (index 0x00..0x39). 0 = no printable char. */
static const char sc_ascii[0x3A] = {
/*00*/ 0,   27,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b','\t',
/*10*/ 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0,  'a', 's',
/*20*/ 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'','`', 0,   '\\','z', 'x', 'c', 'v',
/*30*/ 'b', 'n', 'm', ',', '.', '/', 0,   '*', 0,   ' '
};
static const char sc_ascii_shift[0x3A] = {
/*00*/ 0,   27,  '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b','\t',
/*10*/ 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 0,  'A', 'S',
/*20*/ 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0,   '|', 'Z', 'X', 'C', 'V',
/*30*/ 'B', 'N', 'M', '<', '>', '?', 0,   '*', 0,   ' '
};

/* ── State ────────────────────────────────────────────────────────────── */
struct Entry { char name[MAX_NAME]; unsigned int size; int is_dir; };
struct Pane  { char path[MAX_PATH]; struct Entry ents[MAX_ENTRIES];
               int count; int sel; int scroll; };

static struct Pane panes[2];
static int  active = 0;
static long wid;
static int  running = 1;
static char status[80] = "Tab:switch Enter:open F5:copy F6:move F7:mkdir F8:del Esc:quit";

/* ── String helpers ───────────────────────────────────────────────────── */
static int my_strlen(const char *s) { int n = 0; while (s[n]) n++; return n; }
static void my_strcpy(char *d, const char *s) { while ((*d++ = *s++)); }
static int my_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}
static void set_status(const char *s) {
    int i = 0; while (s[i] && i < (int)sizeof(status) - 1) { status[i] = s[i]; i++; }
    status[i] = '\0';
}

/* forward decls (filled in later tasks) */
static void list_dir(struct Pane *p);
static void draw(void);
static void handle_key(int sc, int pressed);

int main(void) {
    wid = sys_create_window(40, 30, WIN_W, WIN_H);
    if (wid < 0) return 1;
    sys_set_window_title(wid, "File Manager");

    my_strcpy(panes[0].path, "/");
    my_strcpy(panes[1].path, "/");
    list_dir(&panes[0]);
    list_dir(&panes[1]);
    draw();

    while (running) {
        long ev;
        while ((ev = sys_get_window_event(wid)) != 0) {
            int pressed = (ev & 0x100) != 0;
            int sc      = (int)(ev & 0xFF);
            handle_key(sc, pressed);
        }
        sys_yield();
    }

    sys_destroy_window(wid);
    return 0;
}

/* ── Directory listing ────────────────────────────────────────────────── */
/* Insert entry keeping dirs-first then alphabetical order. */
static void insert_sorted(struct Pane *p, const char *name, unsigned int size, int is_dir) {
    if (p->count >= MAX_ENTRIES) return;
    int i = p->count;
    while (i > 0) {
        struct Entry *prev = &p->ents[i - 1];
        if (prev->is_dir && !is_dir) break;                  /* dirs stay above files */
        if (prev->is_dir == is_dir && my_strcmp(prev->name, name) <= 0) break;
        p->ents[i] = *prev;
        i--;
    }
    struct Entry *e = &p->ents[i];
    int n = 0; while (name[n] && n < MAX_NAME - 1) { e->name[n] = name[n]; n++; }
    e->name[n] = '\0';
    e->size = size; e->is_dir = is_dir;
    p->count++;
}

static int at_root(const struct Pane *p) {
    return p->path[0] == '/' && p->path[1] == '\0';
}

static void list_dir(struct Pane *p) {
    p->count = 0; p->sel = 0; p->scroll = 0;

    /* synthetic parent entry in any non-root dir */
    if (!at_root(p)) insert_sorted(p, "..", 0, 1);

    char name[MAX_NAME];
    unsigned int size;
    unsigned char type;
    for (int i = 0; ; i++) {
        long r = sys_readdir_ex(i, name, &size, p->path, &type);
        if (r == 0) break;
        if (name[0] == '\0') continue;
        if (name[0] == '.' && name[1] == '\0') continue;          /* skip "." */
        if (name[0] == '.' && name[1] == '.' && name[2] == '\0') continue; /* we add our own ".." */
        insert_sorted(p, name, size, type == 1);
    }

    if (p->sel >= p->count) p->sel = p->count > 0 ? p->count - 1 : 0;
    if (p->scroll > p->sel) p->scroll = p->sel;
}

/* ── Rendering ────────────────────────────────────────────────────────── */
/* Append unsigned decimal of v to out (out must have room); returns end ptr. */
static char *put_uint(char *out, unsigned int v) {
    char r[10]; int rn = 0;
    if (v == 0) r[rn++] = '0';
    else while (v) { r[rn++] = (char)('0' + v % 10); v /= 10; }
    while (rn) *out++ = r[--rn];
    return out;
}

/* Format a byte count into a short string: "1.2M", "987K", "512". */
static void fmt_size(char *out, unsigned int sz) {
    if (sz >= 1024u * 1024u) {
        unsigned int m = sz / (1024u * 1024u);
        unsigned int tenths = (unsigned int)(((sz % (1024u * 1024u)) * 10u) / (1024u * 1024u));
        out = put_uint(out, m);
        *out++ = '.'; *out++ = (char)('0' + tenths); *out++ = 'M'; *out = '\0';
    } else if (sz >= 1024u) {
        out = put_uint(out, sz / 1024u);
        *out++ = 'K'; *out = '\0';
    } else {
        out = put_uint(out, sz);
        *out = '\0';
    }
}

static void draw_pane(int idx, int x0) {
    struct Pane *p = &panes[idx];
    int is_active = (idx == active);

    /* header: current path */
    char hdr[MAX_PATH + 1];
    int hn = 0; const char *s = p->path;
    while (s[hn] && hn < PANE_W / GLYPH_W - 1) { hdr[hn] = s[hn]; hn++; }
    hdr[hn] = '\0';
    sys_draw_text(x0 + PAD, HEADER_Y, hdr,
                  is_active ? COLOR_HDR_ACT : COLOR_HDR_INA, COLOR_BG);

    /* separator under header */
    sys_fill_rect(x0, LIST_Y - 2, PANE_W, 1, COLOR_SEP);

    /* entries */
    for (int row = 0; row < VISIBLE_ROWS; row++) {
        int ei = p->scroll + row;
        if (ei >= p->count) break;
        struct Entry *e = &p->ents[ei];
        int y = LIST_Y + row * GLYPH_H;

        unsigned int bg = COLOR_BG;
        if (is_active && ei == p->sel) {
            bg = COLOR_SEL_BG;
            sys_fill_rect(x0, y, PANE_W, GLYPH_H, bg);
        }

        /* name (dirs shown as [name]); truncate to NAME_COLS */
        char line[NAME_COLS + 3];
        int li = 0;
        if (e->is_dir) line[li++] = '[';
        int n = 0;
        while (e->name[n] && li < NAME_COLS) line[li++] = e->name[n++];
        if (e->is_dir && li < NAME_COLS + 1) line[li++] = ']';
        line[li] = '\0';
        sys_draw_text(x0 + PAD, y, line, e->is_dir ? COLOR_DIR : COLOR_FG, bg);

        /* size, right-aligned, for files only */
        if (!e->is_dir) {
            char sz[12]; fmt_size(sz, e->size);
            int sl = my_strlen(sz);
            int sx = x0 + PANE_W - PAD - sl * GLYPH_W;
            sys_draw_text(sx, y, sz, COLOR_DIM, bg);
        }
    }

    if (p->count == 0)
        sys_draw_text(x0 + PAD, LIST_Y, "(empty)", COLOR_DIM, COLOR_BG);
}

static void draw(void) {
    sys_fill_rect(0, 0, WIN_W, WIN_H, COLOR_BG);
    draw_pane(0, 0);
    draw_pane(1, PANE_W);
    /* vertical divider */
    sys_fill_rect(PANE_W, 0, 1, FOOTER_Y, COLOR_SEP);
    /* footer / status */
    sys_fill_rect(0, FOOTER_Y - 2, WIN_W, 1, COLOR_SEP);
    sys_draw_text(PAD, FOOTER_Y, status, COLOR_DIM, COLOR_BG);
}
/* ── Path helpers ─────────────────────────────────────────────────────── */
/* out = dir + "/" + name, avoiding "//" at root. */
static void join(char *out, const char *dir, const char *name) {
    int i = 0; while (dir[i] && i < MAX_PATH - 1) { out[i] = dir[i]; i++; }
    if (!(i == 1 && out[0] == '/')) { if (i < MAX_PATH - 1) out[i++] = '/'; }
    int j = 0; while (name[j] && i < MAX_PATH - 1) out[i++] = name[j++];
    out[i] = '\0';
}

/* Strip the last component of p->path; clamp at "/". */
static void go_parent(struct Pane *p) {
    int len = my_strlen(p->path);
    if (len <= 1) return;                 /* already root */
    int i = len - 1;
    while (i > 0 && p->path[i] == '/') i--;     /* trailing slash */
    while (i > 0 && p->path[i] != '/') i--;     /* find prev slash */
    if (i == 0) { p->path[0] = '/'; p->path[1] = '\0'; }
    else p->path[i] = '\0';
}

/* ── Launching ────────────────────────────────────────────────────────── */
/* case-insensitive check that `name` ends with `.ext` (ext lowercase, no dot). */
static int has_ext(const char *name, const char *ext) {
    int nl = my_strlen(name), el = my_strlen(ext);
    if (nl < el + 1) return 0;
    if (name[nl - el - 1] != '.') return 0;
    for (int i = 0; i < el; i++) {
        char c = name[nl - el + i];
        if (c >= 'A' && c <= 'Z') c += 32;
        if (c != ext[i]) return 0;
    }
    return 1;
}

static int is_image(const char *name) {
    return has_ext(name, "bmp") || has_ext(name, "png") || has_ext(name, "jpg") ||
           has_ext(name, "tga") || has_ext(name, "gif");
}

/* fork+exec; manager keeps running (no waitpid — children unreaped until exit). */
static void launch(const char *exe, const char *arg /* may be NULL */) {
    const char *argv[3];
    int argc = 1;
    argv[0] = exe;
    if (arg) { argv[1] = arg; argc = 2; }
    argv[argc] = 0;
    long child = sys_fork();
    if (child == 0) {
        sys_exec(exe, argc, (const char* const*)argv);
        sys_exit(1);   /* exec failed */
    }
    /* parent: continue without blocking */
}

/* ── Modal text prompt ────────────────────────────────────────────────── */
/* Draw a single-line modal prompt over the footer; capture text until Enter
   (returns 1, result in out) or Esc (returns 0). Blocks the event loop. */
static int prompt(const char *label, char *out, int outsz) {
    int len = 0; out[0] = '\0';
    int shift = 0;
    for (;;) {
        /* render prompt bar */
        sys_fill_rect(0, FOOTER_Y - 2, WIN_W, GLYPH_H + 2, COLOR_BG);
        sys_draw_text(PAD, FOOTER_Y, label, COLOR_HDR_ACT, COLOR_BG);
        int lx = PAD + (my_strlen(label) + 1) * GLYPH_W;
        sys_draw_text(lx, FOOTER_Y, out, COLOR_FG, COLOR_BG);
        /* cursor */
        sys_fill_rect(lx + len * GLYPH_W, FOOTER_Y, GLYPH_W, GLYPH_H, COLOR_SEL_BG);

        long ev;
        while ((ev = sys_get_window_event(wid)) == 0) sys_yield();
        int pressed = (ev & 0x100) != 0;
        int sc      = (int)(ev & 0xFF);
        if (sc == 0x2A || sc == 0x36) { shift = pressed; continue; }
        if (!pressed) continue;
        if (sc == 0x01) return 0;                 /* Esc cancels */
        if (sc == 0x1C) { out[len] = '\0'; return len > 0; }  /* Enter */
        if (sc == 0x0E) { if (len > 0) out[--len] = '\0'; continue; } /* Backspace */
        if (sc < 0x3A) {
            char c = shift ? sc_ascii_shift[sc] : sc_ascii[sc];
            if (c >= ' ' && c < 127 && len < outsz - 1) { out[len++] = c; out[len] = '\0'; }
        }
    }
}

/* ── Input handling ───────────────────────────────────────────────────── */
static void enter_selected(struct Pane *p) {
    if (p->count == 0) return;
    struct Entry *e = &p->ents[p->sel];
    if (e->is_dir) {
        if (e->name[0] == '.' && e->name[1] == '.' && e->name[2] == '\0') {
            go_parent(p);
        } else {
            char np[MAX_PATH]; join(np, p->path, e->name);
            my_strcpy(p->path, np);
        }
        list_dir(p);
        return;
    }
    /* file: launch by type */
    {
        char full[MAX_PATH]; join(full, p->path, e->name);
        if (has_ext(e->name, "elf")) {
            launch(full, 0);
            set_status("launched ELF");
        } else if (is_image(e->name)) {
            launch("BIN/IMGVIEW.ELF", full);
            set_status("opened in image viewer");
        } else if (has_ext(e->name, "mpg")) {
            launch("BIN/PLAYER.ELF", full);
            set_status("playing in video player");
        } else {
            set_status("no handler for this file type");
        }
    }
}

/* ── Operations: mkdir / delete ───────────────────────────────────────── */
static void do_mkdir(struct Pane *p) {
    char name[MAX_NAME];
    if (!prompt("mkdir:", name, sizeof(name))) { set_status("mkdir cancelled"); return; }
    char full[MAX_PATH]; join(full, p->path, name);
    if (sys_mkdir(full) == 0) { set_status("directory created"); list_dir(p); }
    else set_status("mkdir failed");
}

static void do_delete(struct Pane *p) {
    if (p->count == 0) return;
    struct Entry *e = &p->ents[p->sel];
    if (e->name[0] == '.' && e->name[1] == '.' && e->name[2] == '\0') {
        set_status("cannot delete '..'"); return;
    }
    char label[MAX_NAME + 16];
    my_strcpy(label, "delete ");
    { int i = 7, j = 0; while (e->name[j] && i < (int)sizeof(label) - 6) label[i++] = e->name[j++];
      my_strcpy(&label[i], "? y/n"); }
    char ans[4];
    if (!prompt(label, ans, sizeof(ans))) { set_status("delete cancelled"); return; }
    if (ans[0] != 'y' && ans[0] != 'Y') { set_status("delete cancelled"); return; }
    char full[MAX_PATH]; join(full, p->path, e->name);
    if (sys_remove(full) == 0) {
        set_status("deleted");
        if (p->sel >= p->count - 1 && p->sel > 0) p->sel--;
        list_dir(p);
    } else set_status("delete failed");
}

/* ── Operations: copy / move ──────────────────────────────────────────── */
#define COPY_BUF 8192

/* Copy a regular file src→dst. Returns 1 on success, 0 on failure. */
static int copy_file(const char *src, const char *dst) {
    int in = sys_open(src);
    if (in < 0) return 0;
    int out = sys_create(dst);
    if (out < 0) { sys_close(in); return 0; }
    static char buf[COPY_BUF];
    int ok = 1;
    for (;;) {
        long n = sys_read(in, buf, COPY_BUF);
        if (n <= 0) break;
        long w = sys_write(out, buf, (size_t)n);
        if (w != n) { ok = 0; break; }
    }
    sys_close(in); sys_close(out);
    return ok;
}

/* Copy active pane's selected file into the other pane's directory.
   If and_delete, remove the source afterwards (move/rename). */
static void do_copy(int and_delete) {
    struct Pane *p = &panes[active];
    struct Pane *q = &panes[active ^ 1];
    if (p->count == 0) return;
    struct Entry *e = &p->ents[p->sel];
    if (e->is_dir) { set_status("directories not supported (files only)"); return; }
    if (my_strcmp(p->path, q->path) == 0) { set_status("source and dest are the same dir"); return; }

    char src[MAX_PATH], dst[MAX_PATH];
    join(src, p->path, e->name);
    join(dst, q->path, e->name);

    if (!copy_file(src, dst)) { set_status(and_delete ? "move failed" : "copy failed"); return; }
    if (and_delete) {
        if (sys_remove(src) != 0) { set_status("moved but source remove failed"); }
        else set_status("moved");
        list_dir(p);
    } else {
        set_status("copied");
    }
    list_dir(q);
}

static void handle_key(int sc, int pressed) {
    if (!pressed) return;
    struct Pane *p = &panes[active];

    switch (sc) {
        case 0x01: running = 0; return;            /* Esc */
        case 0x0F: active ^= 1; break;             /* Tab */
        case 0x48:                                  /* Up */
            if (p->sel > 0) p->sel--;
            if (p->sel < p->scroll) p->scroll = p->sel;
            break;
        case 0x50:                                  /* Down */
            if (p->sel < p->count - 1) p->sel++;
            if (p->sel >= p->scroll + VISIBLE_ROWS) p->scroll = p->sel - VISIBLE_ROWS + 1;
            break;
        case 0x1C:                                  /* Enter */
            enter_selected(p);
            break;
        case 0x3F: do_copy(0); break;              /* F5 copy */
        case 0x40: do_copy(1); break;              /* F6 move */
        case 0x41: do_mkdir(p);  break;            /* F7 */
        case 0x42: do_delete(p); break;            /* F8 */
        default: return;                            /* ignore; no redraw */
    }
    draw();
}
