/*
 * snake.c — Classic Snake game for potat OS windowing subsystem.
 *
 * Controls:
 *   Arrow keys  — change direction
 *   ENTER       — start game (title screen)
 *   R           — restart (after game over)
 *   ESC         — quit
 */

#include "libc/stdio.h"
#include "libc/stdlib.h"
#include "libc/syscall.h"

/* ── Grid / window constants ──────────────────────────────────────────────── */
#define CELL_SIZE  16
#define COLS       30
#define ROWS       24
#define HDR_H      20
#define WIN_W      (COLS * CELL_SIZE)          /* 480 */
#define WIN_H      (HDR_H + ROWS * CELL_SIZE)  /* 404 */
#define MAX_LEN    (COLS * ROWS)

/* ── PS/2 set-1 scancodes ─────────────────────────────────────────────────── */
#define SC_ESC   0x01
#define SC_ENTER 0x1C
#define SC_R     0x13
#define SC_UP    0x48
#define SC_DOWN  0x50
#define SC_LEFT  0x4B
#define SC_RIGHT 0x4D

/* ── Colors (0xRRGGBB) ────────────────────────────────────────────────────── */
#define COLOR_BG      0x003300L
#define COLOR_SNAKE   0x00CC00L
#define COLOR_HEAD    0x88FF88L
#define COLOR_FOOD    0xFF2222L
#define COLOR_HDR_BG  0x001100L
#define COLOR_TEXT    0xCCCCCCL
#define COLOR_DEAD    0xFF4444L

/* ── Data structures ──────────────────────────────────────────────────────── */
typedef struct { int x, y; } Cell;

typedef enum { DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT } Dir;
typedef enum { STATE_TITLE, STATE_PLAY, STATE_DEAD } GameState;

static Cell      body[MAX_LEN];
static int       head_idx  = 0;
static int       snake_len = 0;
static Dir       dir, next_dir;
static Cell      food;
static int       score = 0;
static int       quit  = 0;
static GameState state = STATE_TITLE;
static long      wid   = -1;

/* ── draw_cell: draw one grid square (1-px inset for grid lines) ─────────── */
static void draw_cell(int gx, int gy, long color)
{
    int px = gx * CELL_SIZE + 1;
    int py = HDR_H + gy * CELL_SIZE + 1;
    sys_fill_rect(px, py, CELL_SIZE - 2, CELL_SIZE - 2, color);
}

/* ── draw_hdr: refresh the score/status bar ──────────────────────────────── */
static void draw_hdr(void)
{
    char buf[64];
    sys_fill_rect(0, 0, WIN_W, HDR_H, COLOR_HDR_BG);
    snprintf(buf, sizeof(buf), "SNAKE  Score: %d", score);
    sys_draw_text(4, 2, buf, COLOR_TEXT, COLOR_HDR_BG);
}

/* ── place_food: random cell not occupied by the snake ──────────────────── */
static void place_food(void)
{
    int ok = 0;
    while (!ok) {
        food.x = rand() % COLS;
        food.y = rand() % ROWS;
        ok = 1;
        for (int i = 0; i < snake_len; i++) {
            int idx = (head_idx + i) % MAX_LEN;
            if (body[idx].x == food.x && body[idx].y == food.y) {
                ok = 0;
                break;
            }
        }
    }
    draw_cell(food.x, food.y, COLOR_FOOD);
}

/* ── game_init: reset all state and draw initial board ───────────────────── */
static void game_init(void)
{
    score     = 0;
    head_idx  = 0;
    snake_len = 3;
    dir       = DIR_RIGHT;
    next_dir  = DIR_RIGHT;

    /* Snake starts at horizontal center, vertical center, heading right */
    int sx = COLS / 2 - 1;
    int sy = ROWS / 2;
    for (int i = 0; i < snake_len; i++) {
        body[i].x = sx - i;
        body[i].y = sy;
    }

    /* Draw background */
    sys_fill_rect(0, HDR_H, WIN_W, ROWS * CELL_SIZE, COLOR_BG);

    /* Draw initial snake */
    for (int i = 1; i < snake_len; i++)
        draw_cell(body[i].x, body[i].y, COLOR_SNAKE);
    draw_cell(body[0].x, body[0].y, COLOR_HEAD);

    /* Draw header and food */
    draw_hdr();
    place_food();
}

/* ── draw_title_screen ───────────────────────────────────────────────────── */
static void draw_title_screen(void)
{
    sys_fill_rect(0, 0, WIN_W, WIN_H, COLOR_BG);
    sys_draw_text(WIN_W / 2 - 24, WIN_H / 2 - 20, "S N A K E",   COLOR_HEAD,  COLOR_BG);
    sys_draw_text(WIN_W / 2 - 64, WIN_H / 2 + 4,  "ENTER=start", COLOR_TEXT,  COLOR_BG);
    sys_draw_text(WIN_W / 2 - 64, WIN_H / 2 + 20, "ESC=quit",    COLOR_TEXT,  COLOR_BG);
}

