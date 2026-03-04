# bug-free-potato

## Prerequisites

- [nasm](https://nasm.us/)
- [xorriso](https://www.gnu.org/software/xorriso/)
- [mtools](https://www.gnu.org/software/mtools/)

## Building

    make clean && make build-cd

## Running

Either use attached `potato.bxrc` file or execute with `qemu-system-x86_64 -cdrom dist\x86_64\kernel.iso`.

## Debugging

Add `-serial stdio` to enable debug logging.

## Doom

The kernel can run Doom via [doomgeneric](https://github.com/ozkl/doomgeneric). The platform layer is at `src/userspace/doom/doomgeneric_potato.c`, but the doomgeneric source itself is not included in this repository.

To build with Doom support, clone doomgeneric into the expected location:

    git clone https://github.com/ozkl/doomgeneric src/userspace/doom/doomgeneric

You also need a Doom IWAD (e.g. `doom1.wad` from the shareware release) placed at:

    assets/doom1.wad