# bug-free-potato


## Building

    make clean && make build-cd

Build requires the following to be installed: [nasm](https://nasm.us/), [xorriso](https://www.gnu.org/software/xorriso/) & [mtools](https://www.gnu.org/software/mtools/).

## Running

Either use attached `potato.bxrc` file or execute with `qemu-system-x86_64 -cdrom dist\x86_64\kernel.iso`.

## Debugging

Add `-serial stdio` to enable debug logging.