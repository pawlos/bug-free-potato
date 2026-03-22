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
	sed -i 's/(int32_t)FP_OFF/(intptr_t)FP_OFF/g;s/(int32_t) FP_OFF/(intptr_t) FP_OFF/g' $(DUKE_SRC_DIR)/Engine/src/engine.c
	sed -i 's/int32_t globalpalwritten/intptr_t globalpalwritten/' $(DUKE_SRC_DIR)/Engine/src/engine.c
	sed -i 's/(int32_t)globalpalwritten/(intptr_t)globalpalwritten/' $(DUKE_SRC_DIR)/Engine/src/engine.c
	sed -i 's/suckcache((int32_t \*)screen)/suckcache((intptr_t *)screen)/' $(DUKE_SRC_DIR)/Engine/src/engine.c
	sed -i 's/#error Define your platform!/#include "potato_compat.h"/' $(DUKE_SRC_DIR)/Engine/src/platform.h
	sed -i 's|<fcntl.h>|"libc/file.h"|;s|<unistd.h>|"libc/stdlib.h"|' $(DUKE_SRC_DIR)/Game/src/sounds.c || true
	sed -i 's|<fcntl.h>|"libc/file.h"|;s|<unistd.h>|"libc/stdlib.h"|' $(DUKE_SRC_DIR)/Game/src/config.c || true
	sed -i 's|<signal.h>|"libc/signal.h"|;s|<sys/stat.h>|"libc/sys/stat.h"|' $(DUKE_SRC_DIR)/Game/src/game.c || true
	sed -i 's/uint8_t  \*itoa(int value, uint8_t  \*string, int radix)/uint8_t *duke_itoa(int value, uint8_t *string, int radix)/' $(DUKE_SRC_DIR)/Game/src/global.c || true
	sed -i '/findGRPToUse(groupfilefullpath)/c\\tsprintf(groupfilefullpath, "%s/DUKE3D.GRP", getGameDir()[0] ? getGameDir() : "GAMES/DUKE3D");' $(DUKE_SRC_DIR)/Game/src/game.c || true
	sed -i '/FixFilePath(groupfilefullpath)/d' $(DUKE_SRC_DIR)/Game/src/game.c || true
	sed -i 's|%s\\\\%s|%s/%s|g' $(DUKE_SRC_DIR)/Engine/src/filesystem.c $(DUKE_SRC_DIR)/Game/src/config.c $(DUKE_SRC_DIR)/Game/src/game.c $(DUKE_SRC_DIR)/Game/src/menues.c $(DUKE_SRC_DIR)/Game/src/premap.c || true
	sed -i 's|%s/%s\\\\%s|%s/%s/%s|g' $(DUKE_SRC_DIR)/Game/src/game.c || true

DUKE_ENGINE_SRCS := $(addprefix $(DUKE_SRC_DIR)/Engine/src/, \
    cache.c draw.c dummy_multi.c engine.c filesystem.c \
    fixedPoint_math.c network.c tiles.c)

DUKE_GAME_SRCS := $(addprefix $(DUKE_SRC_DIR)/Game/src/, \
    actors.c animlib.c config.c console.c control.c \
    cvar_defs.c cvars.c game.c gamedef.c global.c \
    keyboard.c menues.c player.c premap.c rts.c \
    scriplib.c sector.c sounds.c)

DUKE_AUDIO_SRCS := $(addprefix $(DUKE_SRC_DIR)/Game/src/audiolib/, \
    fx_man.c ll_man.c multivoc.c mv_mix.c mvreverb.c \
    nodpmi.c pitch.c user.c usrhooks.c)

DUKE_POTATO_SRCS := $(DUKE_DIR)/display_potato.c \
                    $(DUKE_DIR)/dsl_potato.c \
                    $(DUKE_DIR)/music_potato.c

DUKE_ENGINE_OBJS := $(patsubst $(DUKE_SRC_DIR)/%.c, $(DUKE_BUILD)/%.o, $(DUKE_ENGINE_SRCS))
DUKE_GAME_OBJS   := $(patsubst $(DUKE_SRC_DIR)/%.c, $(DUKE_BUILD)/%.o, $(DUKE_GAME_SRCS))
DUKE_AUDIO_OBJS  := $(patsubst $(DUKE_SRC_DIR)/%.c, $(DUKE_BUILD)/%.o, $(DUKE_AUDIO_SRCS))
DUKE_POTATO_OBJS := $(patsubst $(DUKE_DIR)/%.c, $(DUKE_BUILD)/%.o, $(DUKE_POTATO_SRCS))

DUKE_ALL_OBJS := $(DUKE_ENGINE_OBJS) $(DUKE_GAME_OBJS) $(DUKE_AUDIO_OBJS) $(DUKE_POTATO_OBJS)

$(DUKE_ALL_OBJS): | $(DUKE_SRC_DIR)/Engine/src/engine.c

$(DUKE_BUILD)/Engine/src/%.o: $(DUKE_SRC_DIR)/Engine/src/%.c
	mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS_DUKE) -o $@ $<

$(DUKE_BUILD)/Game/src/premap.o: $(DUKE_SRC_DIR)/Game/src/premap.c
	mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS_DUKE) -O0 -o $@ $<

$(DUKE_BUILD)/Game/src/%.o: $(DUKE_SRC_DIR)/Game/src/%.c
	mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS_DUKE) -o $@ $<

$(DUKE_BUILD)/Game/src/audiolib/%.o: $(DUKE_SRC_DIR)/Game/src/audiolib/%.c
	mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS_DUKE) -o $@ $<

$(DUKE_BUILD)/%.o: $(DUKE_DIR)/%.c
	mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS_DUKE) -o $@ $<

$(DUKE_ELF): $(DUKE_ALL_OBJS) $(LIBC_CRT0) $(LIBC_A) src/userspace/libc/libc.ld
	mkdir -p dist/userspace
	$(LD) --no-relax --allow-multiple-definition \
	      -T src/userspace/libc/libc.ld -o $@ \
	      $(LIBC_CRT0) $(DUKE_POTATO_OBJS) $(DUKE_ENGINE_OBJS) $(DUKE_GAME_OBJS) $(DUKE_AUDIO_OBJS) $(LIBC_A)

duke3d: $(DUKE_ELF)
