# ── SDLPoP (Prince of Persia) ────────────────────────────────────────────
SDLPOP_DIR     = src/userspace/sdlpop
SDLPOP_SRC_DIR = $(SDLPOP_DIR)/SDLPoP-src/src
SDLPOP_BUILD   = build/userspace/sdlpop
SDLPOP_ELF     = dist/userspace/sdlpop.elf

CFLAGS_SDLPOP = -ffreestanding -fno-stack-protector -fno-builtin \
                -fno-asynchronous-unwind-tables \
                -m64 -nostdlib -O2 -w \
                -isystem src/userspace/libc \
                -I src/userspace/libc/SDL2 \
                -I src/userspace

# Clone SDLPoP source and apply patches for potatOS
$(SDLPOP_SRC_DIR)/seg000.c:
	git clone --depth=1 https://github.com/NagyD/SDLPoP.git $(SDLPOP_DIR)/SDLPoP-src
	# Remap data paths from data/ to GAMES/POP/DATA/ (FAT32 disk layout)
	sed -i 's|"data/|"GAMES/POP/DATA/|g' \
	    $(SDLPOP_SRC_DIR)/seg009.c \
	    $(SDLPOP_SRC_DIR)/lighting.c \
	    $(SDLPOP_SRC_DIR)/options.c \
	    $(SDLPOP_SRC_DIR)/menu.c
	# Remap config file paths
	sed -i 's|"SDLPoP.ini"|"GAMES/POP/SDLPOP.INI"|g; s|"SDLPoP.cfg"|"GAMES/POP/SDLPOP.CFG"|g' \
	    $(SDLPOP_SRC_DIR)/options.c \
	    $(SDLPOP_SRC_DIR)/seg009.c
	# Fix "%s/data/" fallback in find_first_file_match
	sed -i 's|%s/data/|%s/GAMES/POP/DATA/|g' $(SDLPOP_SRC_DIR)/seg009.c
	# Disable find_home_dir/find_share_dir (no HOME env, no /usr/share on potatOS)
	cd $(SDLPOP_SRC_DIR) && sed -i '/^void find_home_dir/,/^}/ c\void find_home_dir(void) { /* potatOS: no HOME */ }' seg009.c
	cd $(SDLPOP_SRC_DIR) && sed -i '/^void find_share_dir/,/^}/ c\void find_share_dir(void) { /* potatOS: no share */ }' seg009.c
	# potatOS: kernel doesn't set up argc/argv; use safe defaults
	cd $(SDLPOP_SRC_DIR) && sed -i 's/g_argc = argc;/g_argc = 1;/' main.c
	cd $(SDLPOP_SRC_DIR) && sed -i 's/g_argv = argv;/static char *_a[] = {"pop",0}; g_argv = _a;/' main.c
	# Skip PRINCE.EXE scanning (no DOS executable on potatOS)
	sed -i 's/load_dos_exe_modifications/\/\/load_dos_exe_modifications/g' $(SDLPOP_SRC_DIR)/options.c
	# No audio — check_sound_playing must return 0 to prevent infinite wait loops
	sed -i '/^int check_sound_playing/,/^}/ c\int check_sound_playing() { return 0; }' $(SDLPOP_SRC_DIR)/seg009.c

# Find all .c source files in src/
SDLPOP_SRCS = $(wildcard $(SDLPOP_SRC_DIR)/*.c)
SDLPOP_OBJS = $(patsubst $(SDLPOP_SRC_DIR)/%.c, $(SDLPOP_BUILD)/%.o, $(SDLPOP_SRCS))

$(SDLPOP_BUILD)/%.o: $(SDLPOP_SRC_DIR)/%.c $(SDLPOP_SRC_DIR)/seg000.c
	mkdir -p $(SDLPOP_BUILD)
	$(CC) -c $(CFLAGS_SDLPOP) -o $@ $<

$(SDLPOP_ELF): $(SDLPOP_OBJS) $(LIBC_CRT0) $(LIBC_A) src/userspace/libc/libc.ld
	mkdir -p dist/userspace
	$(LD) --no-relax -T src/userspace/libc/libc.ld -o $@ \
	    $(LIBC_CRT0) $(SDLPOP_OBJS) $(LIBC_A)

sdlpop: $(SDLPOP_ELF)
