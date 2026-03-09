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
	-rm -f $(LIBC_CRT0) $(LIBC_A)
	-rm -f $(SIMPLE_OBJS) $(SIMPLE_BINS)
	-rm -rf $(DOOM_BUILD)
	-rm -f  $(DOOM_ELF)
	-rm -rf $(QUAKE_BUILD)
	-rm -f  $(QUAKE_ELF)
	-rm -rf $(Q2_BUILD)
	-rm -f  $(Q2_ELF)
	-rm -rf $(DUKE_BUILD)
	-rm -f  $(DUKE_ELF)

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

# ── Simple C userspace programs (pattern rules) ──────────────────────────
SIMPLE_PROGS = hello fork_test pipe_test fswrite_test keytest \
               sleep_test wm_test snake paktest sh mathtest \
               pidtest stattest envtest xxd kilo

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

# ── Doom ──────────────────────────────────────────────────────────────────────
DOOM_DIR    = src/userspace/doom
DOOMGEN_DIR = $(DOOM_DIR)/doomgeneric/doomgeneric
DOOM_BUILD  = build/userspace/doom
DOOM_ELF    = dist/userspace/doom.elf
DOOM_WAD    = assets/doom1.wad

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

$(DOOM_BUILD)/doomgeneric_potato_sound.o: $(DOOM_DIR)/doomgeneric_potato_sound.c $(DOOMGEN_DIR)/doomgeneric.h $(DOOM_DIR)/mus_opl.h
	mkdir -p $(DOOM_BUILD)
	$(CC) -c $(CFLAGS_DOOM) -I $(DOOM_DIR) -o $@ $<

$(DOOM_BUILD)/opl3.o: $(DOOM_DIR)/opl3.c $(DOOM_DIR)/opl3.h
	mkdir -p $(DOOM_BUILD)
	$(CC) -c $(CFLAGS_DOOM) -I $(DOOM_DIR) -o $@ $<

$(DOOM_BUILD)/mus_opl.o: $(DOOM_DIR)/mus_opl.c $(DOOM_DIR)/mus_opl.h $(DOOM_DIR)/opl3.h
	mkdir -p $(DOOM_BUILD)
	$(CC) -c $(CFLAGS_DOOM) -I $(DOOM_DIR) -o $@ $<

DOOM_POTATO_OBJS = $(DOOM_BUILD)/doomgeneric_potato.o $(DOOM_BUILD)/doomgeneric_potato_sound.o \
                   $(DOOM_BUILD)/opl3.o $(DOOM_BUILD)/mus_opl.o

$(DOOM_ELF): $(DOOMGEN_OBJS) $(DOOM_POTATO_OBJS) $(LIBC_CRT0) $(LIBC_A) src/userspace/libc/libc.ld
	mkdir -p dist/userspace
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
QUAKE_ELF    = dist/userspace/quake.elf

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
    sbar.c screen.c snd_dma.c snd_mem.c snd_mix.c snd_potato.c \
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
	mkdir -p dist/userspace
	$(LD) --no-relax --wrap=Sys_Error --wrap=Sys_Printf \
	      -T src/userspace/libc/libc.ld -o $@ \
	      $(LIBC_CRT0) $(QUAKE_BUILD)/quakegeneric_potato.o $(QUAKEGEN_OBJS) $(LIBC_A)

quake: $(QUAKE_ELF)

# ── Quake 2 ────────────────────────────────────────────────────────────────
Q2_DIR       = src/userspace/quake2
Q2_SRC_DIR   = $(Q2_DIR)/quake2-src
Q2_BUILD     = build/userspace/quake2
Q2_ELF       = dist/userspace/quake2.elf

CFLAGS_Q2 = -ffreestanding -fno-stack-protector -fno-builtin \
            -fno-asynchronous-unwind-tables \
            -m64 -nostdlib -O2 -w \
            -I src/userspace/libc \
            -I src/userspace \
            -I $(Q2_SRC_DIR)

