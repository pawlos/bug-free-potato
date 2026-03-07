/* q2_potato.c -- potatOS platform layer for Quake 2
 *
 * Replaces all Linux/Win32-specific code with potatOS syscall wrappers.
 * Combines: sys, vid, input, sound, cd, network platform code.
 *
 * Statically links both ref_soft (renderer) and game/ (game DLL).
 */

#include "quake2-src/qcommon/qcommon.h"
#include "quake2-src/client/client.h"
#include "quake2-src/client/snd_loc.h"
#include "quake2-src/game/game.h"
#include "quake2-src/linux/glob.h"

/* Forward-declare ref_soft types (avoid r_local.h which redeclares vid.h types) */
typedef enum { rserr_ok, rserr_invalid_fullscreen, rserr_invalid_mode, rserr_unknown } rserr_t;

/* ref_soft's viddef_t is larger than vid.h's — it has buffer/rowbytes fields.
   We access ref_soft's "vid" variable via this extended struct layout. */
typedef unsigned char pixel_t;
typedef struct {
    pixel_t *buffer;
    pixel_t *colormap;
    pixel_t *alphamap;
    int      rowbytes;
    int      width;
    int      height;
} refsoft_vid_t;
extern refsoft_vid_t vid;  /* defined in ref_soft/r_main.c as viddef_t */
extern short *d_pzbuffer;  /* ref_soft z-buffer (r_main.c) */

#include "libc/syscall.h"
#include "libc/string.h"
#include "libc/stdio.h"
#include "libc/stdlib.h"
#include "libc/dirent.h"

/* ======================================================================
 *  SYSTEM (Sys_*)
 * ====================================================================== */

cvar_t *nostdout;
unsigned sys_frame_time;

int curtime;
int Sys_Milliseconds(void)
{
    unsigned long long us = sys_get_micros();
    static unsigned long long base_us;
    if (!base_us) base_us = us;
    curtime = (int)((us - base_us) / 1000ULL);
    return curtime;
}

void Sys_Init(void) { }

void Sys_Error(char *error, ...)
{
    va_list ap;
    char buf[1024];
    va_start(ap, error);
    vsnprintf(buf, sizeof(buf), error, ap);
    va_end(ap);

    sys_write_serial("\n** Q2 ERROR: ", 14);
    sys_write_serial(buf, strlen(buf));
    sys_write_serial(" **\n", 4);

    printf("\n** Q2 ERROR: %s **\n", buf);
    sys_exit(1);
}

void Sys_Quit(void)
{
    sys_exit(0);
}

void Sys_ConsoleOutput(char *string)
{
    if (string)
        sys_write_serial(string, strlen(string));
}

char *Sys_ConsoleInput(void)
{
    return NULL;
}

void Sys_AppActivate(void) { }

void Sys_CopyProtect(void) { }

char *Sys_GetClipboardData(void) { return NULL; }

void Sys_Mkdir(char *path) { (void)path; }

int Sys_FileTime(char *path) { (void)path; return -1; }

/* ── Hunk allocator (large contiguous blocks via sys_mmap) ────────────── */

static byte *membase;
static int maxhunksize;
static int curhunksize;

void *Hunk_Begin(int maxsize)
{
    maxhunksize = maxsize + sizeof(int);
    curhunksize = 0;
    membase = (byte *)sys_mmap((size_t)maxhunksize);
    if (!membase || (long)membase == -1L)
        Sys_Error("Hunk_Begin: unable to allocate %d bytes", maxsize);
    *((int *)membase) = curhunksize;
    return membase + sizeof(int);
}

void *Hunk_Alloc(int size)
{
    byte *buf;
    size = (size + 31) & ~31;
    if (curhunksize + size > maxhunksize)
        Sys_Error("Hunk_Alloc overflow");
    buf = membase + sizeof(int) + curhunksize;
    curhunksize += size;
    return buf;
}

int Hunk_End(void)
{
    /* Can't remap on potatOS -- just keep the full allocation */
    *((int *)membase) = curhunksize + sizeof(int);
    return curhunksize;
}

void Hunk_Free(void *base)
{
    if (base) {
        byte *m = ((byte *)base) - sizeof(int);
        sys_munmap(m, (size_t)(*((int *)m)));
    }
}

/* ── File search (Sys_FindFirst / Sys_FindNext) ──────────────────────── */

static char findbase[MAX_OSPATH];
static char findpath[MAX_OSPATH];
static char findpattern[MAX_OSPATH];
static DIR *fdir;

