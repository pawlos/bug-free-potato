# ── Doom ──────────────────────────────────────────────────────────────────────
DOOM_DIR    = src/userspace/doom
DOOMGEN_DIR = $(DOOM_DIR)/doomgeneric/doomgeneric
DOOM_BUILD  = build/userspace/doom
DOOM_ELF    = dist/userspace/doom.elf
DOOM_WAD    = assets/doom1.wad

CFLAGS_DOOM = -ffreestanding -fno-stack-protector -fno-builtin \
              -fno-asynchronous-unwind-tables \
              -m64 -nostdlib -w \
              -include stddef.h \
              -DFEATURE_SOUND \
              -I src/userspace/libc \
              -I src/userspace \
              -I $(DOOMGEN_DIR)

$(DOOMGEN_DIR)/doomgeneric.h:
	git clone --depth=1 https://github.com/ozkl/doomgeneric.git $(DOOM_DIR)/doomgeneric

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

$(DOOM_BUILD)/dg_doomgeneric.o: $(DOOMGEN_DIR)/doomgeneric.c $(DOOMGEN_DIR)/doomgeneric.h
	mkdir -p $(DOOM_BUILD)
	$(CC) -c $(CFLAGS_DOOM) -Dmain=_dg_unused_main -o $@ $<

$(DOOM_BUILD)/dg_%.o: $(DOOMGEN_DIR)/%.c $(DOOMGEN_DIR)/doomgeneric.h
	mkdir -p $(DOOM_BUILD)
	$(CC) -c $(CFLAGS_DOOM) -o $@ $<

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

$(DOOM_WAD):
	@echo "Downloading shareware doom1.wad..."
	curl -L -o $@ "https://distro.ibiblio.org/slitaz/sources/packages/d/doom1.wad" || \
	  { echo "ERROR: Could not download doom1.wad. Place it manually at $(DOOM_WAD)"; exit 1; }

doom: $(DOOM_ELF)

doom-disk: $(DOOM_ELF) $(DOOM_WAD)
	@echo "doom.elf and doom1.wad ready for disk image"
