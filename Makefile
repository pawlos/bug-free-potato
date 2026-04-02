kernel_source_files := $(shell find src/kernel -name *.cpp)
kernel_object_files := $(patsubst src/kernel/%.cpp, build/kernel/%.o, $(kernel_source_files))

x86_64_cpp_source_files := $(shell find src/arch/x86_64 -name *.cpp)
x86_64_cpp_object_files := $(patsubst src/arch/x86_64/%.cpp, build/x86_64/%.o, $(x86_64_cpp_source_files))

x86_64_asm_source_files := $(shell find src/arch/x86_64 -name *.asm)
x86_64_asm_object_files := $(patsubst src/arch/x86_64/%.asm, build/x86_64/%.o, $(x86_64_asm_source_files))

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

# ── Kernel build rules ───────────────────────────────────────────────────
$(kernel_object_files): build/kernel/%.o : src/kernel/%.cpp
	mkdir -p $(dir $@) && \
	$(CPP) -c -I src/include -I src/arch/x86_64 -g -masm=intel -ffreestanding -fno-rtti -mno-red-zone -mgeneral-regs-only -Wall -Wextra $(CPPFLAGS) $(patsubst build/kernel/%.o, src/kernel/%.cpp, $@) -o $@

$(x86_64_cpp_object_files): build/x86_64/%.o : src/arch/x86_64/%.cpp
	mkdir -p $(dir $@) && \
	$(CPP) -c -I src/include -g -masm=intel -ffreestanding -fno-rtti -mno-red-zone -mgeneral-regs-only -Wall -Wextra $(CPPFLAGS) $(patsubst build/x86_64/%.o, src/arch/x86_64/%.cpp, $@) -o $@

$(x86_64_asm_object_files): build/x86_64/%.o : src/arch/x86_64/%.asm
	mkdir -p $(dir $@) && \
	$(NASM) -f elf64 $(patsubst build/x86_64/%.o, src/arch/x86_64/%.asm, $@) -o $@

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
            src/userspace/libc/dirent.c \
            src/userspace/libc/sdl2.c
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

# ── Simple C userspace programs (pattern rules) ──────────────────────────
SIMPLE_PROGS = hello fork_test pipe_test fswrite_test keytest \
               sleep_test wm_test snake paktest sh mathtest \
               pidtest stattest envtest xxd kilo taskbar sysmon \
               sdl2demo

SIMPLE_OBJS = $(patsubst %, build/userspace/%.o, $(SIMPLE_PROGS))
SIMPLE_BINS = $(patsubst %, dist/userspace/%.elf, $(SIMPLE_PROGS))

$(SIMPLE_OBJS): build/userspace/%.o: src/userspace/%.c $(LIBC_A)
	mkdir -p build/userspace
	$(CC) -c $(CFLAGS_USER) -o $@ $<

$(SIMPLE_BINS): dist/userspace/%.elf: build/userspace/%.o $(LIBC_CRT0) $(LIBC_A) src/userspace/libc/libc.ld
	mkdir -p dist/userspace
	$(LD) -T src/userspace/libc/libc.ld -o $@ $(LIBC_CRT0) $< $(LIBC_A)

# Userspace test ELF
TEST_ELF_OBJ  = build/userspace/test.o
TEST_ELF_BIN  = dist/userspace/test.elf

$(TEST_ELF_OBJ): src/userspace/test.asm
	mkdir -p build/userspace
	$(NASM) -f elf64 -o $(TEST_ELF_OBJ) src/userspace/test.asm

$(TEST_ELF_BIN): $(TEST_ELF_OBJ) src/userspace/test.ld
	mkdir -p dist/userspace
	$(LD) -T src/userspace/test.ld -o $(TEST_ELF_BIN) $(TEST_ELF_OBJ)

# Userspace blink ELF
BLINK_ELF_OBJ = build/userspace/blink.o
BLINK_ELF_BIN = dist/userspace/blink.elf

$(BLINK_ELF_OBJ): src/userspace/blink.asm
	mkdir -p build/userspace
	$(NASM) -f elf64 -o $(BLINK_ELF_OBJ) src/userspace/blink.asm

$(BLINK_ELF_BIN): $(BLINK_ELF_OBJ) src/userspace/blink.ld
	mkdir -p dist/userspace
	$(LD) -T src/userspace/blink.ld -o $(BLINK_ELF_BIN) $(BLINK_ELF_OBJ)

# ── Game / app port rules (split into mk/*.mk) ──────────────────────────
include mk/doom.mk
include mk/quake.mk
include mk/quake2.mk
include mk/duke3d.mk
include mk/player.mk
include mk/devilutionx.mk
include mk/imgview.mk
include mk/sdlpop.mk

