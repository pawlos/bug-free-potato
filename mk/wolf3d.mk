# -- Wolf4SDL (Wolfenstein 3D) -----------------------------------------------
WOLF3D_DIR     = src/userspace/wolf3d
WOLF3D_SRC_DIR = $(WOLF3D_DIR)/wolf4sdl-src
WOLF3D_BUILD   = build/userspace/wolf3d
WOLF3D_ELF     = dist/userspace/wolf3d.elf

# Override at the command line if you have a different Wolf4SDL fork:
#   make wolf3d WOLF4SDL_URL=https://github.com/your-fork/Wolf4SDL.git
WOLF4SDL_URL  ?= https://github.com/fabiangreffrath/wolf4sdl.git

CFLAGS_WOLF3D = -ffreestanding -fno-stack-protector -fno-builtin \
                -fno-asynchronous-unwind-tables \
                -m64 -nostdlib -O2 -w \
                -isystem src/userspace/libc \
                -I src/userspace/libc/SDL2 \
                -I src/userspace \
                -I $(WOLF3D_SRC_DIR)

CXXFLAGS_WOLF3D = $(CFLAGS_WOLF3D) -fno-rtti -fno-exceptions -fpermissive

# Marker file used as the clone target. Lives outside the source dir to avoid
# colliding with any wl_main.c (which is a real Wolf4SDL source file).
WOLF3D_MARKER = $(WOLF3D_DIR)/.cloned

$(WOLF3D_MARKER):
	mkdir -p $(WOLF3D_DIR)
	git clone --depth=1 $(WOLF4SDL_URL) $(WOLF3D_SRC_DIR)
	# Path remap: where Wolf4SDL hardcodes data files, send them to the FAT
	# layout we use (GAMES/WOLF3D/...). Patches are tolerant of files that
	# may not exist in every fork; redirect to /dev/null on failure.
	# Core game data files (SHAREWARE = .WL1, FULL = .WL6 — same engine).
	# Wolf4SDL builds the filename as e.g. "audiohed." + "wl6". Remap the
	# lowercase prefix to GAMES/WOLF3D/ + uppercase, matching disk-image layout.
	# Bump local filename buffers from 13 to 64 bytes BEFORE the path remap.
	# Wolf4SDL assumes 8.3 names, but the path remap below prefixes
	# "GAMES/WOLF3D/" (13 chars), which would overflow a 13-byte buffer when
	# strcpy'd via gdictname/gheadname/etc.  Run this first so the path-remap
	# sed doesn't have to match the new sized literals.
	sed -i 's|char fname\[13\]|char fname[64]|g' \
	    $(WOLF3D_SRC_DIR)/id_ca.cpp \
	    $(WOLF3D_SRC_DIR)/id_pm.cpp
	sed -i \
	    -e 's|"audiohed\.|"GAMES/WOLF3D/AUDIOHED.|g' \
	    -e 's|"audiot\.|"GAMES/WOLF3D/AUDIOT.|g' \
	    -e 's|"vswap\.|"GAMES/WOLF3D/VSWAP.|g' \
	    -e 's|"gamemaps\.|"GAMES/WOLF3D/GAMEMAPS.|g' \
	    -e 's|"maphead\.|"GAMES/WOLF3D/MAPHEAD.|g' \
	    -e 's|"vgagraph\.|"GAMES/WOLF3D/VGAGRAPH.|g' \
	    -e 's|"vgahead\.|"GAMES/WOLF3D/VGAHEAD.|g' \
	    -e 's|"vgadict\.|"GAMES/WOLF3D/VGADICT.|g' \
	    -e 's|"config\.|"GAMES/WOLF3D/CONFIG.|g' \
	    $(WOLF3D_SRC_DIR)/id_ca.cpp \
	    $(WOLF3D_SRC_DIR)/id_pm.cpp \
	    $(WOLF3D_SRC_DIR)/wl_main.cpp \
	    $(WOLF3D_SRC_DIR)/wl_menu.cpp \
	    2>/dev/null
	# Lowercase extension WL1/WL6 → uppercase if Wolf4SDL builds via param.
	sed -i 's/"wl1"/"WL1"/g; s/"wl6"/"WL6"/g; s/"sod"/"SOD"/g; s/"sd1"/"SD1"/g; s/"sd2"/"SD2"/g; s/"sd3"/"SD3"/g' \
	    $(WOLF3D_SRC_DIR)/wl_main.cpp \
	    $(WOLF3D_SRC_DIR)/version.h \
	    2>/dev/null || true
	# Skip per-OS HOME/$XDG_CONFIG paths (no env var support on potatOS).
	# Find any function checking getenv("HOME") and short-circuit it.
	-grep -rl 'getenv("HOME")' $(WOLF3D_SRC_DIR) 2>/dev/null | xargs -r sed -i \
	    's|getenv("HOME")|((char\*)0)|g'
	# potatOS kernel doesn't pass argc/argv to user programs. Guard
	# CheckParameters with hardcoded defaults so it doesn't deref NULL.
	sed -i 's|CheckParameters(argc, argv);|{ static char *_av[] = {(char*)"wolf3d",0}; CheckParameters(1, _av); }|' \
	    $(WOLF3D_SRC_DIR)/wl_main.cpp
	# Replace the $HOME-based configdir block with a fixed potatOS path.
	# Wolf4SDL's CreateConfigDir asserts $HOME exists; we just drop config
	# files alongside the game data.
	python3 -c "import re,sys; p='$(WOLF3D_SRC_DIR)/wl_menu.cpp'; s=open(p).read(); \
	  s=re.sub(r'if\(configdir\[0\] == 0\)\s*\{[^{}]*?\{[^{}]*?\}[^{}]*?\{[^{}]*?\}[^{}]*?\}', \
	  'if(configdir[0]==0){snprintf(configdir,sizeof(configdir),\"GAMES/WOLF3D\");}', s, count=1, flags=re.DOTALL); \
	  open(p,'w').write(s)" 2>/dev/null || true
	# Mirror Quit() error messages to COM serial so they appear in the kernel
	# log (stdout goes to the windowed framebuffer, where they're invisible).
	# Declare serial_printf at file scope (extern "C" can't go inside a function)
	# then call it inside Quit() right after the error string is formatted.
	sed -i '/#include <SDL_syswm.h>/a\\nextern "C" int serial_printf(const char *fmt, ...);' \
	    $(WOLF3D_SRC_DIR)/wl_main.cpp
	sed -i 's|else error\[0\] = 0;|else error[0] = 0; serial_printf("[wolf3d Quit] %s\\n", error[0] ? error : "(no error)");|' \
	    $(WOLF3D_SRC_DIR)/wl_main.cpp
	# Touch marker so make sees it as up-to-date.
	@touch $(WOLF3D_MARKER)

