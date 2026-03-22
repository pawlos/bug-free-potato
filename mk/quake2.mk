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

$(Q2_SRC_DIR)/qcommon/qcommon.h:
	git clone --depth=1 https://github.com/id-Software/Quake-2.git $(Q2_SRC_DIR)
	sed -i 's/sizeof (pv)/sizeof (*pv)/' $(Q2_SRC_DIR)/ref_soft/r_poly.c

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

$(Q2_BUILD)/%.o: $(Q2_SRC_DIR)/%.c $(Q2_SRC_DIR)/qcommon/qcommon.h
	mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS_Q2) -o $@ $<

$(Q2_BUILD)/q2_potato.o: $(Q2_DIR)/q2_potato.c $(Q2_SRC_DIR)/qcommon/qcommon.h
	mkdir -p $(Q2_BUILD)
	$(CC) -c $(CFLAGS_Q2) -o $@ $<

$(Q2_ELF): $(Q2_ENGINE_OBJS) $(Q2_POTATO_OBJS) $(LIBC_CRT0) $(LIBC_A) src/userspace/libc/libc.ld
	mkdir -p dist/userspace
	$(LD) --no-relax --allow-multiple-definition \
	      -T src/userspace/libc/libc.ld -o $@ \
	      $(LIBC_CRT0) $(Q2_POTATO_OBJS) $(Q2_ENGINE_OBJS) $(LIBC_A)

quake2: $(Q2_ELF)