char *Sys_FindFirst(char *path, unsigned musthave, unsigned canthave)
{
    struct dirent *d;
    char *p;

    (void)musthave; (void)canthave;

    if (fdir)
        Sys_Error("Sys_FindFirst without close");

    strcpy(findbase, path);
    if ((p = strrchr(findbase, '/')) != NULL) {
        *p = 0;
        strcpy(findpattern, p + 1);
    } else {
        strcpy(findpattern, "*");
    }

    if (strcmp(findpattern, "*.*") == 0)
        strcpy(findpattern, "*");

    if ((fdir = opendir(findbase)) == NULL)
        return NULL;

    while ((d = readdir(fdir)) != NULL) {
        if (!*findpattern || glob_match(findpattern, d->d_name)) {
            sprintf(findpath, "%s/%s", findbase, d->d_name);
            return findpath;
        }
    }
    return NULL;
}

char *Sys_FindNext(unsigned musthave, unsigned canthave)
{
    struct dirent *d;
    (void)musthave; (void)canthave;

    if (!fdir) return NULL;
    while ((d = readdir(fdir)) != NULL) {
        if (!*findpattern || glob_match(findpattern, d->d_name)) {
            sprintf(findpath, "%s/%s", findbase, d->d_name);
            return findpath;
        }
    }
    return NULL;
}

void Sys_FindClose(void)
{
    if (fdir) closedir(fdir);
    fdir = NULL;
}

/* ── Game DLL (static link) ──────────────────────────────────────────── */

extern game_export_t *GetGameAPI(game_import_t *import);

void Sys_UnloadGame(void) { }

void *Sys_GetGameAPI(void *parms)
{
    return (void *)GetGameAPI((game_import_t *)parms);
}

/* ── Key events / input ──────────────────────────────────────────────── */

void Sys_SendKeyEvents(void)
{
    IN_Frame();
    sys_frame_time = Sys_Milliseconds();
}

/* ======================================================================
 *  VIDEO (VID_* + SWimp_*)
 *  Statically links ref_soft -- based on null/vid_null.c pattern
 * ====================================================================== */

viddef_t viddef;
refexport_t re;
refexport_t GetRefAPI(refimport_t rimp);

#define MAXPRINTMSG 4096

void VID_Printf(int print_level, char *fmt, ...)
{
    va_list ap;
    char msg[MAXPRINTMSG];
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    if (print_level == PRINT_ALL)
        Com_Printf("%s", msg);
    else
        Com_DPrintf("%s", msg);
}

void VID_Error(int err_level, char *fmt, ...)
{
    va_list ap;
    char msg[MAXPRINTMSG];
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    Com_Error(err_level, "%s", msg);
}

void VID_NewWindow(int width, int height)
{
    viddef.width = width;
    viddef.height = height;
}

typedef struct {
    const char *description;
    int width, height, mode;
} vidmode_t;

static vidmode_t vid_modes[] = {
    { "Mode 0: 320x240",  320,  240,  0 },
    { "Mode 1: 400x300",  400,  300,  1 },
    { "Mode 2: 512x384",  512,  384,  2 },
    { "Mode 3: 640x480",  640,  480,  3 },
};
#define VID_NUM_MODES (sizeof(vid_modes) / sizeof(vid_modes[0]))

qboolean VID_GetModeInfo(int *width, int *height, int mode)
{
    if (mode < 0 || mode >= (int)VID_NUM_MODES) return false;
    *width  = vid_modes[mode].width;
    *height = vid_modes[mode].height;
    return true;
}

void VID_Init(void)
{
    refimport_t ri;

    viddef.width  = 320;
    viddef.height = 240;

    ri.Cmd_AddCommand    = Cmd_AddCommand;
    ri.Cmd_RemoveCommand = Cmd_RemoveCommand;
    ri.Cmd_Argc          = Cmd_Argc;
    ri.Cmd_Argv          = Cmd_Argv;
    ri.Cmd_ExecuteText   = Cbuf_ExecuteText;
    ri.Con_Printf        = VID_Printf;
    ri.Sys_Error         = VID_Error;
    ri.FS_LoadFile       = FS_LoadFile;
    ri.FS_FreeFile       = FS_FreeFile;
    ri.FS_Gamedir        = FS_Gamedir;
    ri.Vid_NewWindow     = VID_NewWindow;
    ri.Cvar_Get          = Cvar_Get;
    ri.Cvar_Set          = Cvar_Set;
    ri.Cvar_SetValue     = Cvar_SetValue;
    ri.Vid_GetModeInfo   = VID_GetModeInfo;
    ri.Vid_MenuInit      = VID_MenuInit;

    re = GetRefAPI(ri);
    if (re.api_version != API_VERSION)
        Com_Error(ERR_FATAL, "Re has incompatible api_version");
    if (re.Init(NULL, NULL) == -1)
        Com_Error(ERR_FATAL, "Couldn't start refresh");
}