# ── Clean ────────────────────────────────────────────────────────────────
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
	-rm -f build/userspace/taskbar.o dist/userspace/taskbar.elf
	-rm -rf build/userspace/libc
	-rm -f $(LIBC_CRT0) $(LIBC_A)
	-rm -f $(SIMPLE_OBJS) $(SIMPLE_BINS)
	-rm -rf $(PLAYER_BUILD)
	-rm -f  $(PLAYER_ELF)
	-rm -rf $(IMGVIEW_BUILD)
	-rm -f  $(IMGVIEW_ELF)
# Per-game clean targets (games are built separately, not part of clean)
clean-doom:
	-rm -rf $(DOOM_BUILD) && rm -f $(DOOM_ELF)
clean-quake:
	-rm -rf $(QUAKE_BUILD) && rm -f $(QUAKE_ELF)
clean-quake2:
	-rm -rf $(Q2_BUILD) && rm -f $(Q2_ELF)
clean-duke3d:
	-rm -rf $(DUKE_BUILD) && rm -f $(DUKE_ELF)
clean-devilutionx:
	-rm -rf $(DVLX_BUILD) && rm -f $(DVLX_ELF)

clean-sdlpop:
	-rm -rf $(SDLPOP_BUILD) && rm -f $(SDLPOP_ELF)

clean-games: clean-doom clean-quake clean-quake2 clean-duke3d clean-sdlpop clean-devilutionx

clean-all: clean clean-games

# ── Disk image ───────────────────────────────────────────────────────────
DUKE_GRP ?= assets/duke3d.grp
QUAKE_PAK ?= assets/PAK0.PAK
Q2_PAK0 ?= assets/quake2/pak0.pak
Q2_PAK1 ?= assets/quake2/pak1.pak
Q2_PAK2 ?= assets/quake2/pak2.pak
PLAYER_MPG ?= assets/bad_apple.mpg

ASSET_FILES = assets/font.psf \
              assets/potato.raw \
              assets/potato.txt \
              assets/boot.raw