/* ── draw_dead_screen: overlay text on frozen board ─────────────────────── */
static void draw_dead_screen(void)
{
    char buf[64];
    int cx = WIN_W / 2;
    int cy = WIN_H / 2;

    sys_draw_text(cx - 40, cy - 20, "GAME OVER",      COLOR_DEAD, COLOR_BG);
    snprintf(buf, sizeof(buf), "Score: %d", score);
    sys_draw_text(cx - 32, cy,      buf,               COLOR_TEXT, COLOR_BG);
    sys_draw_text(cx - 72, cy + 16, "R=restart",      COLOR_TEXT, COLOR_BG);
    sys_draw_text(cx - 72, cy + 32, "ESC=quit",       COLOR_TEXT, COLOR_BG);
}

/* ── tick_rate: ticks per snake step (lower = faster) ───────────────────── */
static int tick_rate(void)
{
    int r = 7 - score / 5;
    return r < 3 ? 3 : r;
}

/* ── handle_input: process one window event ──────────────────────────────── */
static void handle_input(long ev)
{
    int pressed = (ev & 0x100) != 0;
    int sc      = (int)(ev & 0xFF);

    if (!pressed) return;

    if (sc == SC_ESC) { quit = 1; return; }

    if (state == STATE_TITLE) {
        if (sc == SC_ENTER) {
            state = STATE_PLAY;
            game_init();
        }
        return;
    }

    if (state == STATE_DEAD) {
        if (sc == SC_R) {
            state = STATE_PLAY;
            game_init();
        }
        return;
    }

    /* STATE_PLAY direction changes — forbid 180-degree reversal */
    if (sc == SC_UP    && dir != DIR_DOWN)  next_dir = DIR_UP;
    if (sc == SC_DOWN  && dir != DIR_UP)    next_dir = DIR_DOWN;
    if (sc == SC_LEFT  && dir != DIR_RIGHT) next_dir = DIR_LEFT;
    if (sc == SC_RIGHT && dir != DIR_LEFT)  next_dir = DIR_RIGHT;
}

/* ── step: advance snake by one cell ─────────────────────────────────────── */
static void step(void)
{
    dir = next_dir;

    /* Compute new head position */
    Cell nh = body[head_idx];
    if (dir == DIR_UP)    nh.y--;
    if (dir == DIR_DOWN)  nh.y++;
    if (dir == DIR_LEFT)  nh.x--;
    if (dir == DIR_RIGHT) nh.x++;

    /* Wall collision */
    if (nh.x < 0 || nh.x >= COLS || nh.y < 0 || nh.y >= ROWS) {
        state = STATE_DEAD;
        return;
    }

    /* Self collision — check all segments except the tail (which will move) */
    for (int i = 0; i < snake_len - 1; i++) {
        int idx = (head_idx + i) % MAX_LEN;
        if (body[idx].x == nh.x && body[idx].y == nh.y) {
            state = STATE_DEAD;
            return;
        }
    }

    /* Check food */
    int ate = (nh.x == food.x && nh.y == food.y);

    /* Erase old tail cell (before advancing head_idx) — only when not growing */
    if (!ate) {
        int tail_idx = (head_idx + snake_len - 1) % MAX_LEN;
        draw_cell(body[tail_idx].x, body[tail_idx].y, COLOR_BG);
    }

    /* Recolor old head as body */
    draw_cell(body[head_idx].x, body[head_idx].y, COLOR_SNAKE);

    /* Advance circular buffer head */
    head_idx = (head_idx - 1 + MAX_LEN) % MAX_LEN;
    body[head_idx] = nh;

    /* Draw new head */
    draw_cell(nh.x, nh.y, COLOR_HEAD);

    if (ate) {
        snake_len++;
        score++;
        draw_hdr();
        place_food();
    }
}

/* ── main ─────────────────────────────────────────────────────────────────── */
int main(void)
{
    wid = sys_create_window(80, 60, WIN_W, WIN_H);
    if (wid < 0) {
        puts("snake: failed to create window");
        return 1;
    }
    sys_set_window_title(wid, "Snake");

    srand((unsigned int)sys_get_ticks());
    draw_title_screen();

    long last_tick = sys_get_ticks();

    while (!quit) {
        /* Drain all pending events */
        long ev;
        while ((ev = sys_get_window_event(wid)) != 0)
            handle_input(ev);

        if (state == STATE_PLAY) {
            long now = sys_get_ticks();
            if (now - last_tick >= tick_rate()) {
                last_tick = now;
                step();
                if (state == STATE_DEAD)
                    draw_dead_screen();
            }
        }

        sys_yield();
    }

    sys_destroy_window(wid);
    return 0;
}
