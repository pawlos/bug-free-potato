#pragma once

/* Syscall numbers — must match src/include/syscall.h in the kernel. */
#define SYS_WRITE     0   /* rdi=fd, rsi=buf ptr, rdx=count; fd=1→stdout  */
#define SYS_EXIT      1   /* rdi = exit code                               */
#define SYS_READ_KEY  2   /* returns char (0-255) or (long)-1 if no key    */
#define SYS_OPEN      3   /* rdi = filename ptr; returns fd or -1          */
#define SYS_READ      4   /* rdi=fd, rsi=buf, rdx=count; returns bytes     */
#define SYS_CLOSE     5   /* rdi = fd; returns 0 or -1                     */
#define SYS_MMAP      6   /* rdi = size; returns virt addr or -1           */
#define SYS_MUNMAP    7   /* rdi = ptr; returns 0 or -1                    */
#define SYS_YIELD     8   /* cooperative yield                             */
#define SYS_GET_TICKS 9   /* returns current tick count                    */
#define SYS_GET_TIME  10  /* returns (hours<<8)|minutes                    */
#define SYS_FILL_RECT 11  /* rdi=x, rsi=y, rdx=w, rcx=h, r8=0xRRGGBB     */
#define SYS_DRAW_TEXT 12  /* rdi=x, rsi=y, rdx=str, rcx=fg, r8=bg         */
#define SYS_FB_WIDTH  13  /* returns framebuffer width in pixels           */
#define SYS_FORK      14  /* clone task; returns child id (parent) or 0 (child) */
#define SYS_EXEC      15  /* rdi=filename, rsi=argc, rdx=argv_ptr; replace image; returns 0 or -1 */
#define SYS_WAITPID   16  /* rdi=child_id, rsi=exit_code_ptr; returns 0 or -1   */
#define SYS_PIPE        17  /* rdi=int[2] ptr; fills [0]=rd_fd [1]=wr_fd          */
#define SYS_LSEEK       18  /* rdi=fd, rsi=offset, rdx=whence; returns new pos    */
#define SYS_FB_HEIGHT   19  /* returns framebuffer height in pixels               */
#define SYS_DRAW_PIXELS   20  /* rdi=buf, rsi=x, rdx=y, rcx=w, r8=h — blit pixels  */
/* Returns (scancode | 0x100) if pressed, scancode if released, -1 if empty. */
#define SYS_GET_KEY_EVENT 21  /* no args                                            */
#define SYS_CREATE        22  /* rdi=filename; create/truncate for writing; returns fd or -1 */
#define SYS_SLEEP            23  /* rdi=milliseconds; block until elapsed; returns 0            */
#define SYS_CREATE_WINDOW    24  /* rdi=cx, rsi=cy, rdx=cw, rcx=ch; returns wid or -1          */
#define SYS_DESTROY_WINDOW   25  /* rdi=wid; returns 0 or -1                                   */
#define SYS_GET_WINDOW_EVENT 26  /* rdi=wid; returns encoded event (0 if empty)                */
#define SYS_READDIR          27  /* rdi=idx, rsi=name_buf, rdx=size_ptr; 1=ok, 0=done          */
#define SYS_MEM_FREE         28  /* () → free heap bytes                                       */
#define SYS_DISK_SIZE        29  /* () → total disk bytes                                      */
#define SYS_REMOVE           30  /* rdi=filename; delete file; returns 0 or -1                 */
#define SYS_SOCK_CONNECT     31  /* rdi=dst_ip, rsi=dst_port; returns fd or -1                 */
#define SYS_GET_MOUSE_EVENT  32  /* () → encoded event or -1 if queue empty
                                    bits[7:0]=dx(int8), bits[15:8]=dy(int8,+up),
                                    bit[16]=left_button, bit[17]=right_button          */
#define SYS_GET_MICROS       33  /* () → microseconds since boot (uint64)              */
#define SYS_AUDIO_WRITE      34  /* rdi=data, rsi=bytes, rdx=rate; 1=ok 0=busy -1=none */
#define SYS_AUDIO_PLAYING    35  /* () → 1=playing, 0=idle, -1=no AC97                 */
#define SYS_WRITE_SERIAL     36  /* rdi=buf, rsi=len; write raw bytes to COM1 serial   */

typedef unsigned long size_t;
typedef long          ssize_t;

/* ── raw syscall stubs ─────────────────────────────────────────────────── */

static inline long __sc0(long nr)
{
    long ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(nr) : "memory");
    return ret;
}

static inline long __sc1(long nr, long a1)
{
    long ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(nr), "D"(a1) : "memory");
    return ret;
}

static inline long __sc2(long nr, long a1, long a2)
{
    long ret;
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(nr), "D"(a1), "S"(a2)
                     : "memory");
    return ret;
}

static inline long __sc3(long nr, long a1, long a2, long a3)
{
    long ret;
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(nr), "D"(a1), "S"(a2), "d"(a3)
                     : "memory");
    return ret;
}

static inline long __sc5(long nr, long a1, long a2, long a3, long a4, long a5)
{
    long ret;
    register long _a4 __asm__("rcx") = a4;
    register long _a5 __asm__("r8")  = a5;
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(nr), "D"(a1), "S"(a2), "d"(a3), "r"(_a4), "r"(_a5)
                     : "memory");
    return ret;
}

/* ── high-level wrappers ───────────────────────────────────────────────── */

static inline long  sys_write(int fd, const void *buf, size_t n)
    { return __sc3(SYS_WRITE, fd, (long)buf, (long)n); }

static inline void  sys_exit(int code)
    { __sc1(SYS_EXIT, code); }

static inline long  sys_read_key(void)
    { return __sc0(SYS_READ_KEY); }