WOLF3D_SRCS = $(wildcard $(WOLF3D_SRC_DIR)/*.c) $(wildcard $(WOLF3D_SRC_DIR)/*.cpp)
WOLF3D_OBJS = $(patsubst $(WOLF3D_SRC_DIR)/%.c, $(WOLF3D_BUILD)/%.o, $(filter %.c,$(WOLF3D_SRCS))) \
              $(patsubst $(WOLF3D_SRC_DIR)/%.cpp, $(WOLF3D_BUILD)/%.o, $(filter %.cpp,$(WOLF3D_SRCS))) \
              $(WOLF3D_BUILD)/cxxrt.o

$(WOLF3D_BUILD)/cxxrt.o: $(WOLF3D_DIR)/cxxrt.cpp
	mkdir -p $(WOLF3D_BUILD)
	g++ -c $(CXXFLAGS_WOLF3D) -o $@ $<

$(WOLF3D_BUILD)/%.o: $(WOLF3D_SRC_DIR)/%.c $(WOLF3D_MARKER)
	mkdir -p $(WOLF3D_BUILD)
	$(CC) -c $(CFLAGS_WOLF3D) -o $@ $<

$(WOLF3D_BUILD)/%.o: $(WOLF3D_SRC_DIR)/%.cpp $(WOLF3D_MARKER)
	mkdir -p $(WOLF3D_BUILD)
	g++ -c $(CXXFLAGS_WOLF3D) -o $@ $<

LIBSTDCXX_A ?= $(shell g++ -print-file-name=libstdc++.a)
LIBGCC_A    ?= $(shell g++ -print-file-name=libgcc.a)

$(WOLF3D_ELF): $(WOLF3D_OBJS) $(LIBC_CRT0) $(LIBC_A) src/userspace/libc/libc.ld
	mkdir -p dist/userspace
	$(LD) --no-relax --allow-multiple-definition -T src/userspace/libc/libc.ld -o $@ \
	    $(LIBC_CRT0) $(WOLF3D_OBJS) \
	    --start-group $(LIBC_A) $(LIBSTDCXX_A) $(LIBGCC_A) --end-group

# Two-phase build: $(wildcard ...) is evaluated when this Makefile is parsed,
# which is before the clone has run. So `make wolf3d` does the clone, then
# re-invokes make so the recursive pass sees the freshly-cloned sources.
wolf3d:
	@$(MAKE) --no-print-directory -f Makefile $(WOLF3D_MARKER)
	@$(MAKE) --no-print-directory -f Makefile $(WOLF3D_ELF)

clean-wolf3d:
	-rm -rf $(WOLF3D_BUILD) $(WOLF3D_ELF)

# Re-clone if you want to start fresh (rare; matches devilutionx convention).
distclean-wolf3d:
	-rm -rf $(WOLF3D_DIR) $(WOLF3D_BUILD) $(WOLF3D_ELF)
