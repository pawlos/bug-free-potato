/*
 * sdl2demo.c — SDL2 shim demo for potat OS.
 *
 * Shows a window with:
 *   - A bouncing ball (automatic)
 *   - A player-controlled square (arrow keys)
 *   - Mouse position tracking (crosshair)
 *   - FPS counter
 *
 * Controls:
 *   Arrow keys  — move the square
 *   ESC         — quit
 */

#include "libc/SDL2/SDL.h"
#include "libc/stdio.h"

#define WIN_W  400
#define WIN_H  300

/* Colors (ARGB8888) */
#define COL_BG     0xFF1A1A2E
#define COL_BALL   0xFFE94560
#define COL_PLAYER 0xFF0F3460
#define COL_CROSS  0xFF16C79A
#define COL_TEXT   0xFFEEEEEE

static Uint32 framebuf[WIN_W * WIN_H];

static void clear(Uint32 color)
{
    for (int i = 0; i < WIN_W * WIN_H; i++)
        framebuf[i] = color;
}

static void draw_rect(int rx, int ry, int rw, int rh, Uint32 color)
{
    for (int y = ry; y < ry + rh && y < WIN_H; y++) {
        if (y < 0) continue;
        for (int x = rx; x < rx + rw && x < WIN_W; x++) {
            if (x < 0) continue;
            framebuf[y * WIN_W + x] = color;
        }
    }
}

static void draw_circle(int cx, int cy, int r, Uint32 color)
{
    for (int y = cy - r; y <= cy + r; y++) {
        if (y < 0 || y >= WIN_H) continue;
        for (int x = cx - r; x <= cx + r; x++) {
            if (x < 0 || x >= WIN_W) continue;
            int dx = x - cx, dy = y - cy;
            if (dx*dx + dy*dy <= r*r)
                framebuf[y * WIN_W + x] = color;
        }
    }
}

static void draw_crosshair(int cx, int cy, Uint32 color)
{
    /* Horizontal line */
    for (int x = cx - 8; x <= cx + 8; x++) {
        if (x >= 0 && x < WIN_W && cy >= 0 && cy < WIN_H)
            framebuf[cy * WIN_W + x] = color;
    }
    /* Vertical line */
    for (int y = cy - 8; y <= cy + 8; y++) {
        if (y >= 0 && y < WIN_H && cx >= 0 && cx < WIN_W)
            framebuf[y * WIN_W + cx] = color;
    }
}

int main(void)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;

    SDL_Window *win = SDL_CreateWindow("SDL2 Demo", SDL_WINDOWPOS_CENTERED,
                                       SDL_WINDOWPOS_CENTERED, WIN_W, WIN_H,
                                       SDL_WINDOW_SHOWN);
    if (!win) { SDL_Quit(); return 1; }

    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) { SDL_DestroyWindow(win); SDL_Quit(); return 1; }

    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                                         SDL_TEXTUREACCESS_STREAMING,
                                         WIN_W, WIN_H);
    if (!tex) { SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); SDL_Quit(); return 1; }

    /* Ball state (bouncing) */
    int ball_x = WIN_W / 2, ball_y = WIN_H / 2;
    int ball_dx = 3, ball_dy = 2;
    int ball_r = 15;

    /* Player square */
    int px = 50, py = 50;
    int pw = 30, ph = 30;
    int speed = 5;

    /* Mouse crosshair */
    int mouse_x = WIN_W / 2, mouse_y = WIN_H / 2;

    /* Arrow key states */
    int key_up = 0, key_down = 0, key_left = 0, key_right = 0;

    /* FPS tracking */
    Uint32 last_fps_time = SDL_GetTicks();
    int frame_count = 0;
    int fps = 0;

    int running = 1;
    while (running) {
        /* ── Events ─────────────────────────────────────────────── */
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_QUIT:
                running = 0;
                break;
            case SDL_KEYDOWN:
                switch (ev.key.keysym.sym) {
                case SDLK_ESCAPE: running = 0; break;
                case SDLK_UP:     key_up    = 1; break;
                case SDLK_DOWN:   key_down  = 1; break;
                case SDLK_LEFT:   key_left  = 1; break;
                case SDLK_RIGHT:  key_right = 1; break;
                }
                break;
            case SDL_KEYUP:
                switch (ev.key.keysym.sym) {
                case SDLK_UP:     key_up    = 0; break;
                case SDLK_DOWN:   key_down  = 0; break;
                case SDLK_LEFT:   key_left  = 0; break;
                case SDLK_RIGHT:  key_right = 0; break;
                }
                break;
            case SDL_MOUSEMOTION:
                mouse_x = ev.motion.x;
                mouse_y = ev.motion.y;
                break;
            }
        }

        /* ── Update ─────────────────────────────────────────────── */
        /* Move player */
        if (key_up)    py -= speed;
        if (key_down)  py += speed;
        if (key_left)  px -= speed;
        if (key_right) px += speed;
        if (px < 0) px = 0;
        if (py < 0) py = 0;
        if (px + pw > WIN_W) px = WIN_W - pw;
        if (py + ph > WIN_H) py = WIN_H - ph;

        /* Bounce ball */
        ball_x += ball_dx;
        ball_y += ball_dy;
        if (ball_x - ball_r < 0 || ball_x + ball_r >= WIN_W) ball_dx = -ball_dx;
        if (ball_y - ball_r < 0 || ball_y + ball_r >= WIN_H) ball_dy = -ball_dy;

        /* FPS */
        frame_count++;
        Uint32 now = SDL_GetTicks();
        if (now - last_fps_time >= 1000) {
            fps = frame_count;
            frame_count = 0;
            last_fps_time = now;
            char title[64];
            snprintf(title, sizeof(title), "SDL2 Demo - %d FPS", fps);
            SDL_SetWindowTitle(win, title);
        }

        /* ── Draw ───────────────────────────────────────────────── */
        clear(COL_BG);
        draw_rect(px, py, pw, ph, COL_PLAYER);
        draw_circle(ball_x, ball_y, ball_r, COL_BALL);
        draw_crosshair(mouse_x, mouse_y, COL_CROSS);

        /* Push to screen */
        SDL_UpdateTexture(tex, (SDL_Rect*)0, framebuf, WIN_W * 4);
        SDL_RenderCopy(ren, tex, (SDL_Rect*)0, (SDL_Rect*)0);
        SDL_RenderPresent(ren);

        /* ~30 FPS cap */
        SDL_Delay(33);
    }

    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
