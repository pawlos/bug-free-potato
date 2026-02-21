kernel_source_files := $(shell find src/impl/kernel -name *.cpp)
kernel_object_files := $(patsubst src/impl/kernel/%.cpp, build/kernel/%.o, $(kernel_source_files))

x86_64_cpp_source_files := $(shell find src/impl/x86_64 -name *.cpp)
x86_64_cpp_object_files := $(patsubst src/impl/x86_64/%.cpp, build/x86_64/%.o, $(x86_64_cpp_source_files))

x86_64_asm_source_files := $(shell find src/impl/x86_64 -name *.asm)
x86_64_asm_object_files := $(patsubst src/impl/x86_64/%.asm, build/x86_64/%.o, $(x86_64_asm_source_files))

CPP=g++
CPPFLAGS=-DKERNEL_LOG
# Uncomment to enable verbose scheduler task-switch logging:
# CPPFLAGS += -DSCHEDULER_DEBUG
QEMU=/mnt/c/Program\ Files/qemu/qemu-system-x86_64.exe
NASM=nasm
LD=ld
# Audio: emulate an AC97 card.
# On Windows hosts use dsound; on Linux use pa (PulseAudio) or alsa.
QEMU_AUDIO_BACKEND ?= dsound
QEMU_OPTIONS=-m 512M \
	-audiodev $(QEMU_AUDIO_BACKEND),id=audio0 \
	-device AC97,audiodev=audio0 \
	-rtc base=localtime

x86_64_object_files := $(x86_64_cpp_object_files) $(x86_64_asm_object_files)

$(kernel_object_files): build/kernel/%.o : src/impl/kernel/%.cpp
	mkdir -p $(dir $@) && \
	$(CPP) -c -I src/intf -I src/impl/x86_64 -g -masm=intel -ffreestanding -Wall -Wextra $(CPPFLAGS) $(patsubst build/kernel/%.o, src/impl/kernel/%.cpp, $@) -o $@

$(x86_64_cpp_object_files): build/x86_64/%.o : src/impl/x86_64/%.cpp
	mkdir -p $(dir $@) && \
	$(CPP) -c -I src/intf -g -masm=intel -ffreestanding -Wall -Wextra $(CPPFLAGS) $(patsubst build/x86_64/%.o, src/impl/x86_64/%.cpp, $@) -o $@


$(x86_64_asm_object_files): build/x86_64/%.o : src/impl/x86_64/%.asm
	mkdir -p $(dir $@) && \
	$(NASM) -f elf64 $(patsubst build/x86_64/%.o, src/impl/x86_64/%.asm, $@) -o $@

.PHONY: all
all: build-cd
build-cd: build-x86_64
	cp dist/x86_64/kernel.bin target/x86_64/iso/boot/kernel.bin && \
	grub-mkrescue /usr/lib/grub/i386-pc -o dist/x86_64/kernel.iso target/x86_64/iso

build-x86_64: $(kernel_object_files) $(x86_64_object_files)
	mkdir -p dist/x86_64 && \
	$(LD) -n --no-relax -o dist/x86_64/kernel.bin -T target/x86_64/linker.ld $(kernel_object_files) $(x86_64_object_files)

gdb: all disk.img
	$(QEMU) -cdrom dist/x86_64/kernel.iso -drive file=disk.img,format=raw,if=ide,media=disk -boot order=d -serial stdio $(QEMU_OPTIONS) -s -S

