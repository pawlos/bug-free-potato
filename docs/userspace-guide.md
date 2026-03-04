# Writing Userspace Programs

## Overview

Userspace programs are compiled to **x86_64 ELF64** binaries linked against a minimal libc shim. They run at CPL=3 (ring 3) and communicate with the kernel through `int 0x80` syscalls.

## Directory layout

```
src/userspace/
├── libc/               ← libc shim (stdio, stdlib, string, math, syscall wrappers)
│   ├── syscall.h       ← inline syscall wrappers
│   ├── stdio.h / stdio.c
│   ├── stdlib.h / stdlib.c
│   ├── string.h / string.c
│   └── math/           ← libm (for float/double)
├── hello/              ← minimal "hello world"
├── wm_test/            ← window manager demo
├── fork_test/          ← fork + exec + waitpid demo
├── pipe_test/          ← pipe IPC demo
├── sleep_test/         ← sleep syscall demo
├── doom/               ← Doom port
└── ...
```

## Build system

Each program has its own directory and is compiled by the top-level `Makefile`. To add a new program:

1. Create `src/userspace/<name>/main.c` (or `main.cpp`).
2. Add the binary to the Makefile's userspace build rules (copy an existing entry).
3. The resulting ELF is placed in `dist/userspace/` and embedded in the FAT disk image.

## Minimal program

```c
// src/userspace/hello/main.c
#include "../libc/stdio.h"

int main(void) {
    puts("hello from ring 3");
    return 0;
}
```

Run from the kernel shell:
```
> exec hello.elf
```

## Syscall wrappers

`syscall.h` provides inline wrappers. Use these rather than raw `int 0x80`.

```c
#include "../libc/syscall.h"

// I/O
sys_write(fd, buf, len);
sys_read(fd, buf, len);

// Files
int fd = sys_open("readme.txt", 0);   // O_RDONLY
sys_lseek(fd, 0, SEEK_SET);

// Process
sys_yield();
sys_sleep_ms(500);           // block for 500 ms
sys_exit(0);

long pid = sys_fork();
if (pid == 0) { /* child */ }
sys_waitpid(pid);

// Framebuffer (direct, non-windowed)
long w = sys_fb_width();
long h = sys_fb_height();
sys_fill_rect(x, y, w, h, 0xFF0000L);          // RGB
sys_draw_text(x, y, "hello", 0xFFFFFF, 0x000000);
sys_draw_pixels(rgb24_buf, x, y, pw, ph);
```

## Writing a windowed program

Windows provide an isolated client area for drawing and keyboard input.

```c
#include "../libc/syscall.h"
#include "../libc/stdio.h"

#define WIN_W 400
#define WIN_H 300

int main(void) {
    // 1. Create window (client-area dimensions)
    long wid = sys_create_window(100, 80, WIN_W, WIN_H);
    if (wid < 0) { puts("no window"); return 1; }

    // 2. Draw background and initial content (window-relative coords)
    sys_fill_rect(0, 0, WIN_W, WIN_H, 0x001133L);
    sys_draw_text(4, 4, "My Window - press Q to quit", 0xFFFFFF, 0x001133);

    // 3. Event loop
    while (1) {
        long ev = sys_get_window_event(wid);
        if (ev == 0) { sys_yield(); continue; }   // empty — yield to avoid busy-spin

        int pressed  = (ev & 0x100) != 0;
        int scancode = (int)(ev & 0xFF);

        if (!pressed) continue;   // ignore key-release events

        if (scancode == 0x10) break;  // Q key (PS/2 set-1)
    }

    // 4. Clean up
    sys_destroy_window(wid);
    return 0;
}
```

### Important rules

- Call `sys_create_window()` **before** any framebuffer drawing if you want all output clipped to the window. Drawing before window creation goes to the global terminal.
- All coordinates passed to `SYS_FILL_RECT`, `SYS_DRAW_TEXT`, and `SYS_DRAW_PIXELS` are **window-relative** (0,0 = client top-left) when your task owns a window.
- A task can own at most **one window**. `sys_create_window()` returns `-1` if you already have one.
- `sys_get_window_event()` is non-blocking. If the queue is empty it returns `0`. Always yield when idle to avoid consuming 100% CPU.

## Rendering pixels (e.g. a game)

```c
#define W 320
#define H 240
static unsigned char fb[W * H * 3];  // RGB24

void render_frame(void) {
    // fill fb with pixel data (R, G, B bytes per pixel)
    sys_draw_pixels(fb, 0, 0, W, H);
}
```

For a full example see the Doom platform layer at `src/userspace/doom/doomgeneric_potato.c`.

## PS/2 set-1 scancodes (common keys)

| Key | Pressed | Released |
|---|---|---|
| Esc | `0x01` | `0x81` |
| 1–0 | `0x02`–`0x0B` | `0x82`–`0x8B` |
| Q | `0x10` | `0x90` |
| W | `0x11` | `0x91` |
| A | `0x1E` | `0x9E` |
| S | `0x1F` | `0x9F` |
| D | `0x20` | `0xA0` |
| Space | `0x39` | `0xB9` |
| Enter | `0x1C` | `0x9C` |
| Left | `0x4B` | `0xCB` |
| Right | `0x4D` | `0xCD` |
| Up | `0x48` | `0xC8` |
| Down | `0x50` | `0xD0` |

The event word from `sys_get_window_event()` has bit 8 set for pressed and clear for released. The scancode is always the **make** (pressed) code regardless of direction.

## stdout from a windowed task

`printf()` / `puts()` write to `fd=1`, which the kernel routes to `WindowManager::put_char()` when your task owns a window. This gives you automatic line-wrapping and scrolling inside the window without any extra syscalls.

## Sleep

```c
#include "../libc/unistd.h"

sleep(1);        // 1 second
usleep(250000);  // 250 ms
```

Or directly:

```c
sys_sleep_ms(250);
```

Resolution is ~20 ms (one PIT tick at 50 Hz).

## Fork + exec pattern

```c
long child = sys_fork();
if (child == 0) {
    sys_exec("child.elf");   // replaces child image
    sys_exit(1);             // exec failed
}
int code = sys_waitpid(child);
```

## Pipes

```c
int pfd[2];
sys_pipe(pfd);   // pfd[0] = read, pfd[1] = write

long w = sys_fork();
if (w == 0) {
    sys_write(pfd[1], "hello", 5);
    sys_exit(0);
}
char buf[32] = {0};
sys_read(pfd[0], buf, 31);   // blocks until data arrives
sys_waitpid(w);
```

## Libc coverage

| Header | Available |
|---|---|
| `stdio.h` | `printf`, `puts`, `putchar`, `sprintf`, `snprintf`, `fopen`, `fread`, `fwrite`, `fclose`, `fseek`, `ftell`, `feof` |
| `stdlib.h` | `malloc`, `calloc`, `realloc`, `free`, `atoi`, `atol`, `itoa`, `exit`, `abs` |
| `string.h` | `memcpy`, `memset`, `memcmp`, `strlen`, `strcpy`, `strncpy`, `strcmp`, `strncmp`, `strcat`, `strchr`, `strstr` |
| `math.h` | Basic trig, `sqrt`, `fabs`, `floor`, `ceil`, `pow`, `log` (libm port) |
| `unistd.h` | `sleep`, `usleep` |

Anything not listed above is not available. Avoid POSIX APIs not in this table.
