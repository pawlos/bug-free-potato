/*
 * Image Viewer for potatOS
 * Opens each image argument in its own window.
 * Supports PNG, JPEG, BMP, TGA, GIF via stb_image.
 */

#include "libc/syscall.h"
#include "libc/stdlib.h"
#include "libc/string.h"
#include "libc/stdio.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_THREAD_LOCALS
#define STBI_NO_SIMD
#include "stb_image.h"

#define MAX_IMAGES 8
#define STAGGER    20   /* pixel offset per window to avoid overlap */
#define TITLE_H    16   /* window chrome title bar height */
#define MAX_WIN_W  640  /* max window width to avoid huge kernel allocs */
#define MAX_WIN_H  480  /* max window height */

typedef struct {
    long           wid;     /* window ID, -1 if closed */
    int            width;
    int            height;
    unsigned char *rgb;     /* RGB24 pixel data from stbi_load */
} ImageWindow;

static ImageWindow g_images[MAX_IMAGES];
static int g_open = 0;  /* number of currently open windows */

static const char *basename(const char *path) {
    const char *last = path;
    for (const char *p = path; *p; p++)
        if (*p == '/') last = p + 1;
    return last;
}

/* Nearest-neighbor downscale from (src_w x src_h) to (dst_w x dst_h).
   Both src and dst are RGB24 (3 bytes per pixel). */
static unsigned char *scale_image(const unsigned char *src,
                                  int src_w, int src_h,
                                  int dst_w, int dst_h)
{
    unsigned char *dst = (unsigned char *)malloc(dst_w * dst_h * 3);
    if (!dst) return NULL;

    for (int y = 0; y < dst_h; y++) {
        int sy = y * src_h / dst_h;
        for (int x = 0; x < dst_w; x++) {
            int sx = x * src_w / dst_w;
            const unsigned char *sp = src + (sy * src_w + sx) * 3;
            unsigned char *dp = dst + (y * dst_w + x) * 3;
            dp[0] = sp[0];
            dp[1] = sp[1];
            dp[2] = sp[2];
        }
    }
    return dst;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: imgview <image1> [image2] ...\n");
        return 1;
    }

    long scr_w = sys_fb_width();
    long scr_h = sys_fb_height();

    /* Cap window size to avoid huge kernel pixel buffer allocations */
    int cap_w = MAX_WIN_W;
    int cap_h = MAX_WIN_H;
    if (cap_w > (int)scr_w - 2) cap_w = (int)scr_w - 2;
    if (cap_h > (int)scr_h - TITLE_H - 2) cap_h = (int)scr_h - TITLE_H - 2;

    int count = argc - 1;
    if (count > MAX_IMAGES) count = MAX_IMAGES;

    for (int i = 0; i < count; i++) {
        const char *path = argv[i + 1];
        int w, h, channels;
        unsigned char *data = stbi_load(path, &w, &h, &channels, 3);
        if (!data) {
            printf("imgview: cannot open %s: %s\n", path, stbi_failure_reason());
            g_images[i].wid = -1;
            continue;
        }

        /* Determine display size — scale to fit within cap, preserving aspect ratio */
        int win_w = w;
        int win_h = h;
        if (win_w > cap_w || win_h > cap_h) {
            /* Scale factor: pick the smaller ratio to fit both dimensions */
            int scale_w = w * 1000 / cap_w;  /* fixed-point x1000 */
            int scale_h = h * 1000 / cap_h;
            int scale = scale_w > scale_h ? scale_w : scale_h;
            win_w = w * 1000 / scale;
            win_h = h * 1000 / scale;
            if (win_w < 1) win_w = 1;
            if (win_h < 1) win_h = 1;
        }

        /* Scale or use original */
        unsigned char *draw_data;
        if (win_w != w || win_h != h) {
            draw_data = scale_image(data, w, h, win_w, win_h);
            if (!draw_data) {
                printf("imgview: out of memory scaling %s\n", path);
                stbi_image_free(data);
                g_images[i].wid = -1;
                continue;
            }
        } else {
            draw_data = data;
        }

        /* stagger windows so they don't perfectly overlap */
        long wx = (scr_w - win_w) / 2 + i * STAGGER;
        long wy = (scr_h - win_h) / 2 + i * STAGGER;
        if (wx < 0) wx = 0;
        if (wy < 0) wy = 0;

        long wid = sys_create_window(wx, wy, win_w, win_h);
        if (wid < 0) {
            printf("imgview: cannot create window for %s\n", path);
            if (draw_data != data) free(draw_data);
            stbi_image_free(data);
            g_images[i].wid = -1;
            continue;
        }

        sys_set_window_title(wid, basename(path));
        sys_draw_pixels(draw_data, 0, 0, win_w, win_h);
        if (draw_data != data) free(draw_data);

        g_images[i].wid    = wid;
        g_images[i].width  = win_w;
        g_images[i].height = win_h;
        g_images[i].rgb    = data;
        g_open++;
    }

    if (g_open == 0) return 1;

    /* event loop: poll keyboard for ESC to close all windows */
    while (g_open > 0) {
        long ev = sys_get_key_event();
        if (ev != -1) {
            int pressed  = (ev >> 8) & 1;
            int scancode = (int)(ev & 0xFF);
            /* ESC = scancode 0x01 */
            if (pressed && scancode == 0x01) {
                for (int i = 0; i < count; i++) {
                    if (g_images[i].wid < 0) continue;
                    sys_destroy_window(g_images[i].wid);
                    stbi_image_free(g_images[i].rgb);
                    g_images[i].wid = -1;
                    g_images[i].rgb = NULL;
                    g_open--;
                }
            }
        }
        sys_sleep_ms(20);
    }

    return 0;
}
