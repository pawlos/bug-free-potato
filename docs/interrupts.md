# Interrupts and Exception Handling

Interrupt handling is set up in `src/impl/x86_64/idt.cpp`. It covers CPU exceptions
(vectors 0–31), hardware IRQs (32–47), and two software interrupt gates (0x80 syscall,
0x81 yield).

---

## IDT initialization

```cpp
void IDT::initialize() {
    // 1. Remap PIC: master → base 0x20, slave → base 0x28
    // 2. Register exception handlers isr0–isr31 (DPL=0)
    // 3. Register IRQ handlers: irq0 (timer), irq1 (keyboard), irq12 (mouse),
    //                           irq14/irq15 (IDE)
    // 4. Register int 0x80 (syscall, DPL=3) — callable from ring-3
    // 5. Register int 0x81 (yield,   DPL=3) — callable from ring-3
    // 6. Unmask PIC
    // 7. Load IDTR
}
```

---

## CPU exceptions (vectors 0–31)

All 32 Intel-defined exceptions are handled. Behavior varies by vector:

| Vector | Name | Handler action |
|---|---|---|
| 0 | Divide by zero | `kernel_panic` |
| 1 | Debug | `kernel_panic` |
| 2 | NMI | `kernel_panic` |
| 6 | Invalid opcode | Log RIP, opcode bytes, user stack (if ring-3); `kernel_panic` |
| 8 | Double fault | `kernel_panic` |
| 13 | General protection fault | Log error code, RIP, CS, GPRs, user stack; `kernel_panic` |
| 14 | Page fault | Log CR2, error code, RIP, RSP, stack dump; `kernel_panic` |
| all others | — | `kernel_panic` with vector number |

Page fault handler output example:
```
PAGE FAULT at VA 0xdeadbeef  error=0x02 (write, not-present)
  RIP=0xFFFF800000104abc  RSP=0xFFFF800000209ff0
  Stack dump (8 words): ...
```

### PanicRegs

`kernel_panic` captures all 16 GPRs using:

```cpp
struct PanicRegs {
    pt::uint64_t rax, rbx, rcx, rdx;
    pt::uint64_t rsi, rdi, rbp, rsp;
    pt::uint64_t r8, r9, r10, r11;
    pt::uint64_t r12, r13, r14, r15;
};
```

These are printed to both the serial port and the framebuffer before halting.

---

## Hardware IRQs

### IRQ 0 — PIT timer (50 Hz)

This is the scheduler heartbeat.

```
irq0_schedule:
    1. Call timer_tick()          (registered callbacks, e.g. sleep wakeups)
    2. Send EOI to PIC (0x20)
    3. Call TaskScheduler::preempt(current_rsp)
         → returns new_rsp (may be same task or different)
    4. Switch RSP to new_rsp; pop registers; iretq
```

The entire context switch happens inside the IRQ 0 handler. `preempt()` receives the
RSP of the interrupted task's PUSHALL frame, updates `task->preempt_rsp`, decides
whether to switch, and returns the RSP to restore.

### IRQ 1 — PS/2 keyboard

```
irq1_handler:
    1. Read scancode byte from port 0x60
    2. Call keyboard_routine(scancode)
         → updates key state table
         → pushes encoded event to window manager (focused window)
         → pushes to global key-event ring (for SYS_GET_KEY_EVENT)
    3. Send EOI (0x20)
```

### IRQ 12 — PS/2 mouse

```
irq12_handler:
    1. Read one byte from port 0x60
    2. Accumulate 3-byte packet
    3. On complete packet: call mouse_routine(bytes[3])
         → update cursor position
         → check left-button rising edge → window_at() → set_focus()
    4. Send EOI to both slave (0xA0) and master (0x20)
```

### IRQ 14 / 15 — IDE

Acknowledge-only (the IDE driver uses polling, not interrupt-driven I/O).

---

## Interrupt stack frame layout

When an interrupt fires, the CPU pushes (in order, high address first):

