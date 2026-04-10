# ── DevilutionX (Diablo) ──────────────────────────────────────────────────
DVLX_DIR     = src/userspace/devilutionx
DVLX_SRC_DIR = $(DVLX_DIR)/devilutionx-src/Source
DVLX_3RD_DIR = $(DVLX_DIR)/devilutionx-src/3rdParty
DVLX_BUILD   = build/userspace/devilutionx
DVLX_ELF     = dist/userspace/devilutionx.elf

CXXFLAGS_DVLX = -ffreestanding -fno-stack-protector -fno-builtin \
                -fno-asynchronous-unwind-tables \
                -m64 -nostdlib -fno-rtti -fno-exceptions -std=c++20 \
                -O2 -w -fpermissive -D__potatOS__ \
                -include $(DVLX_DIR)/dvlx_compat.h \
                -isystem src/userspace/libc \
                -I src/userspace/libc/SDL2 \
                -I src/userspace \
                -I $(DVLX_DIR) \
                -I $(DVLX_SRC_DIR) \
                -I $(DVLX_3RD_DIR) \
                -I $(DVLX_3RD_DIR)/tl \
                -I $(DVLX_3RD_DIR)/libsmackerdec/include \
                -I $(DVLX_3RD_DIR)/libmpq-src \
                -I $(DVLX_3RD_DIR)/PKWare

CFLAGS_DVLX_C = -ffreestanding -fno-stack-protector -fno-builtin \
                -fno-asynchronous-unwind-tables \
                -m64 -nostdlib -O2 -w \
                -D__potatOS__ \
                -isystem src/userspace/libc \
                -I $(DVLX_DIR) \
                -I $(DVLX_3RD_DIR)/libmpq-src

