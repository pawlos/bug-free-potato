# -- ScummVM (Sky + SCUMM engines, SDL2 backend) ------------------------------
# Builds a stripped-down ScummVM with Sky and SCUMM engines. Uses our SDL2
# shim (Wolf4SDL proved end-to-end audio + video). Configure runs once at
# clone time; the source list is captured into .sources for the per-file rules.
SCUMMVM_DIR     = src/userspace/scummvm
SCUMMVM_SRC_DIR = $(SCUMMVM_DIR)/scummvm-src
SCUMMVM_BUILD   = build/userspace/scummvm
SCUMMVM_ELF     = dist/userspace/scummvm.elf

SCUMMVM_URL ?= https://github.com/scummvm/scummvm.git
# Pin to last SDL2-compatible release. Master moved to SDL3 (SDL_AudioStream,
# SDL_Gamepad, SDL_GetWindowMouseGrab, ...) which our SDL2 shim doesn't cover.
SCUMMVM_TAG ?= v2.9.1

# Game data path on disk image. ScummVM's DATA_PATH define is used for the
# global theme/extras directory; per-game data lives next to it.
SCUMMVM_DATA_PATH ?= GAMES/SCUMMVM

CFLAGS_SCUMMVM = -ffreestanding -fno-stack-protector -fno-builtin \
                 -fno-asynchronous-unwind-tables \
                 -m64 -nostdlib -O2 -w \
                 -isystem src/userspace/libc \
                 -I src/userspace/libc/SDL2 \
                 -I src/userspace \
                 -I $(SCUMMVM_SRC_DIR) \
                 -I $(SCUMMVM_SRC_DIR)/engines \
                 -I $(SCUMMVM_SRC_DIR)/backends/platform/sdl \
                 -DHAVE_CONFIG_H \
                 -DSDL_BACKEND -DUSE_SDL2 \
                 -DPOSIX -DPOTATOS \
                 -DENABLE_SKY=STATIC_PLUGIN \
                 -DENABLE_SCUMM=STATIC_PLUGIN \
                 -DDATA_PATH=\"$(SCUMMVM_DATA_PATH)\" \
                 -DPLUGIN_DIRECTORY=\"$(SCUMMVM_DATA_PATH)\"

CXXFLAGS_SCUMMVM = $(CFLAGS_SCUMMVM) \
                   -fno-exceptions -fpermissive \
                   -std=c++11 -fno-operator-names

# Marker file: the clone+configure step runs once and writes the source list
# alongside it. Subsequent builds reuse the captured list without re-cloning.
SCUMMVM_MARKER  = $(SCUMMVM_DIR)/.cloned
SCUMMVM_SOURCES = $(SCUMMVM_DIR)/.sources

