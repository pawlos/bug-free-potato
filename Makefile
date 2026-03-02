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
	-rtc base=localtime \
	-device rtl8139,netdev=n0 \
	-netdev user,id=n0

x86_64_object_files := $(x86_64_cpp_object_files) $(x86_64_asm_object_files)

$(kernel_object_files): build/kernel/%.o : src/impl/kernel/%.cpp
	mkdir -p $(dir $@) && \
	$(CPP) -c -I src/intf -I src/impl/x86_64 -g -masm=intel -ffreestanding -fno-rtti -mno-red-zone -Wall -Wextra $(CPPFLAGS) $(patsubst build/kernel/%.o, src/impl/kernel/%.cpp, $@) -o $@

$(x86_64_cpp_object_files): build/x86_64/%.o : src/impl/x86_64/%.cpp
	mkdir -p $(dir $@) && \
	$(CPP) -c -I src/intf -g -masm=intel -ffreestanding -fno-rtti -mno-red-zone -Wall -Wextra $(CPPFLAGS) $(patsubst build/x86_64/%.o, src/impl/x86_64/%.cpp, $@) -o $@


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
	-rm -f build/kernel/net/*.o
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
	-rm -f $(FORK_TEST_OBJ) $(FORK_TEST_BIN)
	-rm -f $(PIPE_TEST_OBJ) $(PIPE_TEST_BIN)
	-rm -f $(MATHTEST_OBJ)  $(MATHTEST_BIN)
	-rm -f $(KEYTEST_OBJ)   $(KEYTEST_BIN)
	-rm -f $(FSWRITE_OBJ)   $(FSWRITE_BIN)
	-rm -f $(WM_TEST_OBJ)   $(WM_TEST_BIN)
	-rm -f $(SNAKE_OBJ)     $(SNAKE_BIN)
	-rm -f $(PAKTEST_OBJ)   $(PAKTEST_BIN)
	-rm -f $(SH_ELF_OBJ)   $(SH_ELF_BIN)
	-rm -rf $(DOOM_BUILD)
	-rm -f  $(DOOM_ELF)
	-rm -rf $(QUAKE_BUILD)
	-rm -f  $(QUAKE_ELF)

# ── Userspace C runtime (libc shim) ──────────────────────────────────────
CC          = gcc
CFLAGS_USER = -ffreestanding -fno-stack-protector -fno-builtin \
              -fno-asynchronous-unwind-tables \
              -m64 -nostdlib -Wall -Wextra -I src/userspace

LIBC_SRCS = src/userspace/libc/stdio.c \
            src/userspace/libc/stdlib.c \
            src/userspace/libc/string.c \
            src/userspace/libc/file.c \
            src/userspace/libc/math.c \
            src/userspace/libc/dirent.c
LIBC_OBJS = $(patsubst src/userspace/libc/%.c, build/userspace/libc/%.o, $(LIBC_SRCS))
LIBC_ASM_SRCS = src/userspace/libc/setjmp.asm
LIBC_ASM_OBJS = $(patsubst src/userspace/libc/%.asm, build/userspace/libc/%.o, $(LIBC_ASM_SRCS))
LIBC_CRT0 = build/userspace/crt0.o
LIBC_A    = build/userspace/libc.a

$(LIBC_CRT0): src/userspace/libc/crt0.asm
	mkdir -p build/userspace
	$(NASM) -f elf64 -o $@ $<

$(LIBC_OBJS): build/userspace/libc/%.o : src/userspace/libc/%.c
	mkdir -p build/userspace/libc
	$(CC) -c $(CFLAGS_USER) -o $@ $<

$(LIBC_ASM_OBJS): build/userspace/libc/%.o : src/userspace/libc/%.asm
	mkdir -p build/userspace/libc
	$(NASM) -f elf64 -o $@ $<

$(LIBC_A): $(LIBC_OBJS) $(LIBC_ASM_OBJS)
	ar rcs $@ $^

# ── Hello demo (C userspace program) ─────────────────────────────────────
HELLO_ELF_OBJ = build/userspace/hello.o
HELLO_ELF_BIN = src/impl/x86_64/bins/hello.elf

$(HELLO_ELF_OBJ): src/userspace/hello.c $(LIBC_A)
	$(CC) -c $(CFLAGS_USER) -o $@ $<

$(HELLO_ELF_BIN): $(HELLO_ELF_OBJ) $(LIBC_CRT0) $(LIBC_A) src/userspace/libc/libc.ld
	$(LD) -T src/userspace/libc/libc.ld -o $@ $(LIBC_CRT0) $(HELLO_ELF_OBJ) $(LIBC_A)

# ── fork/exec/waitpid test ────────────────────────────────────────────────
FORK_TEST_OBJ = build/userspace/fork_test.o
FORK_TEST_BIN = src/impl/x86_64/bins/fork_test.elf

$(FORK_TEST_OBJ): src/userspace/fork_test.c $(LIBC_A)
	$(CC) -c $(CFLAGS_USER) -o $@ $<

$(FORK_TEST_BIN): $(FORK_TEST_OBJ) $(LIBC_CRT0) $(LIBC_A) src/userspace/libc/libc.ld
	$(LD) -T src/userspace/libc/libc.ld -o $@ $(LIBC_CRT0) $(FORK_TEST_OBJ) $(LIBC_A)

# ── pipe test ────────────────────────────────────────────────────────────────
PIPE_TEST_OBJ = build/userspace/pipe_test.o
PIPE_TEST_BIN = src/impl/x86_64/bins/pipe_test.elf

$(PIPE_TEST_OBJ): src/userspace/pipe_test.c $(LIBC_A)
	$(CC) -c $(CFLAGS_USER) -o $@ $<

$(PIPE_TEST_BIN): $(PIPE_TEST_OBJ) $(LIBC_CRT0) $(LIBC_A) src/userspace/libc/libc.ld
	$(LD) -T src/userspace/libc/libc.ld -o $@ $(LIBC_CRT0) $(PIPE_TEST_OBJ) $(LIBC_A)

# ── filesystem write test ─────────────────────────────────────────────────────
FSWRITE_OBJ = build/userspace/fswrite_test.o
FSWRITE_BIN = src/impl/x86_64/bins/fswrite_test.elf

$(FSWRITE_OBJ): src/userspace/fswrite_test.c $(LIBC_A)
	$(CC) -c $(CFLAGS_USER) -o $@ $<

$(FSWRITE_BIN): $(FSWRITE_OBJ) $(LIBC_CRT0) $(LIBC_A) src/userspace/libc/libc.ld
	$(LD) -T src/userspace/libc/libc.ld -o $@ $(LIBC_CRT0) $(FSWRITE_OBJ) $(LIBC_A)

# ── key event test ────────────────────────────────────────────────────────────
KEYTEST_OBJ = build/userspace/keytest.o
KEYTEST_BIN = src/impl/x86_64/bins/keytest.elf

$(KEYTEST_OBJ): src/userspace/keytest.c $(LIBC_A)
	$(CC) -c $(CFLAGS_USER) -o $@ $<

$(KEYTEST_BIN): $(KEYTEST_OBJ) $(LIBC_CRT0) $(LIBC_A) src/userspace/libc/libc.ld
	$(LD) -T src/userspace/libc/libc.ld -o $@ $(LIBC_CRT0) $(KEYTEST_OBJ) $(LIBC_A)

# ── sleep test ────────────────────────────────────────────────────────────────
SLEEP_TEST_OBJ = build/userspace/sleep_test.o
SLEEP_TEST_BIN = src/impl/x86_64/bins/sleep_test.elf

$(SLEEP_TEST_OBJ): src/userspace/sleep_test.c $(LIBC_A)
	$(CC) -c $(CFLAGS_USER) -o $@ $<

$(SLEEP_TEST_BIN): $(SLEEP_TEST_OBJ) $(LIBC_CRT0) $(LIBC_A) src/userspace/libc/libc.ld
	$(LD) -T src/userspace/libc/libc.ld -o $@ $(LIBC_CRT0) $(SLEEP_TEST_OBJ) $(LIBC_A)

# ── window manager test ───────────────────────────────────────────────────────
WM_TEST_OBJ = build/userspace/wm_test.o
WM_TEST_BIN = src/impl/x86_64/bins/wm_test.elf

$(WM_TEST_OBJ): src/userspace/wm_test.c $(LIBC_A)
	$(CC) -c $(CFLAGS_USER) -o $@ $<

$(WM_TEST_BIN): $(WM_TEST_OBJ) $(LIBC_CRT0) $(LIBC_A) src/userspace/libc/libc.ld
	$(LD) -T src/userspace/libc/libc.ld -o $@ $(LIBC_CRT0) $(WM_TEST_OBJ) $(LIBC_A)

# ── Snake game ────────────────────────────────────────────────────────────────
SNAKE_OBJ = build/userspace/snake.o
SNAKE_BIN = src/impl/x86_64/bins/snake.elf

$(SNAKE_OBJ): src/userspace/snake.c $(LIBC_A)
	$(CC) -c $(CFLAGS_USER) -o $@ $<

$(SNAKE_BIN): $(SNAKE_OBJ) $(LIBC_CRT0) $(LIBC_A) src/userspace/libc/libc.ld
	$(LD) -T src/userspace/libc/libc.ld -o $@ $(LIBC_CRT0) $(SNAKE_OBJ) $(LIBC_A)

# ── PAK VFS stress test ───────────────────────────────────────────────────────
PAKTEST_OBJ = build/userspace/paktest.o
PAKTEST_BIN = src/impl/x86_64/bins/paktest.elf

$(PAKTEST_OBJ): src/userspace/paktest.c $(LIBC_A)
	$(CC) -c $(CFLAGS_USER) -o $@ $<

$(PAKTEST_BIN): $(PAKTEST_OBJ) $(LIBC_CRT0) $(LIBC_A) src/userspace/libc/libc.ld
	$(LD) -T src/userspace/libc/libc.ld -o $@ $(LIBC_CRT0) $(PAKTEST_OBJ) $(LIBC_A)

# ── userland shell ────────────────────────────────────────────────────────────
SH_ELF_OBJ = build/userspace/sh.o
SH_ELF_BIN = src/impl/x86_64/bins/sh.elf

$(SH_ELF_OBJ): src/userspace/sh.c $(LIBC_A)
	$(CC) -c $(CFLAGS_USER) -o $@ $<

$(SH_ELF_BIN): $(SH_ELF_OBJ) $(LIBC_CRT0) $(LIBC_A) src/userspace/libc/libc.ld
	$(LD) -T src/userspace/libc/libc.ld -o $@ $(LIBC_CRT0) $(SH_ELF_OBJ) $(LIBC_A)

# ── math test ─────────────────────────────────────────────────────────────────
MATHTEST_OBJ = build/userspace/mathtest.o
MATHTEST_BIN = src/impl/x86_64/bins/mathtest.elf

$(MATHTEST_OBJ): src/userspace/mathtest.c $(LIBC_A)
	$(CC) -c $(CFLAGS_USER) -o $@ $<

$(MATHTEST_BIN): $(MATHTEST_OBJ) $(LIBC_CRT0) $(LIBC_A) src/userspace/libc/libc.ld
	$(LD) -T src/userspace/libc/libc.ld -o $@ $(LIBC_CRT0) $(MATHTEST_OBJ) $(LIBC_A)

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

# ── Doom ──────────────────────────────────────────────────────────────────────
DOOM_DIR    = src/userspace/doom
DOOMGEN_DIR = $(DOOM_DIR)/doomgeneric/doomgeneric
DOOM_BUILD  = build/userspace/doom
DOOM_ELF    = src/impl/x86_64/bins/doom.elf
DOOM_WAD    = src/impl/x86_64/bins/doom1.wad

# Compile flags: suppress all warnings (-w) so old Doom C doesn't drown the build.
# -I src/userspace/libc makes #include <stdio.h> etc. find our freestanding libc.
# -I src/userspace lets potato.c do #include "libc/syscall.h".
CFLAGS_DOOM = -ffreestanding -fno-stack-protector -fno-builtin \
              -fno-asynchronous-unwind-tables \
              -m64 -nostdlib -w \
              -include stddef.h \
              -DFEATURE_SOUND \
              -I src/userspace/libc \
              -I src/userspace \
              -I $(DOOMGEN_DIR)

# Fetch doomgeneric source on demand (depth-1 clone, ~2 MB).
# The repo nests sources under doomgeneric/doomgeneric/, so clone to
# src/userspace/doom/doomgeneric (the repo root).
$(DOOMGEN_DIR)/doomgeneric.h:
	git clone --depth=1 https://github.com/ozkl/doomgeneric.git $(DOOM_DIR)/doomgeneric

# All doomgeneric .c files — exclude other platform implementations and
# platform-specific audio drivers (Allegro, SDL).
DOOMGEN_SRCS := $(filter-out \
    $(DOOMGEN_DIR)/doomgeneric_allegro.c \
    $(DOOMGEN_DIR)/doomgeneric_emscripten.c \
    $(DOOMGEN_DIR)/doomgeneric_linuxvt.c \
    $(DOOMGEN_DIR)/doomgeneric_sdl.c \
    $(DOOMGEN_DIR)/doomgeneric_soso.c \
    $(DOOMGEN_DIR)/doomgeneric_sosox.c \
    $(DOOMGEN_DIR)/doomgeneric_win.c \
    $(DOOMGEN_DIR)/doomgeneric_xlib.c \
    $(DOOMGEN_DIR)/i_allegromusic.c \
    $(DOOMGEN_DIR)/i_allegrosound.c \
    $(DOOMGEN_DIR)/i_sdlmusic.c \
    $(DOOMGEN_DIR)/i_sdlsound.c, \
    $(wildcard $(DOOMGEN_DIR)/*.c))
DOOMGEN_OBJS := $(patsubst $(DOOMGEN_DIR)/%.c, $(DOOM_BUILD)/dg_%.o, $(DOOMGEN_SRCS))

# doomgeneric.c has its own main() — rename it so ours in potato.c wins.
$(DOOM_BUILD)/dg_doomgeneric.o: $(DOOMGEN_DIR)/doomgeneric.c $(DOOMGEN_DIR)/doomgeneric.h
	mkdir -p $(DOOM_BUILD)
	$(CC) -c $(CFLAGS_DOOM) -Dmain=_dg_unused_main -o $@ $<

# All other doomgeneric sources compile normally
$(DOOM_BUILD)/dg_%.o: $(DOOMGEN_DIR)/%.c $(DOOMGEN_DIR)/doomgeneric.h
	mkdir -p $(DOOM_BUILD)
	$(CC) -c $(CFLAGS_DOOM) -o $@ $<

# Our platform layer (potato.c + sound module)
$(DOOM_BUILD)/doomgeneric_potato.o: $(DOOM_DIR)/doomgeneric_potato.c $(DOOMGEN_DIR)/doomgeneric.h
	mkdir -p $(DOOM_BUILD)
	$(CC) -c $(CFLAGS_DOOM) -o $@ $<

$(DOOM_BUILD)/doomgeneric_potato_sound.o: $(DOOM_DIR)/doomgeneric_potato_sound.c $(DOOMGEN_DIR)/doomgeneric.h
	mkdir -p $(DOOM_BUILD)
	$(CC) -c $(CFLAGS_DOOM) -o $@ $<

DOOM_POTATO_OBJS = $(DOOM_BUILD)/doomgeneric_potato.o $(DOOM_BUILD)/doomgeneric_potato_sound.o

$(DOOM_ELF): $(DOOMGEN_OBJS) $(DOOM_POTATO_OBJS) $(LIBC_CRT0) $(LIBC_A) src/userspace/libc/libc.ld
	$(LD) --no-relax -T src/userspace/libc/libc.ld -o $@ \
	      $(LIBC_CRT0) $(DOOM_POTATO_OBJS) $(DOOMGEN_OBJS) $(LIBC_A)

# Download shareware DOOM1.WAD (id Software freeware release)
$(DOOM_WAD):
	@echo "Downloading shareware doom1.wad..."
	curl -L -o $@ "https://distro.ibiblio.org/slitaz/sources/packages/d/doom1.wad" || \
	  { echo "ERROR: Could not download doom1.wad. Place it manually at $(DOOM_WAD)"; exit 1; }

doom: $(DOOM_ELF)

doom-disk: $(DOOM_ELF) $(DOOM_WAD)
	@echo "doom.elf and doom1.wad ready for disk image"

# ── Quake ──────────────────────────────────────────────────────────────────────
QUAKE_DIR    = src/userspace/quake
QUAKEGEN_DIR = $(QUAKE_DIR)/quakegeneric/source
QUAKE_BUILD  = build/userspace/quake
QUAKE_ELF    = src/impl/x86_64/bins/quake.elf

# Compile flags: suppress all warnings (-w) for old Quake C.
# -I src/userspace/libc makes system includes find our freestanding libc.
# -I src/userspace lets potato.c do #include "libc/syscall.h".
# -I $(QUAKEGEN_DIR) allows #include "quakegeneric.h" etc.
CFLAGS_QUAKE = -ffreestanding -fno-stack-protector -fno-builtin \
               -fno-asynchronous-unwind-tables \
               -m64 -nostdlib -w \
               -I src/userspace/libc \
               -I src/userspace \
               -I $(QUAKEGEN_DIR)

# Fetch quakegeneric on demand (depth-1 clone, ~3 MB).
# After cloning, increase the hardcoded 8 MB memory pool to 64 MB so the
# full game pak0.pak fits without Hunk_Alloc failures.
$(QUAKEGEN_DIR)/quakegeneric.h:
	git clone --depth=1 https://github.com/erysdren/quakegeneric.git $(QUAKE_DIR)/quakegeneric
	sed -i 's/parms\.memsize = 8\*1024\*1024/parms.memsize = 64*1024*1024/' $(QUAKEGEN_DIR)/quakegeneric.c

# All quakegeneric engine sources (from CMakeLists.txt)
QUAKEGEN_SRCS := $(addprefix $(QUAKEGEN_DIR)/, \
    cd_null.c chase.c cl_demo.c cl_input.c cl_main.c cl_parse.c cl_tent.c \
    cmd.c common.c console.c crc.c cvar.c \
    d_edge.c d_fill.c d_init.c d_modech.c d_part.c d_polyse.c d_scan.c \
    d_sky.c d_sprite.c d_surf.c d_vars.c d_zpoint.c \
    draw.c host_cmd.c host.c in_null.c keys.c mathlib.c menu.c model.c \
    net_loop.c net_main.c net_none.c net_vcr.c nonintel.c \
    pr_cmds.c pr_edict.c pr_exec.c \
    r_aclip.c r_alias.c r_bsp.c r_draw.c r_edge.c r_efrag.c r_light.c \
    r_main.c r_misc.c r_part.c r_sky.c r_sprite.c r_surf.c r_vars.c \
    sbar.c screen.c snd_null.c \
    sv_main.c sv_move.c sv_phys.c sv_user.c \
    sys_null.c vid_null.c view.c wad.c world.c zone.c quakegeneric.c)

QUAKEGEN_OBJS := $(patsubst $(QUAKEGEN_DIR)/%.c, $(QUAKE_BUILD)/qg_%.o, $(QUAKEGEN_SRCS))

# quakegeneric.c has a commented-out main(); compile normally
$(QUAKE_BUILD)/qg_%.o: $(QUAKEGEN_DIR)/%.c $(QUAKEGEN_DIR)/quakegeneric.h
	mkdir -p $(QUAKE_BUILD)
	$(CC) -c $(CFLAGS_QUAKE) -o $@ $<

# Our potato platform layer
$(QUAKE_BUILD)/quakegeneric_potato.o: $(QUAKE_DIR)/quakegeneric_potato.c $(QUAKEGEN_DIR)/quakegeneric.h
	mkdir -p $(QUAKE_BUILD)
	$(CC) -c $(CFLAGS_QUAKE) -o $@ $<

$(QUAKE_ELF): $(QUAKEGEN_OBJS) $(QUAKE_BUILD)/quakegeneric_potato.o $(LIBC_CRT0) $(LIBC_A) src/userspace/libc/libc.ld
	$(LD) --no-relax --wrap=Sys_Error --wrap=Sys_Printf \
	      -T src/userspace/libc/libc.ld -o $@ \
	      $(LIBC_CRT0) $(QUAKE_BUILD)/quakegeneric_potato.o $(QUAKEGEN_OBJS) $(LIBC_A)

quake: $(QUAKE_ELF)

# Path to Quake PAK0.PAK (user-supplied; copyrighted — cannot auto-download).
# Copy PAK0.PAK into src/impl/x86_64/bins/ before running make disk.img.
# Override: make disk.img QUAKE_PAK=/path/to/pak0.pak
QUAKE_PAK ?= src/impl/x86_64/bins/PAK0.PAK

# Create FAT32 disk image with test files from bins folder
ASSET_FILES = src/impl/x86_64/bins/font.psf \
              src/impl/x86_64/bins/potato.raw \
              src/impl/x86_64/bins/potato.txt \
              src/impl/x86_64/bins/boot.raw

disk.img: $(ASSET_FILES) $(TEST_ELF_BIN) $(BLINK_ELF_BIN) $(HELLO_ELF_BIN) $(FORK_TEST_BIN) $(PIPE_TEST_BIN) $(MATHTEST_BIN) $(KEYTEST_BIN) $(FSWRITE_BIN) $(SLEEP_TEST_BIN) $(WM_TEST_BIN) $(SH_ELF_BIN) $(SNAKE_BIN) $(PAKTEST_BIN) $(DOOM_ELF) $(DOOM_WAD) $(QUAKE_ELF)
	@echo "Creating FAT32 disk image..."
	dd if=/dev/zero of=disk.img bs=1M count=128 2>/dev/null
	mkfs.vfat -F 32 -n "POTATDISK" disk.img
	@copy_file() { \
	  src="$$1"; dst="$$2"; \
	  [ -f "$$src" ] || return 0; \
	  size=$$(du -sh "$$src" | cut -f1); \
	  printf "  %-30s %6s  ->  %s\n" "$$(basename $$src)" "$$size" "$$dst"; \
	  mcopy -i disk.img "$$src" "::$$dst"; \
	}; \
	copy_file src/impl/x86_64/bins/font.psf   FONT.PSF; \
	copy_file src/impl/x86_64/bins/potato.raw POTATO.RAW; \
	copy_file src/impl/x86_64/bins/potato.txt POTATO.TXT; \
	copy_file src/impl/x86_64/bins/boot.raw   BOOT.RAW; \
	copy_file $(TEST_ELF_BIN)   TEST.ELF; \
	copy_file $(BLINK_ELF_BIN)  BLINK.ELF; \
	copy_file $(HELLO_ELF_BIN)  HELLO.ELF; \
	copy_file $(FORK_TEST_BIN)  FORK_TEST.ELF; \
	copy_file $(PIPE_TEST_BIN)  PIPE_TEST.ELF; \
	copy_file $(MATHTEST_BIN)   MATHTEST.ELF; \
	copy_file $(KEYTEST_BIN)    KEYTEST.ELF; \
	copy_file $(FSWRITE_BIN)    FSWRITE.ELF; \
	copy_file $(SLEEP_TEST_BIN) SLEEP_TEST.ELF; \
	copy_file $(WM_TEST_BIN)    WM_TEST.ELF; \
	copy_file $(SH_ELF_BIN)     SH.ELF; \
	copy_file $(SNAKE_BIN)      SNAKE.ELF; \
	copy_file $(PAKTEST_BIN)    PAKTEST.ELF; \
	copy_file $(DOOM_ELF)       DOOM.ELF; \
	copy_file $(DOOM_WAD)       DOOM1.WAD; \
	copy_file $(QUAKE_ELF)      QUAKE.ELF; \
	copy_file $(QUAKE_PAK)      PAK0.PAK
	@echo "Disk image created with files:"
	@mdir -i disk.img ::

run: all disk.img
	$(QEMU) -cdrom dist/x86_64/kernel.iso -drive file=disk.img,format=raw,if=ide,media=disk -boot order=d -serial stdio $(QEMU_OPTIONS)

rundisk: build-x86_64 disk.img
	$(QEMU) -kernel dist/x86_64/kernel.bin -drive file=disk.img,format=raw,if=ide,media=disk -serial stdio $(QEMU_OPTIONS)