# Clone Q2 GPL source on demand
$(Q2_SRC_DIR)/qcommon/qcommon.h:
	git clone --depth=1 https://github.com/id-Software/Quake-2.git $(Q2_SRC_DIR)
	sed -i 's/sizeof (pv)/sizeof (*pv)/' $(Q2_SRC_DIR)/ref_soft/r_poly.c

# Q2 engine sources (explicit lists to avoid platform-specific files)
Q2_QCOMMON_SRCS := $(addprefix $(Q2_SRC_DIR)/qcommon/, \
    cmd.c cmodel.c common.c crc.c cvar.c files.c md4.c net_chan.c pmove.c)

Q2_CLIENT_SRCS := $(addprefix $(Q2_SRC_DIR)/client/, \
    cl_cin.c cl_ents.c cl_fx.c cl_input.c cl_inv.c cl_main.c \
    cl_newfx.c cl_parse.c cl_pred.c cl_scrn.c cl_tent.c cl_view.c \
    console.c keys.c menu.c qmenu.c snd_dma.c snd_mem.c snd_mix.c)

Q2_SERVER_SRCS := $(addprefix $(Q2_SRC_DIR)/server/, \
    sv_ccmds.c sv_ents.c sv_game.c sv_init.c sv_main.c sv_send.c \
    sv_user.c sv_world.c)

