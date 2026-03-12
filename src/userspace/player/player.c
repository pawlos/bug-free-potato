/*
 * MPEG-1 Video Player for potatOS
 * Uses pl_mpeg (single-header MPEG1/MP2 decoder by Dominic Szablewski)
 */

#include "libc/syscall.h"
#include "libc/stdlib.h"
#include "libc/string.h"
#include "libc/stdio.h"

#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg.h"

typedef struct {
    long     wid;           /* window id */
    int      width;
    int      height;
    unsigned char *rgb_buf; /* RGB24 frame buffer */
    int      sample_rate;
} player_ctx_t;

static void on_video(plm_t *plm, plm_frame_t *frame, void *user)
{
    (void)plm;
    player_ctx_t *ctx = (player_ctx_t *)user;
    plm_frame_to_rgb(frame, ctx->rgb_buf, ctx->width * 3);
    sys_draw_pixels(ctx->rgb_buf, 0, 0, ctx->width, ctx->height);
}

/* Accumulation buffer — submit chunks to avoid DMA stop/start gaps.
   ~2400 stereo frames ≈ 50ms at 48 kHz. */
#define AUDIO_BUF_FRAMES  4800
static short g_audio_buf[AUDIO_BUF_FRAMES * 2];
static int   g_audio_fill = 0;   /* stereo frames accumulated */
#define SUBMIT_FRAMES     2400   /* submit when we reach this many frames */

static void audio_submit(int sample_rate)
{
    if (g_audio_fill <= 0) return;
    /* Wait for AC97 to finish current playback before submitting.
       This paces audio to real-time — prevents running ahead. */
    while (sys_audio_is_playing() == 1)
        sys_yield();
    sys_audio_write(g_audio_buf,
                    (unsigned long)(g_audio_fill * 4),
                    (unsigned int)sample_rate);
    g_audio_fill = 0;
}

static void on_audio(plm_t *plm, plm_samples_t *samples, void *user)
{
    (void)plm;
    player_ctx_t *ctx = (player_ctx_t *)user;
    int count = samples->count;  /* number of stereo frames */

    /* Append decoded samples to accumulation buffer */
    for (int i = 0; i < count && g_audio_fill < AUDIO_BUF_FRAMES; i++) {
        for (int ch = 0; ch < 2; ch++) {
            float s = samples->interleaved[i * 2 + ch];
            if (s >  1.0f) s =  1.0f;
            if (s < -1.0f) s = -1.0f;
            g_audio_buf[g_audio_fill * 2 + ch] = (short)(s * 32767.0f);
        }
        g_audio_fill++;
    }

    /* Submit when threshold reached */
    if (g_audio_fill >= SUBMIT_FRAMES)
        audio_submit(ctx->sample_rate);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: player <file.mpg>\n");
        return 1;
    }

    const char *filename = argv[1];

    plm_t *plm = plm_create_with_filename(filename);
    if (!plm) {
        printf("Error: could not open %s\n", filename);
        return 1;
    }

    int width  = plm_get_width(plm);
    int height = plm_get_height(plm);

    if (width <= 0 || height <= 0) {
        printf("Error: invalid video dimensions %dx%d\n", width, height);
        plm_destroy(plm);
        return 1;
    }

    /* Center the window on screen */
    long scr_w = sys_fb_width();
    long scr_h = sys_fb_height();
    long wx = (scr_w - width) / 2;
    long wy = (scr_h - height) / 2;
    if (wx < 0) wx = 0;
    if (wy < 0) wy = 0;

    long wid = sys_create_window(wx, wy, width, height);
    if (wid < 0) {
        printf("Error: could not create window\n");
        plm_destroy(plm);
        return 1;
    }

    /* Show filename in window title bar; taskbar uses task_name instead */
    sys_set_window_title(wid, filename);

    player_ctx_t ctx;
    ctx.wid       = wid;
    ctx.width     = width;
    ctx.height    = height;
    ctx.rgb_buf   = (unsigned char *)malloc(width * height * 3);

    if (!ctx.rgb_buf) {
        printf("Error: out of memory for frame buffer\n");
        sys_destroy_window(wid);
        plm_destroy(plm);
        return 1;
    }

    memset(ctx.rgb_buf, 0, width * height * 3);

    /* Configure decoder */
    plm_set_video_decode_callback(plm, on_video, &ctx);

    int has_audio = plm_get_num_audio_streams(plm) > 0;
    if (has_audio) {
        ctx.sample_rate = plm_get_samplerate(plm);
        plm_set_audio_enabled(plm, 1);
        plm_set_audio_decode_callback(plm, on_audio, &ctx);
        plm_set_audio_lead_time(plm, (double)PLM_AUDIO_SAMPLES_PER_FRAME / (double)ctx.sample_rate);
    }

    plm_set_loop(plm, 0);

    /* Main decode loop */
    unsigned long last_us = sys_get_micros();

    while (!plm_has_ended(plm)) {
        /* Check for ESC key to quit */
        long ev = sys_get_window_event(wid);
        if (ev > 0) {
            /* Key events: bits 7..0 = scancode, bit 8 = pressed */
            int scancode = ev & 0xFF;
            int pressed  = (ev >> 8) & 1;
            if (pressed && scancode == 0x01) /* ESC scancode */
                break;
        }

        unsigned long now_us = sys_get_micros();
        double elapsed = (double)(now_us - last_us) / 1000000.0;
        last_us = now_us;

        /* Cap elapsed to avoid huge jumps (e.g., after a stall) */
        if (elapsed > 0.1)
            elapsed = 0.1;

        plm_decode(plm, elapsed);

        /* Yield to let other tasks run */
        sys_yield();
    }

    /* Cleanup */
    free(ctx.rgb_buf);
    sys_destroy_window(wid);
    plm_destroy(plm);

    return 0;
}