void VID_Shutdown(void)
{
    if (re.Shutdown) re.Shutdown();
}

void VID_CheckChanges(void) { }
void VID_MenuInit(void) { }
void VID_MenuDraw(void) { }
const char *VID_MenuKey(int k) { (void)k; return NULL; }

/* ── SWimp: software renderer platform bridge ───────────────────────── */

/* Window and framebuffer state */
static long g_wid = -1;
#define Q2_SCALE 2
static int g_render_w = 320;
static int g_render_h = 240;
#define DISP_W (g_render_w * Q2_SCALE)
#define DISP_H (g_render_h * Q2_SCALE)

static unsigned char g_palette[1024];  /* RGBA, 4 bytes per entry */
static unsigned char *g_rgb_buf;  /* allocated on mode set */

int SWimp_Init(void *hInstance, void *wndProc)
{
    (void)hInstance; (void)wndProc;
    return 0;  /* success */
}

rserr_t SWimp_SetMode(int *pwidth, int *pheight, int mode, qboolean fullscreen)
{
    int w, h;
    (void)fullscreen;

    if (!VID_GetModeInfo(&w, &h, mode))
        return rserr_invalid_mode;

    g_render_w = w;
    g_render_h = h;

    /* Allocate the render target -- ref_soft writes 8-bit indexed here */
    if (vid.buffer) free(vid.buffer);
    vid.buffer = (byte *)malloc(w * h);
    if (!vid.buffer) return rserr_unknown;
    memset(vid.buffer, 0, w * h);

    vid.width    = w;
    vid.height   = h;
    vid.rowbytes = w;

    /* Allocate RGB24 output buffer for scaled display */
    if (g_rgb_buf) free(g_rgb_buf);
    g_rgb_buf = (unsigned char *)malloc(DISP_W * DISP_H * 3);

    /* Create/recreate window */
    if (g_wid >= 0) sys_destroy_window(g_wid);
    int fb_w = (int)sys_fb_width();
    int fb_h = (int)sys_fb_height();
    int cx = (fb_w - DISP_W) / 2;
    int cy = (fb_h - DISP_H) / 2;
    if (cx < 0) cx = 0;
    if (cy < 18) cy = 18;
    g_wid = sys_create_window(cx, cy, DISP_W, DISP_H);
    sys_set_window_title(g_wid, "Quake II");

    *pwidth = w;
    *pheight = h;

    VID_NewWindow(w, h);
    return rserr_ok;
}

void SWimp_SetPalette(const unsigned char *palette)
{
    if (palette)
        memcpy(g_palette, palette, 1024);  /* 256 × RGBA */
    else {
        /* Default grayscale */
        int i;
        for (i = 0; i < 256; i++) {
            g_palette[i*4+0] = (unsigned char)i;
            g_palette[i*4+1] = (unsigned char)i;
            g_palette[i*4+2] = (unsigned char)i;
            g_palette[i*4+3] = 255;
        }
    }
}

void SWimp_EndFrame(void)
{
    if (!vid.buffer || !g_rgb_buf || g_wid < 0) return;

    /* Convert 8-bit indexed to RGB24 at 2x scale (palette is RGBA, 4 bytes/entry) */
    int sy, sx;
    for (sy = 0; sy < g_render_h; sy++) {
        unsigned char row[640 * Q2_SCALE * 3];  /* max width row (up to 640px) */
        unsigned char *rp = row;
        for (sx = 0; sx < g_render_w; sx++) {
            unsigned int idx = (unsigned int)vid.buffer[sy * vid.rowbytes + sx] * 4u;
            unsigned char r = g_palette[idx + 0];
            unsigned char g = g_palette[idx + 1];
            unsigned char b = g_palette[idx + 2];
            int s;
            for (s = 0; s < Q2_SCALE; s++) {
                *rp++ = r; *rp++ = g; *rp++ = b;
            }
        }
        int row_bytes = g_render_w * Q2_SCALE * 3;
        int s;
        for (s = 0; s < Q2_SCALE; s++) {
            memcpy(g_rgb_buf + (sy * Q2_SCALE + s) * row_bytes, row, row_bytes);
        }
    }

    sys_draw_pixels(g_rgb_buf, 0, 0, DISP_W, DISP_H);
}

