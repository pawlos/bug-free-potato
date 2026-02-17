# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**bug-free-potato** is a 64-bit OS kernel (potat OS) written in C++ and x86_64 assembly. It runs on QEMU and implements core OS functionality including boot, memory management (VMM), device drivers, a filesystem (FAT12), and a shell interface.

## Essential Commands

```bash
# Build ISO for booting on x86_64
make build-cd

# Run in QEMU emulator (uses AC97 audio)
make run

# Debug with GDB (QEMU paused at startup, listening on localhost:1234)
make gdb

# Clean build artifacts
make clean

# Run with debug serial output
make run  # Serial output shows via -serial stdio flag
```

For audio device backend selection on Linux:
```bash
QEMU_AUDIO_BACKEND=pa make run  # PulseAudio
QEMU_AUDIO_BACKEND=alsa make run  # ALSA
```

## Architecture

### Directory Structure

- `src/intf/` - Header files (interfaces for all modules)
- `src/impl/kernel/` - Kernel-agnostic implementation (main loop, shell, line reader)
- `src/impl/x86_64/` - Architecture-specific implementation
  - `boot/` - Boot assembly and initialization
  - `device/` - Device drivers (PCI, keyboard, mouse, disk/IDE, AC97 audio, timer, COM serial)
  - `filesystem/` - FAT12 filesystem implementation
  - `libs/` - Standard library utilities
  - `idt.cpp`, `virtual.cpp` - Interrupt handling and virtual memory management
  - `framebuffer.cpp`, `print.cpp` - Display and text output
- `target/x86_64/` - Linker scripts and GRUB ISO configuration
- `build/` - Object files (generated)
- `dist/` - Output binaries (generated)

### Design Patterns

**Interface/Implementation Split**: Headers in `src/intf/` are included by both kernel and device code. Implementations in `src/impl/kernel/` are platform-agnostic; `src/impl/x86_64/` contains architecture-specific code.

**Global Kernel Objects**: Key singletons available globally (defined in boot):
- `vmm` - Virtual memory manager with `kmalloc()`, `kcalloc()`, `kfree()`
- `terminal_printer` - Main framebuffer output (implements Print interface)
- Device managers for keyboard, mouse, disk, timer, AC97, etc.

**Assembly Integration**: C++ functions called from assembly are marked with `ASMCALL` macro (extern "C"). Entry point is `kernel_main(boot_info* boot_info, void* l4_page_table)`.

### Build Configuration

- **Compiler**: g++ with `-ffreestanding -masm=intel` flags
- **Assembler**: NASM for x86_64 ELF64 object files
- **Linker**: GNU LD with script at `target/x86_64/linker.ld`
- **Bootloader**: GRUB (ISO created with grub-mkrescue)
- **Logging**: Enabled by `-DKERNEL_LOG` define; use `klog()` for debug output

### Code Standards

**Type System**: Use custom `pt::` namespace types from `defs.h`:
- Integers: `pt::uint8_t`, `pt::uint16_t`, `pt::uint32_t`, `pt::uint64_t`, `pt::int8_t`, `pt::int16_t`
- Pointer/size: `pt::size_t`, `pt::uintptr_t`

**Naming**:
- Classes: PascalCase (`VMM`, `TerminalPrinter`, `IO`)
- Functions/variables: camelCase or snake_case (be consistent within a file)
- Constants: snake_case with `constexpr`
- Macros: UPPER_SNAKE_CASE
- Global variables: snake_case

**Error Handling**: Fatal errors call `kernel_panic("message", error_code)`. Error codes defined in `kernel.h` (e.g., `NotAbleToAllocateMemory`).

**Headers**: Use `#pragma once` for include guards; include format is `#include "header.h"` (quotes, not angle brackets).

## Key Subsystems

- **Boot (boot.cpp)**: Initializes paging, sets up IDT, enables CPU features, calls kernel_main
- **Shell (shell.cpp)**: Interactive command interface with line reader for kernel testing
- **Disk I/O (device/ide.cpp)**: IDE controller driver for disk access
- **FAT12 (filesystem/fat12.cpp)**: File reading from FAT12 disk images
- **AC97 Audio (device/ac97.cpp)**: Audio device driver (recently added)
- **Memory Management (virtual.cpp)**: Paging, heap allocation, virtual address translation
- **Interrupt Handling (idt.cpp)**: IDT setup and interrupt routing

## Debugging Notes

- Use `make gdb` to start QEMU with GDB stub enabled
- GDB commands: `target remote localhost:1234`, `c` (continue), `b` (break), `p variable` (print)
- Set `-DKERNEL_LOG` in Makefile to enable `klog()` output to serial port
- Check `.gdb_history` for previous GDB sessions
- Serial output visible in QEMU terminal window with `-serial stdio`

## Testing

No automated unit tests. Manual testing via:
- QEMU execution: `make run`
- Kernel shell commands at runtime
- GDB debugging with `make gdb`
- Disk image created automatically with test files from `src/impl/x86_64/bins/` directory

## References

See `AGENTS.md` for detailed code style guidelines, compilation flags, and common coding patterns.