$(SCUMMVM_MARKER):
	mkdir -p $(SCUMMVM_DIR)
	# Clone if missing, otherwise hard-reset to the pinned tag (undoes any
	# prior in-place sed patches so the marker rule is idempotent).
	if [ ! -d $(SCUMMVM_SRC_DIR)/.git ]; then \
	  git clone --depth=1 --branch $(SCUMMVM_TAG) $(SCUMMVM_URL) $(SCUMMVM_SRC_DIR); \
	else \
	  cd $(SCUMMVM_SRC_DIR) && git fetch --depth=1 origin tag $(SCUMMVM_TAG) && \
	    git reset --hard $(SCUMMVM_TAG); \
	fi
	test -d $(SCUMMVM_SRC_DIR)/.git  # fail loudly if clone failed
	# Run upstream configure to generate config.h, config.mk, engines.mk,
	# detection_table.h, plugins_table.h. Disable everything we don't have.
	cd $(SCUMMVM_SRC_DIR) && ./configure \
	    --backend=sdl --disable-all-engines --enable-engine=sky --enable-engine=scumm \
	    --disable-mt32emu --disable-lua --disable-cloud \
	    --disable-system-dialogs --disable-translation \
	    --disable-detection-full --disable-savegame-timestamp \
	    --disable-hq-scalers --disable-edge-scalers \
	    --disable-aspect --disable-taskbar --disable-debug \
	    --disable-vorbis --disable-tremor --disable-flac --disable-mad \
	    --disable-libcurl --disable-sdlnet --disable-png --disable-jpeg \
	    --disable-theoradec --disable-faad --disable-fluidsynth --disable-fluidlite \
	    --disable-mpeg2 --disable-a52 --disable-vpx --disable-mikmod --disable-openmpt \
	    --disable-bink --disable-text-console
	# Strip host-detected feature flags (configure auto-detects ALSA, OpenGL,
	# CURL, etc. on the build host — none of which exist on potatOS).
	cd $(SCUMMVM_SRC_DIR) && sed -i \
	    -e 's/^USE_ALSA = 1/# USE_ALSA disabled/' \
	    -e 's/^USE_OPENGL = 1/# USE_OPENGL disabled/' \
	    -e 's/^USE_GLAD = 1/# USE_GLAD disabled/' \
	    -e 's/^USE_OPENGL_GAME = 1/# USE_OPENGL_GAME disabled/' \
	    -e 's/^USE_OPENGL_SHADERS = 1/# USE_OPENGL_SHADERS disabled/' \
	    -e 's/^USE_LINUXCD = 1/# USE_LINUXCD disabled/' \
	    -e 's/^USE_CURL = 1/# USE_CURL disabled/' \
	    -e 's/^USE_SYSTEM_PRINTING = 1/# USE_SYSTEM_PRINTING disabled/' \
	    -e 's/^USE_ENET = 1/# USE_ENET disabled/' \
	    -e 's/^USE_FRIBIDI = 1/# USE_FRIBIDI disabled/' \
	    -e 's/^USE_FREETYPE2 = 1/# USE_FREETYPE2 disabled/' \
	    -e 's/^USE_TINYGL = 1/# USE_TINYGL disabled/' \
	    -e 's/^USE_ZLIB = 1/# USE_ZLIB disabled/' \
	    -e 's/^SCUMMVM_SSE2 = 1/# SCUMMVM_SSE2 disabled/' \
	    -e 's/^SCUMMVM_AVX2 = 1/# SCUMMVM_AVX2 disabled/' \
	    -e 's/^# USE_SCALERS = 1/USE_SCALERS = 1/' \
	    config.mk
	cd $(SCUMMVM_SRC_DIR) && sed -i \
	    -e 's|^#define USE_ALSA|#undef USE_ALSA|' \
	    -e 's|^#define USE_OPENGL|#undef USE_OPENGL|' \
	    -e 's|^#define USE_GLAD|#undef USE_GLAD|' \
	    -e 's|^#define USE_OPENGL_GAME|#undef USE_OPENGL_GAME|' \
	    -e 's|^#define USE_OPENGL_SHADERS|#undef USE_OPENGL_SHADERS|' \
	    -e 's|^#define USE_LINUXCD|#undef USE_LINUXCD|' \
	    -e 's|^#define USE_CURL|#undef USE_CURL|' \
	    -e 's|^#define USE_SYSTEM_PRINTING|#undef USE_SYSTEM_PRINTING|' \
	    -e 's|^#define USE_SEQ_MIDI|#undef USE_SEQ_MIDI|' \
	    -e 's|^#define USE_SNDIO|#undef USE_SNDIO|' \
	    -e 's|^#define USE_TIMIDITY|#undef USE_TIMIDITY|' \
	    -e 's|^#define SCUMMVM_SSE2|#undef SCUMMVM_SSE2|' \
	    -e 's|^#define SCUMMVM_AVX2|#undef SCUMMVM_AVX2|' \
	    -e 's|^#define USE_ENET|#undef USE_ENET|' \
	    -e 's|^#define USE_FRIBIDI|#undef USE_FRIBIDI|' \
	    -e 's|^#define USE_FREETYPE2|#undef USE_FREETYPE2|' \
	    -e 's|^#define USE_TINYGL|#undef USE_TINYGL|' \
	    -e 's|^#define USE_ZLIB|#undef USE_ZLIB|' \
	    -e 's|^#undef USE_SCALERS$$|#define USE_SCALERS|' \
	    config.h
	# Capture the source list from upstream's make-by-module logic. After the
	# config.mk patches above, `make -n` walks every module.mk and emits the
	# exact .cpp files we need to compile.
	cd $(SCUMMVM_SRC_DIR) && $(MAKE) -n 2>&1 \
	    | grep -oE 'c [^ ]+\.cpp' \
	    | sed 's|^c ||' | sort -u > ../.sources
	# Squelch HOME/$XDG_* lookups in the posix backend (no env vars on potatOS).
	-grep -rl 'getenv("HOME")' $(SCUMMVM_SRC_DIR)/backends 2>/dev/null \
	    | xargs -r sed -i 's|getenv("HOME")|((char*)0)|g'
	@touch $(SCUMMVM_MARKER)

