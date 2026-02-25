# potatOS Documentation

Developer notes for the bug-free-potato hobby OS kernel.

## Contents

| Document | Topics |
|---|---|
| [boot-process.md](boot-process.md) | 32→64-bit transition, paging setup, device init order, VA map |
| [memory.md](memory.md) | Frame allocator, heap (kmalloc/kfree/coalesce), page tables, per-task spaces |
| [interrupts.md](interrupts.md) | IDT setup, exceptions, IRQ handlers, syscall/yield gates, stack frame layout |
| [scheduling.md](scheduling.md) | Task scheduler, context switching, sleep/wake mechanism |
| [windowing.md](windowing.md) | Window manager, chrome rendering, focus, coordinate systems |
| [syscalls.md](syscalls.md) | All 26+ syscalls: arguments, return values, behaviour |
| [userspace-guide.md](userspace-guide.md) | Writing and building userspace ELF programs |

## Quick-start commands

```bash
make build-cd   # build ISO
make run        # run in QEMU
make gdb        # QEMU paused, GDB on localhost:1234
make clean
```