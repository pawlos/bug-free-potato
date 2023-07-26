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