void SWimp_BeginFrame(float camera_separation) { (void)camera_separation; }
void SWimp_Shutdown(void)
{
    if (g_wid >= 0) { sys_destroy_window(g_wid); g_wid = -1; }
    if (vid.buffer) { free(vid.buffer); vid.buffer = NULL; }
    if (g_rgb_buf) { free(g_rgb_buf); g_rgb_buf = NULL; }
}
void SWimp_AppActivate(qboolean active) { (void)active; }

/* ======================================================================
 *  INPUT (IN_*)
 * ====================================================================== */

static int g_mouse_dx, g_mouse_dy;
static int g_mb_left, g_mb_right;

static int sc_to_q2key(int sc)
{
    switch (sc) {
    case 0x48: return K_UPARROW;   case 0x50: return K_DOWNARROW;
    case 0x4B: return K_LEFTARROW; case 0x4D: return K_RIGHTARROW;
    case 0x01: return K_ESCAPE;    case 0x1C: return K_ENTER;
    case 0x0F: return K_TAB;       case 0x39: return K_SPACE;
    case 0x0E: return K_BACKSPACE;
    case 0x1D: return K_CTRL;      case 0x38: return K_ALT;
    case 0x2A: case 0x36: return K_SHIFT;

    case 0x3B: return K_F1;  case 0x3C: return K_F2;
    case 0x3D: return K_F3;  case 0x3E: return K_F4;
    case 0x3F: return K_F5;  case 0x40: return K_F6;
    case 0x41: return K_F7;  case 0x42: return K_F8;
    case 0x43: return K_F9;  case 0x44: return K_F10;
    case 0x57: return K_F11; case 0x58: return K_F12;

    case 0x47: return K_HOME;  case 0x4F: return K_END;
    case 0x49: return K_PGUP;  case 0x51: return K_PGDN;
    case 0x52: return K_INS;   case 0x53: return K_DEL;

    case 0x10: return 'q'; case 0x11: return 'w'; case 0x12: return 'e';
    case 0x13: return 'r'; case 0x14: return 't'; case 0x15: return 'y';
    case 0x16: return 'u'; case 0x17: return 'i'; case 0x18: return 'o';
    case 0x19: return 'p'; case 0x1E: return 'a'; case 0x1F: return 's';
    case 0x20: return 'd'; case 0x21: return 'f'; case 0x22: return 'g';
    case 0x23: return 'h'; case 0x24: return 'j'; case 0x25: return 'k';
    case 0x26: return 'l'; case 0x2C: return 'z'; case 0x2D: return 'x';
    case 0x2E: return 'c'; case 0x2F: return 'v'; case 0x30: return 'b';
    case 0x31: return 'n'; case 0x32: return 'm';

    case 0x02: return '1'; case 0x03: return '2'; case 0x04: return '3';
    case 0x05: return '4'; case 0x06: return '5'; case 0x07: return '6';
    case 0x08: return '7'; case 0x09: return '8'; case 0x0A: return '9';
    case 0x0B: return '0';
    case 0x0C: return '-'; case 0x0D: return '=';
    case 0x1A: return '['; case 0x1B: return ']';
    case 0x27: return ';'; case 0x28: return '\'';
    case 0x29: return '`'; case 0x2B: return '\\';
    case 0x33: return ','; case 0x34: return '.'; case 0x35: return '/';
    default:   return 0;
    }
}

cvar_t *in_joystick;

void IN_Init(void) { in_joystick = Cvar_Get("in_joystick", "0", 0); }
void IN_Shutdown(void) { }
void IN_Commands(void) { }
void IN_Activate(qboolean active) { (void)active; }
void IN_ActivateMouse(void) { }
void IN_DeactivateMouse(void) { }

void IN_Frame(void)
{
    /* Poll keyboard via window events */
    for (;;) {
        long ev = (g_wid >= 0) ? sys_get_window_event(g_wid) : 0;
        if (ev == 0) break;
        int sc = (int)(ev & 0xFF);
        int qk = sc_to_q2key(sc);
        if (!qk) continue;
        int down = (ev & 0x100) ? 1 : 0;
        Key_Event(qk, down, sys_frame_time);
    }

    /* Poll mouse */
    for (;;) {
        long mev = sys_get_mouse_event();
        if (mev == -1) break;
        int dx = (signed char)(mev & 0xFF);
        int dy = (signed char)((mev >> 8) & 0xFF);
        int lb  = (int)((mev >> 16) & 1);
        int rb  = (int)((mev >> 17) & 1);
        g_mouse_dx += dx;
        g_mouse_dy -= dy;
        if (lb != g_mb_left)  { g_mb_left  = lb; Key_Event(K_MOUSE1, lb, sys_frame_time); }
        if (rb != g_mb_right) { g_mb_right = rb; Key_Event(K_MOUSE2, rb, sys_frame_time); }
    }
}