clean:
	-rm -f build/kernel/*.o
	-rm -f build/x86_64/*.o
	-rm -f build/x86_64/boot/*.o
	-rm -f build/x86_64/device/*.o
	-rm -f build/x86_64/filesystem/*.o
	-rm -f dist/x86_64/kernel.*
	-rm -f disk.img
	-rm -f $(TEST_ELF_OBJ) $(TEST_ELF_BIN)
	-rm -f $(BLINK_ELF_OBJ) $(BLINK_ELF_BIN)
	-rm -rf build/userspace/libc
	-rm -f $(LIBC_CRT0) $(LIBC_A) $(HELLO_ELF_BIN)

# ── Userspace C runtime (libc shim) ──────────────────────────────────────
CC          = gcc
CFLAGS_USER = -ffreestanding -fno-stack-protector -fno-builtin \
              -fno-asynchronous-unwind-tables \
              -m64 -nostdlib -Wall -Wextra -I src/userspace

LIBC_SRCS = src/userspace/libc/stdio.c \
            src/userspace/libc/stdlib.c \
            src/userspace/libc/string.c
LIBC_OBJS = $(patsubst src/userspace/libc/%.c, build/userspace/libc/%.o, $(LIBC_SRCS))
LIBC_CRT0 = build/userspace/crt0.o
LIBC_A    = build/userspace/libc.a

$(LIBC_CRT0): src/userspace/libc/crt0.asm
	mkdir -p build/userspace
	$(NASM) -f elf64 -o $@ $<

$(LIBC_OBJS): build/userspace/libc/%.o : src/userspace/libc/%.c
	mkdir -p build/userspace/libc
	$(CC) -c $(CFLAGS_USER) -o $@ $<

$(LIBC_A): $(LIBC_OBJS)
	ar rcs $@ $^

# ── Hello demo (C userspace program) ─────────────────────────────────────
HELLO_ELF_OBJ = build/userspace/hello.o
HELLO_ELF_BIN = src/impl/x86_64/bins/hello.elf

$(HELLO_ELF_OBJ): src/userspace/hello.c $(LIBC_A)
	$(CC) -c $(CFLAGS_USER) -o $@ $<

$(HELLO_ELF_BIN): $(HELLO_ELF_OBJ) $(LIBC_CRT0) $(LIBC_A) src/userspace/libc/libc.ld
	$(LD) -T src/userspace/libc/libc.ld -o $@ $(LIBC_CRT0) $(HELLO_ELF_OBJ) $(LIBC_A)

# Files to copy to disk image from bins folder
BIN_FILES := $(wildcard src/impl/x86_64/bins/*)

# Userspace test ELF
TEST_ELF_OBJ  = build/userspace/test.o
TEST_ELF_BIN  = src/impl/x86_64/bins/test.elf

$(TEST_ELF_OBJ): src/userspace/test.asm
	mkdir -p build/userspace
	$(NASM) -f elf64 -o $(TEST_ELF_OBJ) src/userspace/test.asm

$(TEST_ELF_BIN): $(TEST_ELF_OBJ) src/userspace/test.ld
	$(LD) -T src/userspace/test.ld -o $(TEST_ELF_BIN) $(TEST_ELF_OBJ)

# Userspace blink ELF
BLINK_ELF_OBJ = build/userspace/blink.o
BLINK_ELF_BIN = src/impl/x86_64/bins/blink.elf

$(BLINK_ELF_OBJ): src/userspace/blink.asm
	mkdir -p build/userspace
	$(NASM) -f elf64 -o $(BLINK_ELF_OBJ) src/userspace/blink.asm

$(BLINK_ELF_BIN): $(BLINK_ELF_OBJ) src/userspace/blink.ld
	$(LD) -T src/userspace/blink.ld -o $(BLINK_ELF_BIN) $(BLINK_ELF_OBJ)

# Create FAT12 disk image with test files from bins folder
disk.img: $(BIN_FILES) $(TEST_ELF_BIN) $(BLINK_ELF_BIN) $(HELLO_ELF_BIN)
	@echo "Creating FAT12 disk image..."
	@# Create 10MB disk image (large enough for testing)
	dd if=/dev/zero of=disk.img bs=512 count=20480 2>/dev/null
	@# Format as FAT12 (1.44MB floppy geometry)
	mkfs.vfat -F 12 -n "POTATDISK" disk.img
	@# Copy all files from bins folder
	@for file in $(BIN_FILES); do \
		filename=$$(basename $$file); \
		uppername=$$(echo $$filename | tr '[:lower:]' '[:upper:]'); \
		echo "  Copying $$filename -> $$uppername"; \
		mcopy -i disk.img $$file ::$$uppername; \
	done
	@# Also copy the test/blink ELFs explicitly (in case they weren't in BIN_FILES wildcard)
	@mcopy -i disk.img $(TEST_ELF_BIN)  ::TEST.ELF  2>/dev/null || true
	@mcopy -i disk.img $(BLINK_ELF_BIN) ::BLINK.ELF 2>/dev/null || true
	@mcopy -i disk.img $(HELLO_ELF_BIN) ::HELLO.ELF 2>/dev/null || true
	@echo "Disk image created with files:"
	@mdir -i disk.img ::

run: all disk.img
	$(QEMU) -cdrom dist/x86_64/kernel.iso -drive file=disk.img,format=raw,if=ide,media=disk -boot order=d -serial stdio $(QEMU_OPTIONS)

rundisk: build-x86_64 disk.img
	$(QEMU) -kernel dist/x86_64/kernel.bin -drive file=disk.img,format=raw,if=ide,media=disk -serial stdio $(QEMU_OPTIONS)
