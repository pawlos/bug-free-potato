# Syscall Reference

All syscalls use `int 0x80` with the following register convention:

| Register | Role |
|---|---|
| `rax` | Syscall number (in) / return value (out) |
| `rdi` | arg1 |
| `rsi` | arg2 |
| `rdx` | arg3 |
| `rcx` | arg4 |
| `r8` | arg5 |

Syscalls are dispatched in `src/impl/x86_64/idt.cpp`.
C wrappers live in `src/userspace/libc/syscall.h`.

---

## Process & I/O

### SYS_EXIT (1)
Terminate the calling task with an exit code.

```
rdi = exit_code (int)
```
Does not return. The parent is unblocked if it is waiting in `SYS_WAITPID`.

---

### SYS_WRITE (2)
Write bytes to a file descriptor.

```
rdi = fd
rsi = buf (const char*)
rdx = len
→ rax = bytes written, or -1
```

`fd=1` (stdout): if the calling task owns a window, text is routed to `WindowManager::put_char()`; otherwise to the full-screen framebuffer terminal.

---

### SYS_READ (3)
Read bytes from a file descriptor (blocking for pipes).

```
rdi = fd
rsi = buf (char*)
rdx = len
→ rax = bytes read, or -1
```

---

### SYS_FORK (4)
Fork the calling task. Returns twice.

```
→ rax = child task-id in parent, 0 in child, -1 on error
```

---

### SYS_EXEC (5)
Replace the current task image with an ELF binary from disk.

```
rdi = path (const char*)
→ does not return on success; rax = -1 on failure
```

---

### SYS_WAITPID (6)
Block until a child task exits.

```
rdi = child_task_id
→ rax = child exit code (low 32 bits)
```

---

### SYS_OPEN (7)
Open a file on the FAT filesystem.

```
rdi = path (const char*)
rsi = flags (O_RDONLY=0, O_RDWR=2, O_CREATE=0x40)
→ rax = fd, or -1
```

---

### SYS_LSEEK (8)
Set the read/write position of an open file descriptor.

```
rdi = fd
rsi = offset
rdx = whence (SEEK_SET=0, SEEK_CUR=1, SEEK_END=2)
→ rax = new position, or -1
```

---

### SYS_CREATE (9)
Create a new file.

```
rdi = path (const char*)
→ rax = fd, or -1
```

---

### SYS_PIPE (10)
Create a kernel pipe (anonymous, in-memory ring buffer, 512 bytes).

```
rdi = pipefd[2]  (int[2], output: pipefd[0]=read end, pipefd[1]=write end)
→ rax = 0, or -1
```

---

## Graphics

All graphics syscalls accept **window-relative coordinates** when the calling task owns a window. The kernel translates them to screen-absolute and clips to the client area before touching the framebuffer.

### SYS_FILL_RECT (11)
Fill a rectangle with a solid colour.

```
rdi = x
rsi = y
rdx = w
rcx = h
r8  = 0x00RRGGBB colour
→ rax = 0
```

---

### SYS_DRAW_TEXT (12)
Draw a null-terminated string at pixel coordinates.

```
rdi = x
rsi = y
rdx = str (const char*)
rcx = fg colour (0x00RRGGBB)
r8  = bg colour (0x00RRGGBB)
→ rax = 0
```

Uses the PSF1 bitmap font loaded by the kernel.

---

### SYS_DRAW_PIXELS (20)
Blit a raw RGB24 pixel buffer.

```
rdi = buf (const uint8_t*, 3 bytes per pixel, row-major, no padding)
rsi = x
rdx = y
rcx = w
r8  = h
→ rax = 0
```

Buffer size must be `w * h * 3` bytes. Coordinates are window-relative.

---

### SYS_FB_WIDTH (21)
```
→ rax = framebuffer width in pixels
```

### SYS_FB_HEIGHT (22)
```
→ rax = framebuffer height in pixels
```

---

## Windowing

### SYS_CREATE_WINDOW (24)
Allocate a window for the calling task (one per task max).

```
rdi = cx   (client-area screen x)
rsi = cy   (client-area screen y)
rdx = cw   (client-area width)
rcx = ch   (client-area height)
→ rax = window ID (0-7), or -1
```

The window immediately becomes focused. The outer frame (border + title bar) is added by the kernel around the requested client dimensions.

---

### SYS_DESTROY_WINDOW (25)
Release the calling task's window.

```
rdi = wid
→ rax = 0, or -1
```

The screen region is erased and `task->window_id` is reset to `INVALID_WID`.

---

### SYS_GET_WINDOW_EVENT (26)
Non-blocking poll for a keyboard event on a window.

```
rdi = wid
→ rax = encoded event (64-bit), or 0 if empty
```

Event encoding:
```
bit 8       1 = pressed, 0 = released
bits 7..0   PS/2 set-1 scancode
```

---

## Scheduling

### SYS_YIELD (15)
Voluntarily give up the CPU. Returns in the next scheduler round.

```
→ rax = 0
```

---

### SYS_SLEEP (23)
Block the calling task for at least `ms` milliseconds.

```
rdi = ms (unsigned long)
→ rax = 0  (returns after deadline)
```

Resolution is ~20 ms (one timer tick at 50 Hz). If `ms < 20` the task blocks for one tick. If `ms = 0` the call is a no-op.

The scheduler wakes sleeping tasks on the timer interrupt and immediately triggers a context switch if any task became READY, so wake latency is at most one tick.

---

### SYS_GET_TICKS (16)
```
→ rax = monotonic tick counter (64-bit)
```

Increments at 50 Hz (every 20 ms).

---

### SYS_GET_TIME (17)
Read the RTC.

```
rdi = struct tm* (output)
→ rax = 0
```

---

## Memory

### SYS_MMAP (18)
Map anonymous memory pages into the task's address space.

```
rdi = addr hint (or 0)
rsi = length
rdx = prot flags
rcx = map flags
→ rax = mapped address, or MAP_FAILED (-1)
```

---

### SYS_MUNMAP (19)
Unmap previously mapped pages.

```
rdi = addr
rsi = length
→ rax = 0, or -1
```

---

## Syscall number summary

| # | Name | Brief |
|---|---|---|
| 1 | SYS_EXIT | Exit task |
| 2 | SYS_WRITE | Write to fd |
| 3 | SYS_READ | Read from fd |
| 4 | SYS_FORK | Fork task |
| 5 | SYS_EXEC | Exec ELF |
| 6 | SYS_WAITPID | Wait for child |
| 7 | SYS_OPEN | Open file |
| 8 | SYS_LSEEK | Seek file |
| 9 | SYS_CREATE | Create file |
| 10 | SYS_PIPE | Create pipe |
| 11 | SYS_FILL_RECT | Draw rectangle |
| 12 | SYS_DRAW_TEXT | Draw text |
| 15 | SYS_YIELD | Yield CPU |
| 16 | SYS_GET_TICKS | Tick counter |
| 17 | SYS_GET_TIME | RTC time |
| 18 | SYS_MMAP | Map memory |
| 19 | SYS_MUNMAP | Unmap memory |
| 20 | SYS_DRAW_PIXELS | Blit pixel buffer |
| 21 | SYS_FB_WIDTH | Framebuffer width |
| 22 | SYS_FB_HEIGHT | Framebuffer height |
| 23 | SYS_SLEEP | Sleep ms |
| 24 | SYS_CREATE_WINDOW | Create window |
| 25 | SYS_DESTROY_WINDOW | Destroy window |
| 26 | SYS_GET_WINDOW_EVENT | Poll window event |
| 27 | SYS_GET_KEY_EVENT | Poll global key event |