void IN_Move(usercmd_t *cmd)
{
    if (g_mouse_dx || g_mouse_dy) {
        /* Apply mouse to view angles -- sensitivity scaled */
        cl.viewangles[YAW]   -= (float)g_mouse_dx * 0.022f * 5.0f;
        cl.viewangles[PITCH] += (float)g_mouse_dy * 0.022f * 5.0f;
        g_mouse_dx = 0;
        g_mouse_dy = 0;
    }
    (void)cmd;
}

/* ======================================================================
 *  SOUND (SNDDMA_*)
 * ====================================================================== */

#define SND_SPEED       11025
#define DMA_SAMPLES     8192
#define DMA_BYTES       (DMA_SAMPLES * 2)
#define SUBMIT_PAIRS    1024
#define SUBMIT_SAMPLES  (SUBMIT_PAIRS * 2)
#define SUBMIT_BYTES    (SUBMIT_SAMPLES * 2)

static short g_dma_buf[DMA_SAMPLES];
static short g_sub_buf[SUBMIT_SAMPLES];
static unsigned g_dma_pos;
static unsigned long long g_submit_us;

qboolean SNDDMA_Init(void)
{
    if (sys_audio_is_playing() == -1)
        return false;

    memset(&dma, 0, sizeof(dma));
    dma.speed            = SND_SPEED;
    dma.channels         = 2;
    dma.samplebits       = 16;
    dma.samples          = DMA_SAMPLES;
    dma.submission_chunk = SUBMIT_SAMPLES;
    dma.samplepos        = 0;
    dma.buffer           = (byte *)g_dma_buf;

    g_dma_pos   = 0;
    g_submit_us = 0;

    Com_Printf("Sound: potatOS AC97 backend, %d Hz stereo 16-bit\n", SND_SPEED);
    return true;
}

int SNDDMA_GetDMAPos(void)
{
    if (g_dma_pos == 0) return 0;
    unsigned long long now_us  = sys_get_micros();
    unsigned long long elapsed = now_us - g_submit_us;
    unsigned long long played  = elapsed * (SND_SPEED * 2ULL) / 1000000ULL;
    if (played > SUBMIT_SAMPLES) played = SUBMIT_SAMPLES;
    unsigned abs_pos = (g_dma_pos - SUBMIT_SAMPLES) + (unsigned)played;
    return (int)(abs_pos & (DMA_SAMPLES - 1));
}

void SNDDMA_Shutdown(void) { }
void SNDDMA_BeginPainting(void) { }

void SNDDMA_Submit(void)
{
    unsigned mix_pos   = (unsigned)paintedtime * 2;
    int      new_samps = (int)(mix_pos - g_dma_pos);
    if (new_samps < SUBMIT_SAMPLES) return;
    if (sys_audio_is_playing() == 1) return;

    unsigned src = g_dma_pos & (DMA_SAMPLES - 1);
    int i;
    for (i = 0; i < SUBMIT_SAMPLES; i++)
        g_sub_buf[i] = g_dma_buf[(src + i) & (DMA_SAMPLES - 1)];

    g_dma_pos  += SUBMIT_SAMPLES;
    g_submit_us = sys_get_micros();
    sys_audio_write(g_sub_buf, SUBMIT_BYTES, SND_SPEED);
}

/* ======================================================================
 *  CD AUDIO (stubs)
 * ====================================================================== */

void CDAudio_Play(int track, qboolean looping) { (void)track; (void)looping; }
void CDAudio_Stop(void) { }
void CDAudio_Resume(void) { }
void CDAudio_Update(void) { }
int  CDAudio_Init(void)   { return 0; }
void CDAudio_Shutdown(void) { }

/* ======================================================================
 *  NETWORK (stubs -- single-player only)
 * ====================================================================== */

netadr_t net_from;
sizebuf_t net_message;
byte net_message_buffer[MAX_MSGLEN];

static netadr_t net_local_adr;