# All Source/*.cpp minus platform/feature files we don't support
DVLX_ALL_SRCS := $(shell find $(DVLX_SRC_DIR) -name '*.cpp' 2>/dev/null)
DVLX_EXCLUDE := $(wildcard \
    $(DVLX_SRC_DIR)/platform/ctr/*.cpp) $(wildcard \
    $(DVLX_SRC_DIR)/platform/switch/*.cpp) $(wildcard \
    $(DVLX_SRC_DIR)/platform/vita/*.cpp) $(wildcard \
    $(DVLX_SRC_DIR)/platform/android/*.cpp) $(wildcard \
    $(DVLX_SRC_DIR)/lua/*.cpp) $(wildcard \
    $(DVLX_SRC_DIR)/lua/modules/*.cpp) $(wildcard \
    $(DVLX_SRC_DIR)/discord/*.cpp) \
    $(DVLX_SRC_DIR)/dvlnet/tcp_client.cpp \
    $(DVLX_SRC_DIR)/dvlnet/tcp_server.cpp \
    $(DVLX_SRC_DIR)/dvlnet/protocol_zt.cpp \
    $(DVLX_SRC_DIR)/dvlnet/zerotier_lwip.cpp \
    $(DVLX_SRC_DIR)/dvlnet/zerotier_native.cpp \
    $(DVLX_SRC_DIR)/engine/sound_stubs.cpp \
    $(DVLX_SRC_DIR)/effects_stubs.cpp \
    $(DVLX_SRC_DIR)/utils/push_aulib_decoder.cpp \
    $(DVLX_SRC_DIR)/storm/storm_svid.cpp \
    $(DVLX_SRC_DIR)/utils/screen_reader.cpp \
    $(DVLX_SRC_DIR)/utils/utf8.cpp \
    $(DVLX_SRC_DIR)/utils/sdl2_to_1_2_backports.cpp
DVLX_SRCS := $(filter-out $(DVLX_EXCLUDE), $(DVLX_ALL_SRCS))

# 3rdParty sources (PKWare for MPQ decompression)
DVLX_3RD_SRCS := $(DVLX_3RD_DIR)/PKWare/explode.cpp \
                  $(DVLX_3RD_DIR)/PKWare/implode.cpp

# libmpq C sources
DVLX_LIBMPQ_SRCS := $(DVLX_3RD_DIR)/libmpq-src/libmpq/common.c \
                     $(DVLX_3RD_DIR)/libmpq-src/libmpq/explode.c \
                     $(DVLX_3RD_DIR)/libmpq-src/libmpq/extract.c \
                     $(DVLX_3RD_DIR)/libmpq-src/libmpq/huffman.c \
                     $(DVLX_3RD_DIR)/libmpq-src/libmpq/mpq.c \
                     $(DVLX_3RD_DIR)/libmpq-src/libmpq/wave.c

# Our platform/compat sources (C++ runtime, stubs, fmt, Aulib shim)
DVLX_POTATO_SRCS := $(DVLX_DIR)/cxxrt.cpp \
                     $(DVLX_DIR)/dvlx_stubs.cpp \
                     $(DVLX_DIR)/dvlx_fmt.cpp \
                     $(DVLX_DIR)/aulib_potato.cpp

# C-only stubs (avoid C++ header conflicts)
DVLX_POTATO_C_SRCS := $(DVLX_DIR)/dvlx_wchar.c

DVLX_OBJS := $(patsubst $(DVLX_SRC_DIR)/%.cpp, $(DVLX_BUILD)/src/%.o, $(DVLX_SRCS))
DVLX_3RD_OBJS := $(patsubst $(DVLX_3RD_DIR)/%.cpp, $(DVLX_BUILD)/3rd/%.o, $(DVLX_3RD_SRCS))
DVLX_LIBMPQ_OBJS := $(patsubst $(DVLX_3RD_DIR)/libmpq-src/libmpq/%.c, $(DVLX_BUILD)/3rd/libmpq/%.o, $(DVLX_LIBMPQ_SRCS))
DVLX_POTATO_OBJS := $(patsubst $(DVLX_DIR)/%.cpp, $(DVLX_BUILD)/potato/%.o, $(DVLX_POTATO_SRCS))
DVLX_POTATO_C_OBJS := $(patsubst $(DVLX_DIR)/%.c, $(DVLX_BUILD)/potato/%.o, $(DVLX_POTATO_C_SRCS))

$(DVLX_BUILD)/src/%.o: $(DVLX_SRC_DIR)/%.cpp
	mkdir -p $(dir $@)
	g++ -c $(CXXFLAGS_DVLX) -o $@ $<

# PKWare compiled as C to avoid C++ mangled memcpy
$(DVLX_BUILD)/3rd/PKWare/%.o: $(DVLX_3RD_DIR)/PKWare/%.cpp
	mkdir -p $(dir $@)
	gcc -x c -c -ffreestanding -fno-stack-protector -fno-builtin -m64 -nostdlib -O2 -w \
	    -I $(DVLX_3RD_DIR)/PKWare -o $@ $<

$(DVLX_BUILD)/3rd/%.o: $(DVLX_3RD_DIR)/%.cpp
	mkdir -p $(dir $@)
	g++ -c $(CXXFLAGS_DVLX) -o $@ $<

# libmpq compiled as C
$(DVLX_BUILD)/3rd/libmpq/%.o: $(DVLX_3RD_DIR)/libmpq-src/libmpq/%.c
	mkdir -p $(dir $@)
	gcc -c $(CFLAGS_DVLX_C) -o $@ $<

$(DVLX_BUILD)/potato/%.o: $(DVLX_DIR)/%.cpp
	mkdir -p $(dir $@)
	g++ -c $(CXXFLAGS_DVLX) -o $@ $<

$(DVLX_BUILD)/potato/%.o: $(DVLX_DIR)/%.c
	mkdir -p $(dir $@)
	gcc -c $(CFLAGS_DVLX_C) -o $@ $<

$(DVLX_ELF): $(DVLX_OBJS) $(DVLX_3RD_OBJS) $(DVLX_LIBMPQ_OBJS) $(DVLX_POTATO_OBJS) $(DVLX_POTATO_C_OBJS) $(LIBC_CRT0) $(LIBC_A) src/userspace/libc/libc.ld
	mkdir -p dist/userspace
	$(LD) --no-relax --allow-multiple-definition -N \
	      -T src/userspace/libc/libc.ld -o $@ \
	      $(LIBC_CRT0) $(DVLX_POTATO_OBJS) $(DVLX_POTATO_C_OBJS) $(DVLX_OBJS) $(DVLX_3RD_OBJS) $(DVLX_LIBMPQ_OBJS) \
	      --start-group \
	      $(LIBC_A) \
	      /usr/lib/x86_64-linux-gnu/libz.a \
	      /usr/lib/x86_64-linux-gnu/libbz2.a \
	      /usr/lib/gcc/x86_64-linux-gnu/12/libstdc++.a \
	      /usr/lib/gcc/x86_64-linux-gnu/12/libgcc.a \
	      --end-group

devilutionx: $(DVLX_ELF)
