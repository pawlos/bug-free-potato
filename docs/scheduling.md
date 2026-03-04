# Task Scheduler

## Architecture

The scheduler lives in `src/arch/x86_64/task.cpp` (class `TaskScheduler`).
Scheduling is **preemptive, round-robin**, driven by the PIT timer at **50 Hz** (20 ms per tick).

Context switches happen inside the timer IRQ handler (`preempt()`), which is invoked from the IDT stub at `int 0x20`.

---

## Task states

```
TASK_READY    — runnable, waiting to be scheduled
TASK_RUNNING  — currently executing on the CPU
TASK_BLOCKED  — waiting for an event (sleep, pipe read, waitpid)
TASK_DEAD     — exited, waiting for parent to collect exit code
```

---

## Task struct (relevant fields)

Defined in `src/include/task.h`:

```cpp
struct Task {
    pt::uint32_t  id;
    TaskState     state;

    // Kernel interrupt stack (16 KB)
    pt::uintptr_t kernel_stack_base;
    pt::size_t    kernel_stack_size;    // TASK_STACK_SIZE = 16384

    // Saved CPU context (PUSHALL frame pointer, updated on each preemption)
    pt::uintptr_t preempt_rsp;

    // Address space
    pt::uintptr_t cr3;                  // physical address of PML4 (0 = kernel default)
    bool          user_mode;
    pt::uintptr_t user_stack_base;      // ring-3 execution stack (16 KB)

    // FPU/SSE state (512 bytes, 16-byte aligned)
    pt::uint8_t   fxsave_area[512] __attribute__((aligned(16)));

    // Process hierarchy
    pt::uint32_t  parent_id;
    pt::uint32_t  waiting_for;          // child being waited on (BLOCKED state)
    int           exit_code;

    // Per-task file descriptors
    File          fd_table[MAX_FDS];

    // Window (INVALID_WID = 0xFFFFFFFF if none)
    pt::uint32_t  window_id;

    // Sleep deadline (absolute ticks; 0 = not sleeping)
    pt::uint64_t  sleep_deadline;
};
```

---

## Context switch

On each timer tick, `preempt()` does:

1. **Save current task state**: PUSHALL frame pointer stored in `task->preempt_rsp`; FPU state saved with `FXSAVE`.
2. **Wake sleeping tasks**: scan all tasks; any with `state==TASK_BLOCKED && sleep_deadline != 0 && ticks >= sleep_deadline` → mark `TASK_READY`, clear `sleep_deadline`.
3. **Pick next task**: round-robin over `TASK_READY` tasks. If the current task has not exhausted its quantum (`SCHEDULER_QUANTUM = 10` ticks = 200 ms) *and* no sleeping task just woke, stay with the current task.
4. **Restore next task**: load `cr3` (switches address space), restore FPU with `FXRESTOR`, POPALL, `iretq`.

```
SCHEDULER_QUANTUM = 10 ticks = 200 ms at 50 Hz
```

---

## Sleep / SYS_SLEEP

### Kernel API

```cpp
void TaskScheduler::sleep_task(pt::uint64_t ms);
```

Computes the deadline, blocks the task, and yields immediately:

```cpp
void TaskScheduler::sleep_task(pt::uint64_t ms) {
    if (ms == 0) return;
    Task* t         = &tasks[current_task_id];
    pt::uint64_t tk = ms / 20;          // 20 ms per tick
    if (tk == 0) tk = 1;                // minimum 1 tick
    t->sleep_deadline = get_ticks() + tk;
    t->state          = TASK_BLOCKED;
    task_yield();                       // int 0x81 → switch away immediately
}
```

### Wake path

Inside `preempt()` (runs every timer tick):

```cpp
for each task t:
    if (t.state == TASK_BLOCKED && t.sleep_deadline != 0) {
        if (get_ticks() >= t.sleep_deadline) {
            t.state         = TASK_READY;
            t.sleep_deadline = 0;
            any_woke        = true;
        }
    }

if (any_woke)
    force_switch = true;   // don't wait for quantum expiry
```

This means a sleeping task can wake up to **one full tick (20 ms) late** in the worst case (if the deadline falls just after a tick boundary). In practice, the average latency is ~10 ms.

### Userspace

```c
// libc wrappers (syscall.h / unistd.h)
void sleep(unsigned int seconds);      // calls SYS_SLEEP(seconds * 1000)
void usleep(unsigned long us);         // calls SYS_SLEEP(us / 1000), min 1 ms
```

### Example

```c
#include "syscall.h"

int main(void) {
    for (int i = 0; i < 5; i++) {
        puts("tick");
        sys_sleep_ms(500);   // block for ~500 ms
    }
    return 0;
}
```

---

## FPU / SSE per-task state

Each task has a 512-byte `fxsave_area` (16-byte aligned) for `FXSAVE`/`FXRSTOR`.

- FPU state is saved on every preemption and restored on every resume.
- SSE (`XMM0–XMM15`, `MXCSR`) is included automatically by `FXSAVE`.
- Enabled at boot via `CR4.OSFXSR` and `CR4.OSXMMEXCPT`.

This allows userspace programs to use `float`/`double` and SSE intrinsics freely.

---

## ELF task creation

```cpp
pt::uint32_t TaskScheduler::create_elf_task(const char* path);
```

Steps:

1. Load ELF from FAT filesystem into the **ELF staging area** (PA `0x18000000`, VA `0xFFFF800018000000`).
2. Allocate private page-table frames (PML4 → PDPT → PD → PT) for code isolation.
3. Copy ELF load segments from staging VA into newly allocated physical frames.
4. Allocate a 16 KB user execution stack.
5. Create a `Task` with `user_mode = true`, `cr3 = private PML4`.
6. Set task state to `TASK_READY`; it enters the scheduler on the next tick.

---

## Yield

```cpp
// kernel
void TaskScheduler::task_yield();   // issues int 0x81
```

Userspace: `sys_yield()` → `SYS_YIELD` → `task_yield()`.

Yielding immediately forces a context switch without waiting for the quantum to expire.

---

## Pipe blocking

`SYS_READ` on an empty pipe sets the reader to `TASK_BLOCKED`. The writer's `SYS_WRITE` marks the reader `TASK_READY` and calls `task_yield()` so the reader runs promptly.

## waitpid blocking

`SYS_WAITPID` sets the parent to `TASK_BLOCKED` with `waiting_for = child_id`. `SYS_EXIT` scans for a parent waiting on the exiting task and marks it `TASK_READY`.