static inline int   sys_open(const char *name)
    { return (int)__sc1(SYS_OPEN, (long)name); }

static inline long  sys_read(int fd, void *buf, size_t n)
    { return __sc3(SYS_READ, fd, (long)buf, (long)n); }

static inline int   sys_close(int fd)
    { return (int)__sc1(SYS_CLOSE, fd); }

static inline void *sys_mmap(size_t size)
    { return (void *)__sc1(SYS_MMAP, (long)size); }

static inline void  sys_munmap(void *ptr)
    { __sc1(SYS_MUNMAP, (long)ptr); }

static inline void  sys_yield(void)
    { __sc0(SYS_YIELD); }

static inline long  sys_get_ticks(void)
    { return __sc0(SYS_GET_TICKS); }

static inline long  sys_get_time(void)
    { return __sc0(SYS_GET_TIME); }

static inline void  sys_fill_rect(long x, long y, long w, long h, long rgb)
    { __sc5(SYS_FILL_RECT, x, y, w, h, rgb); }

static inline void  sys_draw_text(long x, long y, const char *s, long fg, long bg)
    { __sc5(SYS_DRAW_TEXT, x, y, (long)s, fg, bg); }

static inline long  sys_fb_width(void)
    { return __sc0(SYS_FB_WIDTH); }

static inline long  sys_fork(void)
    { return __sc0(SYS_FORK); }

static inline long  sys_exec(const char* filename, int argc,
                              const char* const* argv)
    { return __sc3(SYS_EXEC, (long)filename, (long)argc, (long)argv); }

static inline long  sys_waitpid(long child_pid, int* exit_code)
    { return __sc2(SYS_WAITPID, child_pid, (long)exit_code); }

static inline long  sys_pipe(int pipefd[2])
    { return __sc1(SYS_PIPE, (long)pipefd); }

static inline long  sys_lseek(int fd, long offset, int whence)
    { return __sc3(SYS_LSEEK, fd, offset, whence); }

static inline long  sys_fb_height(void)
    { return __sc0(SYS_FB_HEIGHT); }

static inline void  sys_draw_pixels(const void *buf, long x, long y, long w, long h)
    { __sc5(SYS_DRAW_PIXELS, (long)buf, x, y, w, h); }

/* Returns (scancode | 0x100) if key pressed, bare scancode if released,
   or -1 when the event queue is empty.
   PS/2 set-1 scancodes: 0x01=Esc, 0x1C=Enter, 0x39=Space, 0x48=Up, 0x50=Down,
                         0x4B=Left, 0x4D=Right, 0x1D=LCtrl, 0x38=LAlt, etc. */
static inline long  sys_get_key_event(void)
    { return __sc0(SYS_GET_KEY_EVENT); }

static inline int   sys_create(const char *name)
    { return (int)__sc1(SYS_CREATE, (long)name); }

static inline void  sys_sleep_ms(unsigned long ms)
    { __sc1(SYS_SLEEP, (long)ms); }

static inline long sys_create_window(long cx, long cy, long cw, long ch)
{
    register long _ch    __asm__("rcx") = ch;
    register long _flags __asm__("r8")  = 0;   /* no flags = normal window with chrome */
    long ret;
    __asm__ volatile("int $0x80"
        : "=a"(ret)
        : "a"((long)SYS_CREATE_WINDOW), "D"(cx), "S"(cy), "d"(cw), "r"(_ch), "r"(_flags)
        : "memory");
    return ret;
}

static inline long sys_destroy_window(long wid)
    { return __sc1(SYS_DESTROY_WINDOW, wid); }

static inline long sys_get_window_event(long wid)
    { return __sc1(SYS_GET_WINDOW_EVENT, wid); }

static inline long sys_readdir(int idx, char* name, unsigned int* size)
    { return __sc3(SYS_READDIR, (long)idx, (long)name, (long)size); }

static inline long sys_mem_free(void)
    { return __sc0(SYS_MEM_FREE); }

static inline long sys_disk_size(void)
    { return __sc0(SYS_DISK_SIZE); }

static inline long sys_remove(const char* filename)
    { return __sc1(SYS_REMOVE, (long)filename); }

static inline long sys_sock_connect(unsigned int ip, unsigned short port)
    { return __sc2(SYS_SOCK_CONNECT, (long)ip, (long)port); }

/* Returns -1 if the event queue is empty, otherwise an encoded event:
     int dx    = (signed char)(ev & 0xFF);
     int dy    = (signed char)((ev >> 8) & 0xFF);  // positive = up
     int left  = (ev >> 16) & 1;
     int right = (ev >> 17) & 1;                                      */
static inline long sys_get_mouse_event(void)
    { return __sc0(SYS_GET_MOUSE_EVENT); }

/* Returns microseconds since boot — sub-tick precision via PIT latch. */
static inline unsigned long long sys_get_micros(void)
    { return (unsigned long long)__sc0(SYS_GET_MICROS); }

/* Submit raw 16-bit signed stereo PCM to the AC97 hardware.
   Returns 1 if accepted and DMA started, 0 if AC97 is already playing
   (data dropped — retry next tic), -1 if no AC97 hardware. */
static inline long sys_audio_write(const void *data, unsigned long bytes,
                                   unsigned int rate)
    { return __sc3(SYS_AUDIO_WRITE, (long)data, (long)bytes, (long)rate); }

/* Returns 1 if AC97 is currently playing, 0 if idle, -1 if absent. */
static inline long sys_audio_is_playing(void)
    { return __sc0(SYS_AUDIO_PLAYING); }

/* Write raw bytes to the kernel COM1 serial port (appears in QEMU -serial stdio). */
static inline long sys_write_serial(const char *buf, size_t n)
    { return __sc2(SYS_WRITE_SERIAL, (long)buf, (long)n); }
