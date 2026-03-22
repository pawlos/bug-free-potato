# ── MPEG-1 Video Player ──────────────────────────────────────────────────
PLAYER_DIR   = src/userspace/player
PLAYER_BUILD = build/userspace/player
PLAYER_ELF   = dist/userspace/player.elf

$(PLAYER_BUILD)/player.o: $(PLAYER_DIR)/player.c $(PLAYER_DIR)/pl_mpeg.h
	mkdir -p $(PLAYER_BUILD)
	$(CC) -c $(CFLAGS_USER) -I src/userspace/libc -O2 -w -o $@ $<

$(PLAYER_ELF): $(PLAYER_BUILD)/player.o $(LIBC_CRT0) $(LIBC_A) src/userspace/libc/libc.ld
	mkdir -p dist/userspace
	$(LD) --no-relax -T src/userspace/libc/libc.ld -o $@ \
	      $(LIBC_CRT0) $< $(LIBC_A)

player: $(PLAYER_ELF)
