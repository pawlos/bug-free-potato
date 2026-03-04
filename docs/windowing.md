# Windowing Subsystem

Added in commit `58f4afc`. Provides a simple multi-window manager for userspace programs running inside the kernel's QEMU framebuffer.

## Overview

- Up to **8 concurrent windows** (`MAX_WINDOWS = 8`)
- Each window has a 1-px border and a 16-px title bar (chrome)
- One window has **focus** at a time; only the focused window receives keyboard events
- Mouse **click-to-focus**: clicking any window's outer frame switches focus
- Windows are owned by a task; a task may own at most one window

## Key files

| File | Role |
|---|---|
| `src/include/window.h` | `Window` struct, `WindowManager` class, constants, event encoding |
| `src/arch/x86_64/window.cpp` | Full implementation |
| `src/arch/x86_64/device/mouse.cpp` | Click-to-focus edge detection |
| `src/arch/x86_64/device/keyboard.cpp` | Routes key events to `WindowManager::push_key_event()` |
| `src/arch/x86_64/idt.cpp` | Syscall handlers that call `translate_rect()`/`translate_point()` |

## Constants (window.h)

```cpp
constexpr pt::uint32_t MAX_WINDOWS  = 8;
constexpr pt::uint32_t TITLE_BAR_H  = 16;   // pixels; matches PSF1 glyph height
constexpr pt::uint32_t BORDER_W     = 1;    // 1-pixel border on all sides
constexpr pt::uint32_t EVENT_CAP    = 32;   // per-window key-event ring capacity
constexpr pt::uint32_t INVALID_WID  = 0xFFFFFFFF;  // "no window" sentinel
constexpr pt::uint64_t WEV_KEY_PRESS_BIT = 0x100;  // set when key is pressed
```

## Window struct

```cpp
struct Window {
    pt::uint32_t id;              // slot index 0-7
    pt::uint32_t owner_task_id;   // owning task
    bool         active;          // true when allocated

    // Outer frame (includes chrome)
    pt::uint32_t screen_x, screen_y;  // top-left of outer frame
    pt::uint32_t total_w, total_h;    // full size with chrome

    // Client area (screen-absolute, pre-computed)
    pt::uint32_t client_ox, client_oy;  // top-left of drawable area
    pt::uint32_t client_w,  client_h;   // drawable size

    // Per-window key-event ring
    pt::uint64_t events[EVENT_CAP];
    pt::uint32_t ev_read, ev_write;

    // Text cursor (characters, not pixels) – used by SYS_WRITE stdout routing
    pt::uint32_t text_col, text_row;
};
```

## WindowManager API

All methods are static (singleton pattern).

```cpp
// Initialization
static void initialize();

// Lifecycle
static pt::uint32_t create_window(x, y, w, h, owner_task_id);
//   x,y,w,h are client-area dimensions in screen pixels
//   Returns slot index (0-7) or INVALID_WID on failure.
static void destroy_window(pt::uint32_t wid);

// Coordinate translation
static bool translate_rect(wid, rx, ry, rw, rh,
                            &sx, &sy, &sw, &sh);  // window-rel → screen-abs, clips
static bool translate_point(wid, rx, ry, &sx, &sy);

// Events
static void         push_key_event(pt::uint64_t ev);  // routes to focused window
static pt::uint64_t poll_event(pt::uint32_t wid);     // 0 = empty

// Focus
static void         set_focus(pt::uint32_t wid);
static pt::uint32_t window_at(pt::uint32_t px, pt::uint32_t py);  // hit-test

// Text output (used by SYS_WRITE when task has a window)
static void put_char(pt::uint32_t wid, char c);

static pt::uint32_t focused_id;  // currently focused window ID
```

## Coordinate systems

```
Screen-absolute (0,0 = top-left of framebuffer)
┌─────────────────────────────┐
│  outer frame (screen_x/y)   │  ← 1-px border (0x404040)
│ ┌─────────────────────────┐ │
│ │  title bar (TITLE_BAR_H)│ │  ← blue (focused) / dark-gray (unfocused)
│ ├─────────────────────────┤ │
│ │  client area            │ │  ← userspace draws here
│ │  (0,0) in window-coords │ │
│ └─────────────────────────┘ │
└─────────────────────────────┘
```

Userspace syscalls (`SYS_FILL_RECT`, `SYS_DRAW_TEXT`, `SYS_DRAW_PIXELS`) always use **window-relative** coordinates. The kernel translates them to screen-absolute before calling into the framebuffer.

## Window titles

Windows can display a title in their title bar via `SYS_SET_WINDOW_TITLE` (syscall 37). The title is stored as a null-terminated string (max 31 characters) in the `Window` struct and rendered over the title bar chrome using the PSF1 font.

```c
long wid = sys_create_window(100, 80, 400, 300);
sys_set_window_title(wid, "My App");
```

The title is drawn in white text on the title bar background colour (blue when focused, gray when unfocused). It is automatically repainted on focus changes.

## Chrome colours

| State | Title bar colour |
|---|---|
| Focused | `0x0055AA` (blue) |
| Unfocused | `0x303030` (dark gray) |
| Border (always) | `0x404040` |

## Key-event encoding

A 64-bit event word:

```
bits 63..9   reserved (0)
bit  8       1 = key pressed, 0 = key released  (WEV_KEY_PRESS_BIT)
bits 7..0    PS/2 set-1 scancode
```

`0` is the empty-queue sentinel.

Helper:
```cpp
static inline pt::uint64_t wev_make_key(pt::uint8_t sc, bool pressed) {
    return (pt::uint64_t)sc | (pressed ? WEV_KEY_PRESS_BIT : 0);
}
```

## Click-to-focus (mouse.cpp)

Rising-edge detection on the left mouse button:

```cpp
bool left_clicked = left_button_pressed && !prev_left_button;
prev_left_button  = left_button_pressed;
if (left_clicked) {
    pt::uint32_t hit = WindowManager::window_at(mouse.pos_x, mouse.pos_y);
    if (hit != INVALID_WID)
        WindowManager::set_focus(hit);
}
```

`window_at()` tests against each active window's **outer frame** (including chrome), so clicking the title bar also focuses the window.

## stdout routing

`SYS_WRITE(fd=1)` in `idt.cpp` checks whether the calling task owns a window:

```cpp
if (t && t->window_id != INVALID_WID)
    WindowManager::put_char(t->window_id, c);   // goes to window
else
    fbterm.put_char(c);                          // goes to full-screen terminal
```

`put_char()` handles `\n`, `\r`, `\t` (4-space aligned), line-wrap, and scrolling.

## Lifecycle example

```
1. Task calls SYS_CREATE_WINDOW(cx, cy, cw, ch)
       → WindowManager allocates first inactive slot
       → New window becomes focused (previous dimmed)
       → Returns window ID

2. All drawing syscalls clip to client area automatically

3. Keyboard events only reach the focused window's event ring

4. Mouse click on any window's outer frame → set_focus()

5. Task calls SYS_DESTROY_WINDOW(wid)
       → Screen region erased
       → window_id in Task struct reset to INVALID_WID
```

## Limitations / known constraints

- One window per task (enforced by `SYS_CREATE_WINDOW`).
- Windows are not movable or resizable after creation.
- No z-ordering; windows are painted in slot order (last slot on top) when chrome is redrawn.
- `EVENT_CAP = 32` — fast typists could overflow the ring if the task doesn't drain it.