/* Loopback for local server connection */
#define MAX_LOOPBACK 4
typedef struct {
    byte data[MAX_MSGLEN];
    int  datalen;
} loopmsg_t;

typedef struct {
    loopmsg_t msgs[MAX_LOOPBACK];
    int get, send;
} loopback_t;

static loopback_t loopbacks[2];

static qboolean NET_GetLoopPacket(netsrc_t sock, netadr_t *from, sizebuf_t *message)
{
    loopback_t *loop = &loopbacks[sock];
    if (loop->send - loop->get > MAX_LOOPBACK)
        loop->get = loop->send - MAX_LOOPBACK;
    if (loop->get >= loop->send)
        return false;
    int i = loop->get & (MAX_LOOPBACK - 1);
    loop->get++;
    memcpy(message->data, loop->msgs[i].data, loop->msgs[i].datalen);
    message->cursize = loop->msgs[i].datalen;
    memset(from, 0, sizeof(*from));
    from->type = NA_LOOPBACK;
    return true;
}

static void NET_SendLoopPacket(netsrc_t sock, int length, void *data, netadr_t to)
{
    (void)to;
    loopback_t *loop = &loopbacks[sock ^ 1];
    int i = loop->send & (MAX_LOOPBACK - 1);
    loop->send++;
    memcpy(loop->msgs[i].data, data, length);
    loop->msgs[i].datalen = length;
}

void NET_Init(void) { }
void NET_Shutdown(void) { }
void NET_Config(qboolean multiplayer) { (void)multiplayer; }

qboolean NET_GetPacket(netsrc_t sock, netadr_t *from, sizebuf_t *message)
{
    return NET_GetLoopPacket(sock, from, message);
}

void NET_SendPacket(netsrc_t sock, int length, void *data, netadr_t to)
{
    if (to.type == NA_LOOPBACK) {
        NET_SendLoopPacket(sock, length, data, to);
        return;
    }
    /* No real network */
}

qboolean NET_CompareAdr(netadr_t a, netadr_t b)
{
    if (a.type != b.type) return false;
    if (a.type == NA_LOOPBACK) return true;
    if (a.type == NA_IP)
        return (memcmp(a.ip, b.ip, 4) == 0 && a.port == b.port);
    return false;
}

qboolean NET_CompareBaseAdr(netadr_t a, netadr_t b)
{
    if (a.type != b.type) return false;
    if (a.type == NA_LOOPBACK) return true;
    if (a.type == NA_IP)
        return (memcmp(a.ip, b.ip, 4) == 0);
    return false;
}

qboolean NET_IsLocalAddress(netadr_t adr)
{
    return NET_CompareAdr(adr, net_local_adr);
}

char *NET_AdrToString(netadr_t a)
{
    static char s[64];
    if (a.type == NA_LOOPBACK) {
        strcpy(s, "loopback");
    } else {
        sprintf(s, "%d.%d.%d.%d:%d", a.ip[0], a.ip[1], a.ip[2], a.ip[3],
                (int)a.port);
    }
    return s;
}

qboolean NET_StringToAdr(char *s, netadr_t *a)
{
    (void)s;
    memset(a, 0, sizeof(*a));
    a->type = NA_LOOPBACK;
    return true;
}

void NET_Sleep(int msec)
{
    if (msec > 0) sys_sleep_ms((unsigned long)msec);
}

/* ======================================================================
 *  MISC (strlwr for Q2 compatibility)
 * ====================================================================== */

char *strlwr(char *s)
{
    char *p = s;
    while (*p) {
        if (*p >= 'A' && *p <= 'Z') *p += 32;
        p++;
    }
    return s;
}

/* ======================================================================
 *  ENTRY POINT
 * ====================================================================== */

int main(void)
{
    char *argv[] = {
        "quake2",
        "+set", "basedir", "GAMES/QUAKE2",
        "+set", "sw_mode", "0",        /* 320x240 */
        "+set", "vid_ref",  "soft",
        "+set", "s_khz", "11",
        "+menu_main",                  /* skip intro cinematic demo loop */
        NULL
    };
    int argc = 0;
    while (argv[argc]) argc++;

    Qcommon_Init(argc, argv);

    int oldtime = Sys_Milliseconds();
    for (;;) {
        int newtime = Sys_Milliseconds();
        int time = newtime - oldtime;
        if (time < 1) time = 1;
        if (time > 200) time = 200;
        Qcommon_Frame(time);
        oldtime = newtime;
        sys_sleep_ms(20);
    }
    return 0;
}