# Two-phase: $(shell cat ...) is evaluated when the Makefile is parsed, which
# happens before the marker rule has run on first invocation. The recursive
# make call in the `scummvm` target ensures the second pass sees the file.
SCUMMVM_SRCS_RAW := $(shell [ -f $(SCUMMVM_SOURCES) ] && cat $(SCUMMVM_SOURCES))
# Drop only the subtrees that pull in host-OS headers we can't satisfy
# (network sockets) or are gated entirely by USE_* defines we've turned off.
# Things like macgui / audiocd / fmtowns plugins must stay because plugin
# tables and gui widgets reference them unconditionally even when their
# user-facing features are disabled.
SCUMMVM_DROP := graphics/tinygl/% \
                backends/networking/% backends/cloud/% backends/dlc/%
SCUMMVM_SRCS := $(filter-out $(SCUMMVM_DROP),$(SCUMMVM_SRCS_RAW))
SCUMMVM_OBJS := $(patsubst %.cpp,$(SCUMMVM_BUILD)/%.o,$(SCUMMVM_SRCS)) \
                $(SCUMMVM_BUILD)/cxxrt.o

$(SCUMMVM_BUILD)/cxxrt.o: $(SCUMMVM_DIR)/cxxrt.cpp
	@mkdir -p $(SCUMMVM_BUILD)
	g++ -c $(CXXFLAGS_SCUMMVM) -o $@ $<

$(SCUMMVM_BUILD)/%.o: $(SCUMMVM_SRC_DIR)/%.cpp $(SCUMMVM_MARKER)
	@mkdir -p $(dir $@)
	g++ -c $(CXXFLAGS_SCUMMVM) -o $@ $<

LIBSTDCXX_A ?= $(shell g++ -print-file-name=libstdc++.a)
LIBGCC_A    ?= $(shell g++ -print-file-name=libgcc.a)

$(SCUMMVM_ELF): $(SCUMMVM_OBJS) $(LIBC_CRT0) $(LIBC_A) src/userspace/libc/libc.ld
	@mkdir -p dist/userspace
	$(LD) --no-relax --allow-multiple-definition -T src/userspace/libc/libc.ld -o $@ \
	    $(LIBC_CRT0) $(SCUMMVM_OBJS) \
	    --start-group $(LIBC_A) $(LIBSTDCXX_A) $(LIBGCC_A) --end-group

# Two-phase: first pass clones+configures, second pass compiles+links with the
# freshly-captured source list. Mirrors mk/wolf3d.mk.
scummvm:
	@$(MAKE) --no-print-directory -f Makefile $(SCUMMVM_MARKER)
	@$(MAKE) --no-print-directory -f Makefile $(SCUMMVM_ELF)

clean-scummvm:
	-rm -rf $(SCUMMVM_BUILD) $(SCUMMVM_ELF)

distclean-scummvm:
	-rm -rf $(SCUMMVM_DIR)/scummvm-src $(SCUMMVM_DIR)/.cloned $(SCUMMVM_DIR)/.sources
	-rm -rf $(SCUMMVM_BUILD) $(SCUMMVM_ELF)