Q2_GAME_SRCS := $(filter-out $(Q2_SRC_DIR)/game/q_shared.c, \
    $(wildcard $(Q2_SRC_DIR)/game/*.c))

Q2_REFSOFT_SRCS := $(addprefix $(Q2_SRC_DIR)/ref_soft/, \
    r_aclip.c r_alias.c r_bsp.c r_draw.c r_edge.c r_image.c r_light.c \
    r_main.c r_misc.c r_model.c r_part.c r_poly.c r_polyse.c \
    r_rast.c r_scan.c r_sprite.c r_surf.c)

Q2_SHARED_SRCS := $(Q2_SRC_DIR)/game/q_shared.c $(Q2_SRC_DIR)/linux/glob.c

Q2_ENGINE_SRCS := $(Q2_QCOMMON_SRCS) $(Q2_CLIENT_SRCS) $(Q2_SERVER_SRCS) \
                  $(Q2_GAME_SRCS) $(Q2_REFSOFT_SRCS) $(Q2_SHARED_SRCS)

Q2_ENGINE_OBJS := $(patsubst $(Q2_SRC_DIR)/%.c, $(Q2_BUILD)/%.o, $(Q2_ENGINE_SRCS))

Q2_POTATO_SRCS := $(Q2_DIR)/q2_potato.c
Q2_POTATO_OBJS := $(patsubst $(Q2_DIR)/%.c, $(Q2_BUILD)/%.o, $(Q2_POTATO_SRCS))

# Compile Q2 engine sources
$(Q2_BUILD)/%.o: $(Q2_SRC_DIR)/%.c $(Q2_SRC_DIR)/qcommon/qcommon.h
	mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS_Q2) -o $@ $<

# Compile potato platform layer
$(Q2_BUILD)/q2_potato.o: $(Q2_DIR)/q2_potato.c $(Q2_SRC_DIR)/qcommon/qcommon.h
	mkdir -p $(Q2_BUILD)
	$(CC) -c $(CFLAGS_Q2) -o $@ $<

$(Q2_ELF): $(Q2_ENGINE_OBJS) $(Q2_POTATO_OBJS) $(LIBC_CRT0) $(LIBC_A) src/userspace/libc/libc.ld
	mkdir -p dist/userspace
	$(LD) --no-relax --allow-multiple-definition \
	      -T src/userspace/libc/libc.ld -o $@ \
	      $(LIBC_CRT0) $(Q2_POTATO_OBJS) $(Q2_ENGINE_OBJS) $(LIBC_A)

quake2: $(Q2_ELF)

# ── Duke Nukem 3D (Chocolate Duke3D) ────────────────────────────────────
DUKE_DIR     = src/userspace/duke3d
DUKE_SRC_DIR = $(DUKE_DIR)/chocolate_duke3D
DUKE_BUILD   = build/userspace/duke3d
DUKE_ELF     = dist/userspace/duke3d.elf

CFLAGS_DUKE = -ffreestanding -fno-stack-protector -fno-builtin \
              -fno-asynchronous-unwind-tables \
              -m64 -nostdlib -O2 -w \
              -DPLATFORM_POTATO \
              -DUSER_DUMMY_NETWORK=1 \
              -include $(DUKE_DIR)/potato_compat.h \
              -I src/userspace/libc \
              -I src/userspace \
              -I $(DUKE_DIR) \
              -I $(DUKE_SRC_DIR)/Engine/src \
              -I $(DUKE_SRC_DIR)/Game/src

# Clone Chocolate Duke3D source on demand + apply 64-bit patches
$(DUKE_SRC_DIR)/Engine/src/engine.c:
	git clone --depth=1 https://github.com/fabiensanglard/chocolate_duke3D.git $(DUKE_SRC_DIR)
	@echo "Applying potatOS / 64-bit patches to Chocolate Duke3D..."
	# Fix FP_OFF truncation: remove (int32_t) casts before FP_OFF in engine.c
	sed -i 's/(int32_t)FP_OFF/(intptr_t)FP_OFF/g;s/(int32_t) FP_OFF/(intptr_t) FP_OFF/g' $(DUKE_SRC_DIR)/Engine/src/engine.c
	# Fix globalpalwritten: should be intptr_t, not int32_t
	sed -i 's/int32_t globalpalwritten/intptr_t globalpalwritten/' $(DUKE_SRC_DIR)/Engine/src/engine.c
	sed -i 's/(int32_t)globalpalwritten/(intptr_t)globalpalwritten/' $(DUKE_SRC_DIR)/Engine/src/engine.c
	# Fix suckcache parameter type (intptr_t* on 64-bit)
	sed -i 's/suckcache((int32_t \*)screen)/suckcache((intptr_t *)screen)/' $(DUKE_SRC_DIR)/Engine/src/engine.c
	# Fix platform.h to include potato_compat.h
	sed -i 's/#error Define your platform!/#include "potato_compat.h"/' $(DUKE_SRC_DIR)/Engine/src/platform.h
	# Fix premap.c -O0 issue: ensure it includes via platform.h
	# Fix sounds.c: uses open()/read()/close() for RTS files
	sed -i 's|<fcntl.h>|"libc/file.h"|;s|<unistd.h>|"libc/stdlib.h"|' $(DUKE_SRC_DIR)/Game/src/sounds.c || true
	# Fix config.c: uses getenv, open, etc.
	sed -i 's|<fcntl.h>|"libc/file.h"|;s|<unistd.h>|"libc/stdlib.h"|' $(DUKE_SRC_DIR)/Game/src/config.c || true
	# Fix game.c: uses signal, time, etc.
	sed -i 's|<signal.h>|"libc/signal.h"|;s|<sys/stat.h>|"libc/sys/stat.h"|' $(DUKE_SRC_DIR)/Game/src/game.c || true
	# Fix itoa conflict with libc (rename to duke_itoa)
	sed -i 's/uint8_t  \*itoa(int value, uint8_t  \*string, int radix)/uint8_t *duke_itoa(int value, uint8_t *string, int radix)/' $(DUKE_SRC_DIR)/Game/src/global.c || true
	# Patch load_duke3d_groupfile to use hardcoded GRP path (bypass findGRPToUse/FixFilePath inlining)
	sed -i '/findGRPToUse(groupfilefullpath)/c\\tsprintf(groupfilefullpath, "%s/DUKE3D.GRP", getGameDir()[0] ? getGameDir() : "GAMES/DUKE3D");' $(DUKE_SRC_DIR)/Game/src/game.c || true
	sed -i '/FixFilePath(groupfilefullpath)/d' $(DUKE_SRC_DIR)/Game/src/game.c || true
	# Replace backslash path separators with forward slash (potatOS VFS uses /)
	sed -i 's|%s\\\\%s|%s/%s|g' $(DUKE_SRC_DIR)/Engine/src/filesystem.c $(DUKE_SRC_DIR)/Game/src/config.c $(DUKE_SRC_DIR)/Game/src/game.c $(DUKE_SRC_DIR)/Game/src/menues.c $(DUKE_SRC_DIR)/Game/src/premap.c || true
	sed -i 's|%s/%s\\\\%s|%s/%s/%s|g' $(DUKE_SRC_DIR)/Game/src/game.c || true

# Engine sources (replacing display.c with our display_potato.c)
DUKE_ENGINE_SRCS := $(addprefix $(DUKE_SRC_DIR)/Engine/src/, \
    cache.c draw.c dummy_multi.c engine.c filesystem.c \
    fixedPoint_math.c network.c tiles.c)

# Game sources
DUKE_GAME_SRCS := $(addprefix $(DUKE_SRC_DIR)/Game/src/, \
    actors.c animlib.c config.c console.c control.c \
    cvar_defs.c cvars.c game.c gamedef.c global.c \
    keyboard.c menues.c player.c premap.c rts.c \
    scriplib.c sector.c sounds.c)

# Audiolib sources (replacing dsl.c with our dsl_potato.c)
DUKE_AUDIO_SRCS := $(addprefix $(DUKE_SRC_DIR)/Game/src/audiolib/, \
    fx_man.c ll_man.c multivoc.c mv_mix.c mvreverb.c \
    nodpmi.c pitch.c user.c usrhooks.c)

# Our platform files
DUKE_POTATO_SRCS := $(DUKE_DIR)/display_potato.c \
                    $(DUKE_DIR)/dsl_potato.c \
                    $(DUKE_DIR)/music_potato.c

DUKE_ENGINE_OBJS := $(patsubst $(DUKE_SRC_DIR)/%.c, $(DUKE_BUILD)/%.o, $(DUKE_ENGINE_SRCS))
DUKE_GAME_OBJS   := $(patsubst $(DUKE_SRC_DIR)/%.c, $(DUKE_BUILD)/%.o, $(DUKE_GAME_SRCS))
DUKE_AUDIO_OBJS  := $(patsubst $(DUKE_SRC_DIR)/%.c, $(DUKE_BUILD)/%.o, $(DUKE_AUDIO_SRCS))
DUKE_POTATO_OBJS := $(patsubst $(DUKE_DIR)/%.c, $(DUKE_BUILD)/%.o, $(DUKE_POTATO_SRCS))

DUKE_ALL_OBJS := $(DUKE_ENGINE_OBJS) $(DUKE_GAME_OBJS) $(DUKE_AUDIO_OBJS) $(DUKE_POTATO_OBJS)

# Ensure clone happens before any Duke3D compilation
$(DUKE_ALL_OBJS): | $(DUKE_SRC_DIR)/Engine/src/engine.c

# Compile engine sources
$(DUKE_BUILD)/Engine/src/%.o: $(DUKE_SRC_DIR)/Engine/src/%.c
	mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS_DUKE) -o $@ $<

# Compile game sources (premap.c at -O0 to avoid crash)
$(DUKE_BUILD)/Game/src/premap.o: $(DUKE_SRC_DIR)/Game/src/premap.c
	mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS_DUKE) -O0 -o $@ $<

$(DUKE_BUILD)/Game/src/%.o: $(DUKE_SRC_DIR)/Game/src/%.c
	mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS_DUKE) -o $@ $<

# Compile audiolib sources
$(DUKE_BUILD)/Game/src/audiolib/%.o: $(DUKE_SRC_DIR)/Game/src/audiolib/%.c
	mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS_DUKE) -o $@ $<

# Compile potato platform sources
$(DUKE_BUILD)/%.o: $(DUKE_DIR)/%.c
	mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS_DUKE) -o $@ $<

$(DUKE_ELF): $(DUKE_ALL_OBJS) $(LIBC_CRT0) $(LIBC_A) src/userspace/libc/libc.ld
	mkdir -p dist/userspace
	$(LD) --no-relax --allow-multiple-definition \
	      -T src/userspace/libc/libc.ld -o $@ \
	      $(LIBC_CRT0) $(DUKE_POTATO_OBJS) $(DUKE_ENGINE_OBJS) $(DUKE_GAME_OBJS) $(DUKE_AUDIO_OBJS) $(LIBC_A)

duke3d: $(DUKE_ELF)

DUKE_GRP ?= assets/duke3d.grp

# Path to Quake PAK0.PAK (user-supplied; copyrighted — cannot auto-download).
# Copy PAK0.PAK into assets/ before running make disk.img.
# Override: make disk.img QUAKE_PAK=/path/to/pak0.pak
QUAKE_PAK ?= assets/PAK0.PAK

# Create FAT32 disk image with test files from bins folder
ASSET_FILES = assets/font.psf \
              assets/potato.raw \
              assets/potato.txt \
              assets/boot.raw

Q2_PAK0 ?= assets/quake2/pak0.pak
Q2_PAK1 ?= assets/quake2/pak1.pak
Q2_PAK2 ?= assets/quake2/pak2.pak

disk.img: $(ASSET_FILES) $(TEST_ELF_BIN) $(BLINK_ELF_BIN) $(SIMPLE_BINS) $(DOOM_ELF) $(DOOM_WAD) $(QUAKE_ELF) $(Q2_ELF) $(DUKE_ELF)
	@echo "Creating FAT32 disk image..."
	dd if=/dev/zero of=disk.img bs=1M count=512 2>/dev/null
	mkfs.vfat -F 32 -n "POTATDISK" disk.img
	mmd -i disk.img ::SYS ::BIN ::GAMES ::GAMES/DOOM ::GAMES/QUAKE ::GAMES/QUAKE/ID1 ::GAMES/SNAKE ::GAMES/QUAKE2 ::GAMES/QUAKE2/BASEQ2 ::GAMES/DUKE3D
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
	copy_file $(DOOM_ELF)                    GAMES/DOOM/DOOM.ELF; \
	copy_file $(DOOM_WAD)                    GAMES/DOOM/DOOM1.WAD; \
	copy_file dist/userspace/snake.elf       GAMES/SNAKE/SNAKE.ELF; \
	copy_file $(QUAKE_ELF)                   GAMES/QUAKE/QUAKE.ELF; \
	copy_file $(QUAKE_PAK)                   GAMES/QUAKE/ID1/PAK0.PAK; \
	copy_file $(Q2_ELF)                      GAMES/QUAKE2/QUAKE2.ELF; \
	copy_file $(Q2_PAK0)                     GAMES/QUAKE2/BASEQ2/PAK0.PAK; \
	copy_file $(Q2_PAK1)                     GAMES/QUAKE2/BASEQ2/PAK1.PAK; \
	copy_file $(Q2_PAK2)                     GAMES/QUAKE2/BASEQ2/PAK2.PAK; \
	copy_file $(DUKE_ELF)                    GAMES/DUKE3D/DUKE3D.ELF; \
	copy_file $(DUKE_GRP)                    GAMES/DUKE3D/DUKE3D.GRP
	@echo "Disk image created with directory hierarchy:"
	@mdir -i disk.img ::

run: all disk.img
	$(QEMU) -cdrom dist/x86_64/kernel.iso -drive file=disk.img,format=raw,if=ide,media=disk -boot order=d -serial stdio $(QEMU_OPTIONS)

rundisk: build-x86_64 disk.img
	$(QEMU) -kernel dist/x86_64/kernel.bin -drive file=disk.img,format=raw,if=ide,media=disk -serial stdio $(QEMU_OPTIONS)
