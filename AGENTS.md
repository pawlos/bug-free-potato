# AGENTS.md - Coding Guidelines for bug-free-potato

This is an OS kernel project (potat OS) written in C++ and x86_64 assembly.

## Build Commands

```bash
# Build the kernel binary
make build-x86_64

# Build bootable ISO (requires grub-mkrescue, xorriso, mtools)
make build-cd

# Clean build artifacts
make clean

# Run in QEMU emulator
make run

# Debug with GDB (starts QEMU with -s flag)
make gdb
```

## Project Structure

- `src/impl/kernel/` - Kernel main implementation (C++)
- `src/impl/x86_64/` - x86_64-specific implementations
- `src/impl/x86_64/boot/` - Boot assembly files
- `src/impl/x86_64/device/` - Device drivers (PCI, keyboard, mouse, AC97, etc.)
- `src/intf/` - Header files
- `target/x86_64/` - Linker scripts and ISO config
- `build/` - Object files (generated)
- `dist/` - Output binaries (generated)

## Code Style Guidelines

### Types
- Use custom `pt::` namespace types from `defs.h`:
  - `pt::uint8_t`, `pt::uint16_t`, `pt::uint32_t`, `pt::uint64_t`
  - `pt::int8_t`, `pt::int16_t`
  - `pt::size_t`, `pt::uintptr_t`

### Naming Conventions
- **Classes**: PascalCase (e.g., `VMM`, `TerminalPrinter`, `IO`)
- **Functions**: camelCase or snake_case (be consistent within file)
- **Variables**: snake_case or camelCase
- **Constants**: snake_case with constexpr
- **Macros**: UPPER_SNAKE_CASE
- **Global variables**: snake_case (e.g., `vmm`, `ticks`)

### Headers
- Use `#pragma once` for include guards
- Include format: `#include "header.h"` (quotes, not angle brackets)
- Header files go in `src/intf/`

### Assembly Interop
- Use `ASMCALL` macro for C++ functions called from assembly:
  ```cpp
  ASMCALL void kernel_main(boot_info* boot_info, void* l4_page_table)
  ```
- Assembly files use NASM syntax

### Error Handling
- Use `kernel_panic("message", reason_code)` for fatal errors
- Error codes defined in `kernel.h` (e.g., `NotAbleToAllocateMemory`)
- Use `klog()` for debug logging (requires `KERNEL_LOG` define)

### Memory Management
- Use `vmm.kmalloc()` / `vmm.kcalloc()` / `vmm.kfree()` for heap allocation
- Global `vmm` object available throughout kernel
- Memory is 8-byte aligned automatically

### Classes
- Prefer inline functions for simple getters
- Use `[[nodiscard]]` for functions returning important values
- Static methods for utility classes (e.g., `IO::outb()`)

### Code Organization
- Keep arch-specific code in `src/impl/x86_64/`
- Device drivers go in `src/impl/x86_64/device/`
- Platform-agnostic code goes in `src/impl/kernel/`

### Compiler Flags
- `-ffreestanding` - freestanding environment (no stdlib)
- `-masm=intel` - Intel syntax for inline assembly
- `-DKERNEL_LOG` - enable debug logging

### Common Patterns
```cpp
// Kernel logging
klog("[MODULE] Message with value: %x\n", value);

// Panic on error
if (ptr == nullptr) {
    kernel_panic("Null pointer encountered", NullRefNotExpected);
}

// Inline assembly
asm __volatile__("cli");  // disable interrupts

// Constants
constexpr pt::size_t MAX_DEVICES = 16;
```

### Debugging
- Use `-serial stdio` QEMU flag for debug output
- GDB connects to localhost:1234 when using `make gdb`
- Check `.gdb_history` for previous debug sessions

### Testing
- This project does not use automated unit tests
- Test by running in QEMU: `make run`
- Manual testing via kernel shell commands

## CI/CD
- GitHub Actions workflow in `.github/workflows/build.yml`
- Builds on every push/PR to master
- Uses NASM and standard build tools