disk.img: $(ASSET_FILES) $(TEST_ELF_BIN) $(BLINK_ELF_BIN) $(SIMPLE_BINS) $(PLAYER_ELF) $(IMGVIEW_ELF)
	@echo "Creating FAT32 disk image..."
	dd if=/dev/zero of=disk.img bs=1M count=1024 2>/dev/null
	mkfs.vfat -F 32 -n "POTATDISK" disk.img
	mmd -i disk.img ::SYS ::BIN ::GAMES ::GAMES/SNAKE
	@copy_file() { \
	  src="$$1"; dst="$$2"; \
	  [ -f "$$src" ] || return 0; \
	  size=$$(du -sh "$$src" | cut -f1); \
	  printf "  %-30s %6s  ->  %s\n" "$$(basename $$src)" "$$size" "$$dst"; \
	  mcopy -i disk.img "$$src" "::$$dst"; \
	}; \
	copy_file assets/font.psf   SYS/FONT.PSF; \
	copy_file assets/potato.raw SYS/POTATO.RAW; \
	copy_file assets/potato.txt SYS/POTATO.TXT; \
	copy_file assets/boot.raw   SYS/BOOT.RAW; \
	copy_file $(TEST_ELF_BIN)           BIN/TEST.ELF; \
	copy_file $(BLINK_ELF_BIN)          BIN/BLINK.ELF; \
	copy_file dist/userspace/taskbar.elf BIN/TASKBAR.ELF; \
	copy_file dist/userspace/sysmon.elf  BIN/SYSMON.ELF; \
	copy_file dist/userspace/hello.elf       BIN/HELLO.ELF; \
	copy_file dist/userspace/fork_test.elf   BIN/FORK_TEST.ELF; \
	copy_file dist/userspace/pipe_test.elf   BIN/PIPE_TEST.ELF; \
	copy_file dist/userspace/mathtest.elf    BIN/MATHTEST.ELF; \
	copy_file dist/userspace/keytest.elf     BIN/KEYTEST.ELF; \
	copy_file dist/userspace/fswrite_test.elf BIN/FSWRITE.ELF; \
	copy_file dist/userspace/sleep_test.elf  BIN/SLEEP_TEST.ELF; \
	copy_file dist/userspace/wm_test.elf     BIN/WM_TEST.ELF; \
	copy_file dist/userspace/sh.elf          BIN/SH.ELF; \
	copy_file dist/userspace/paktest.elf     BIN/PAKTEST.ELF; \
	copy_file dist/userspace/pidtest.elf     BIN/PIDTEST.ELF; \
	copy_file dist/userspace/stattest.elf    BIN/STATTEST.ELF; \
	copy_file dist/userspace/envtest.elf     BIN/ENVTEST.ELF; \
	copy_file dist/userspace/xxd.elf         BIN/XXD.ELF; \
	copy_file dist/userspace/kilo.elf        BIN/KILO.ELF; \
	copy_file dist/userspace/sdl2demo.elf   BIN/SDL2DEMO.ELF; \
	copy_file dist/userspace/snake.elf       GAMES/SNAKE/SNAKE.ELF; \
	copy_file $(PLAYER_ELF)                  BIN/PLAYER.ELF; \
	copy_file $(IMGVIEW_ELF)                 BIN/IMGVIEW.ELF; \
	copy_file assets/menu.png                SYS/MENU.PNG; \
	copy_file $(PLAYER_MPG)                  SYS/BADAPPLE.MPG; \
	if [ -f $(DOOM_ELF) ]; then \
	  mmd -i disk.img ::GAMES/DOOM; \
	  copy_file $(DOOM_ELF)  GAMES/DOOM/DOOM.ELF; \
	  copy_file $(DOOM_WAD)  GAMES/DOOM/DOOM1.WAD; \
	fi; \
	if [ -f $(QUAKE_ELF) ]; then \
	  mmd -i disk.img ::GAMES/QUAKE ::GAMES/QUAKE/ID1; \
	  copy_file $(QUAKE_ELF) GAMES/QUAKE/QUAKE.ELF; \
	  copy_file $(QUAKE_PAK) GAMES/QUAKE/ID1/PAK0.PAK; \
	fi; \
	if [ -f $(Q2_ELF) ]; then \
	  mmd -i disk.img ::GAMES/QUAKE2 ::GAMES/QUAKE2/BASEQ2; \
	  copy_file $(Q2_ELF)  GAMES/QUAKE2/QUAKE2.ELF; \
	  copy_file $(Q2_PAK0) GAMES/QUAKE2/BASEQ2/PAK0.PAK; \
	  copy_file $(Q2_PAK1) GAMES/QUAKE2/BASEQ2/PAK1.PAK; \
	  copy_file $(Q2_PAK2) GAMES/QUAKE2/BASEQ2/PAK2.PAK; \
	fi; \
	if [ -f $(DUKE_ELF) ]; then \
	  mmd -i disk.img ::GAMES/DUKE3D; \
	  copy_file $(DUKE_ELF) GAMES/DUKE3D/DUKE3D.ELF; \
	  copy_file $(DUKE_GRP) GAMES/DUKE3D/DUKE3D.GRP; \
	fi; \
	if [ -f $(SDLPOP_ELF) ]; then \
	  mmd -i disk.img ::GAMES/POP ::GAMES/POP/DATA ::GAMES/POP/DATA/PRINCE ::GAMES/POP/DATA/FAT ::GAMES/POP/DATA/GUARD ::GAMES/POP/DATA/GUARD1 ::GAMES/POP/DATA/GUARD2 ::GAMES/POP/DATA/KID ::GAMES/POP/DATA/LEVELS ::GAMES/POP/DATA/SHADOW ::GAMES/POP/DATA/SKEL ::GAMES/POP/DATA/TITLE ::GAMES/POP/DATA/VDUNGEON ::GAMES/POP/DATA/VIZIER ::GAMES/POP/DATA/VPALACE ::GAMES/POP/DATA/PV ::GAMES/POP/DATA/FONT; \
	  copy_file $(SDLPOP_ELF) GAMES/POP/POP.ELF; \
	  for f in src/userspace/sdlpop/SDLPoP-src/data/PRINCE/*; do [ -f "$$f" ] && mcopy -i disk.img "$$f" "::GAMES/POP/DATA/PRINCE/$$(basename $$f)"; done; \
	  for f in src/userspace/sdlpop/SDLPoP-src/data/FAT/*; do [ -f "$$f" ] && mcopy -i disk.img "$$f" "::GAMES/POP/DATA/FAT/$$(basename $$f)"; done; \
	  for f in src/userspace/sdlpop/SDLPoP-src/data/GUARD/*; do [ -f "$$f" ] && mcopy -i disk.img "$$f" "::GAMES/POP/DATA/GUARD/$$(basename $$f)"; done; \
	  for f in src/userspace/sdlpop/SDLPoP-src/data/GUARD1/*; do [ -f "$$f" ] && mcopy -i disk.img "$$f" "::GAMES/POP/DATA/GUARD1/$$(basename $$f)"; done; \
	  for f in src/userspace/sdlpop/SDLPoP-src/data/GUARD2/*; do [ -f "$$f" ] && mcopy -i disk.img "$$f" "::GAMES/POP/DATA/GUARD2/$$(basename $$f)"; done; \
	  for f in src/userspace/sdlpop/SDLPoP-src/data/KID/*; do [ -f "$$f" ] && mcopy -i disk.img "$$f" "::GAMES/POP/DATA/KID/$$(basename $$f)"; done; \
	  for f in src/userspace/sdlpop/SDLPoP-src/data/LEVELS/*; do [ -f "$$f" ] && mcopy -i disk.img "$$f" "::GAMES/POP/DATA/LEVELS/$$(basename $$f)"; done; \
	  for f in src/userspace/sdlpop/SDLPoP-src/data/SHADOW/*; do [ -f "$$f" ] && mcopy -i disk.img "$$f" "::GAMES/POP/DATA/SHADOW/$$(basename $$f)"; done; \
	  for f in src/userspace/sdlpop/SDLPoP-src/data/SKEL/*; do [ -f "$$f" ] && mcopy -i disk.img "$$f" "::GAMES/POP/DATA/SKEL/$$(basename $$f)"; done; \
	  for f in src/userspace/sdlpop/SDLPoP-src/data/TITLE/*; do [ -f "$$f" ] && mcopy -i disk.img "$$f" "::GAMES/POP/DATA/TITLE/$$(basename $$f)"; done; \
	  for f in src/userspace/sdlpop/SDLPoP-src/data/VDUNGEON/*; do [ -f "$$f" ] && mcopy -i disk.img "$$f" "::GAMES/POP/DATA/VDUNGEON/$$(basename $$f)"; done; \
	  for f in src/userspace/sdlpop/SDLPoP-src/data/VIZIER/*; do [ -f "$$f" ] && mcopy -i disk.img "$$f" "::GAMES/POP/DATA/VIZIER/$$(basename $$f)"; done; \
	  for f in src/userspace/sdlpop/SDLPoP-src/data/VPALACE/*; do [ -f "$$f" ] && mcopy -i disk.img "$$f" "::GAMES/POP/DATA/VPALACE/$$(basename $$f)"; done; \
	  for f in src/userspace/sdlpop/SDLPoP-src/data/PV/*; do [ -f "$$f" ] && mcopy -i disk.img "$$f" "::GAMES/POP/DATA/PV/$$(basename $$f)"; done; \
	  for f in src/userspace/sdlpop/SDLPoP-src/data/font/*; do [ -f "$$f" ] && mcopy -i disk.img "$$f" "::GAMES/POP/DATA/FONT/$$(basename $$f)"; done; \
	  [ -f src/userspace/sdlpop/SDLPoP-src/data/icon.png ] && mcopy -i disk.img src/userspace/sdlpop/SDLPoP-src/data/icon.png "::GAMES/POP/DATA/ICON.PNG"; \
	  [ -f src/userspace/sdlpop/SDLPoP-src/data/light.png ] && mcopy -i disk.img src/userspace/sdlpop/SDLPoP-src/data/light.png "::GAMES/POP/DATA/LIGHT.PNG"; \
	  [ -f src/userspace/sdlpop/SDLPoP-src/SDLPoP.ini ] && mcopy -i disk.img src/userspace/sdlpop/SDLPoP-src/SDLPoP.ini "::GAMES/POP/SDLPOP.INI"; \
	  for f in assets/pop/*.DAT; do [ -f "$$f" ] && mcopy -i disk.img "$$f" "::GAMES/POP/DATA/$$(basename $$f)"; done; \
	fi; \
	if [ -f $(DVLX_ELF) ]; then \
	  mmd -i disk.img ::GAMES/DIABLO; \
	  copy_file $(DVLX_ELF)                   GAMES/DIABLO/DIABLO.ELF; \
	  copy_file assets/diablo/DIABDAT.MPQ      GAMES/DIABLO/DIABDAT.MPQ; \
	  copy_file assets/diablo/devilutionx.mpq  GAMES/DIABLO/DEVILUTIONX.MPQ; \
	  if [ -d src/userspace/devilutionx/devilutionx-src/assets ]; then \
	    cd src/userspace/devilutionx/devilutionx-src && \
	    find assets -type d | sort | while read d; do \
	      mmd -i ../../../../disk.img "::GAMES/DIABLO/$$d" 2>/dev/null || true; \
	    done && \
	    find assets -type f | while read f; do \
	      mcopy -i ../../../../disk.img "$$f" "::GAMES/DIABLO/$$f"; \
	    done && \
	    cd ../../../..; \
	  fi; \
	fi
	@echo "Disk image created with directory hierarchy:"
	@mdir -i disk.img ::

# ── Run targets ──────────────────────────────────────────────────────────
run: all disk.img
	$(QEMU) -cdrom dist/x86_64/kernel.iso -drive file=disk.img,format=raw,if=ide,media=disk -boot order=d -serial stdio $(QEMU_OPTIONS)

rundisk: build-x86_64 disk.img
	$(QEMU) -kernel dist/x86_64/kernel.bin -drive file=disk.img,format=raw,if=ide,media=disk -serial stdio $(QEMU_OPTIONS)