```
         ┌──────────────┐  ← RSP before interrupt
         │   SS         │  (only if privilege change)
         │   RSP        │  (only if privilege change)
         │   RFLAGS     │
         │   CS         │
         │   RIP        │
         │   error code │  (only for some exceptions: 8, 10–14, 17, 29, 30)
         └──────────────┘  ← RSP after CPU push
```

The asm stub then issues `PUSHALL` (all 15 GPRs except RSP) to build the full saved
context. `preempt_rsp` points to the base of this PUSHALL block.

### PUSHALL register order (task.cpp convention)

The scheduler uses fixed offsets into the frame to patch registers at fork time:

| Frame offset (×8) | Register | Notes |
|---|---|---|
| 0 | r15 | |
| 1 | r14 | |
| 2 | r13 | |
| 3 | r12 | |
| 4 | r11 | |
| 5 | r10 | |
| 6 | r9 | |
| 7 | r8 | |
| 8 | rbp | patched in fork (child RBP → new stack) |
| 9 | rdi | |
| 10 | rsi | |
| 11 | rdx | |
| 12 | rcx | |
| 13 | rbx | |
| 14 | rax | patched in fork (child rax = 0) |
| 15 | *iretq frame start* | |
| 15 | RIP | patched in exec (new entry point) |
| 16 | CS | |
| 17 | RFLAGS | |
| 18 | RSP (user) | patched in fork/exec |
| 19 | SS | |

---

## Syscall gate (int 0x80)

`int 0x80` is registered as **DPL=3**, so userspace can invoke it without a GPF.

### Dispatch

```cpp
// idt.cpp — int 0x80 handler
pt::uint64_t syscall_handler(pt::uint64_t nr,
                              pt::uint64_t a1, pt::uint64_t a2,
                              pt::uint64_t a3, pt::uint64_t a4,
                              pt::uint64_t a5) {
    switch (nr) {
        case SYS_EXIT:   ...
        case SYS_WRITE:  ...
        // 26 cases total
    }
}
```

The handler is a regular C++ function. The asm stub saves all registers, reads
`rax` (syscall number) and `rdi/rsi/rdx/rcx/r8` (args), calls the C++ handler,
writes the return value back to `rax` in the saved frame, and `iretq`s.

### Calling convention

```
rax  = syscall number
rdi  = arg1
rsi  = arg2
rdx  = arg3
rcx  = arg4
r8   = arg5
← rax = return value
```

All other registers are preserved across the syscall boundary (saved and restored by
the asm stub).

---

## Yield gate (int 0x81)

`int 0x81` is the **cooperative yield** path, also usable from ring-3.

```
int 0x81:
    1. asm stub saves PUSHALL on kernel stack
    2. Calls TaskScheduler::yield_tick(rsp)
         → saves rsp into current task's preempt_rsp
         → marks current task TASK_READY (unless TASK_BLOCKED, e.g. sleeping)
         → calls do_switch_to_next() → returns new_rsp
    3. Restores to new_rsp; POPALL; iretq
```

`int 0x81` is fired by:
- `TaskScheduler::task_yield()` (cooperative yield from kernel code)
- `SYS_YIELD` syscall
- `sleep_task()` after setting deadline and marking TASK_BLOCKED

---

## klog and serial output

```cpp
// kernel.h
#ifdef KERNEL_LOG
#define klog(fmt, ...) serial_printf(fmt, ##__VA_ARGS__)
#else
#define klog(fmt, ...) do {} while(0)
#endif
```

Enable by adding `-DKERNEL_LOG` to the Makefile. Output goes to the serial port, which
QEMU forwards to stdio (`-serial stdio` in the run target).

---

## Interrupt masking summary

| What | When masked |
|---|---|
| All interrupts (`cli`) | During `kernel_panic`, inside IDT setup |
| PIC master (IRQ 0–7) | Before `IDT::initialize()` returns; then unmasked |
| PIC slave (IRQ 8–15) | Same |
| Individual IRQ mask | Never changed after init (all used IRQs stay unmasked) |

The scheduler and syscall handlers run with interrupts **enabled** (the CPU re-enables
them after `iretq` because RFLAGS.IF is preserved in the saved frame).
