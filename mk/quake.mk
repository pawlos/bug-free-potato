# ── Quake ──────────────────────────────────────────────────────────────────────
QUAKE_DIR    = src/userspace/quake
QUAKEGEN_DIR = $(QUAKE_DIR)/quakegeneric/source
QUAKE_BUILD  = build/userspace/quake
QUAKE_ELF    = dist/userspace/quake.elf

CFLAGS_QUAKE = -ffreestanding -fno-stack-protector -fno-builtin \
               -fno-asynchronous-unwind-tables \
               -m64 -nostdlib -w \
               -I src/userspace/libc \
               -I src/userspace \
               -I $(QUAKEGEN_DIR)

$(QUAKEGEN_DIR)/quakegeneric.h:
	git clone --depth=1 https://github.com/erysdren/quakegeneric.git $(QUAKE_DIR)/quakegeneric
	sed -i 's/parms\.memsize = 8\*1024\*1024/parms.memsize = 64*1024*1024/' $(QUAKEGEN_DIR)/quakegeneric.c

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

$(QUAKE_BUILD)/qg_%.o: $(QUAKEGEN_DIR)/%.c $(QUAKEGEN_DIR)/quakegeneric.h
	mkdir -p $(QUAKE_BUILD)
	$(CC) -c $(CFLAGS_QUAKE) -o $@ $<

$(QUAKE_BUILD)/quakegeneric_potato.o: $(QUAKE_DIR)/quakegeneric_potato.c $(QUAKEGEN_DIR)/quakegeneric.h
	mkdir -p $(QUAKE_BUILD)
	$(CC) -c $(CFLAGS_QUAKE) -o $@ $<

$(QUAKE_ELF): $(QUAKEGEN_OBJS) $(QUAKE_BUILD)/quakegeneric_potato.o $(LIBC_CRT0) $(LIBC_A) src/userspace/libc/libc.ld
	mkdir -p dist/userspace
	$(LD) --no-relax --wrap=Sys_Error --wrap=Sys_Printf \
	      -T src/userspace/libc/libc.ld -o $@ \
	      $(LIBC_CRT0) $(QUAKE_BUILD)/quakegeneric_potato.o $(QUAKEGEN_OBJS) $(LIBC_A)

quake: $(QUAKE_ELF)
