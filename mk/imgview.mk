# ── Image Viewer ─────────────────────────────────────────────────────────
IMGVIEW_DIR   = src/userspace/imgview
IMGVIEW_BUILD = build/userspace/imgview
IMGVIEW_ELF   = dist/userspace/imgview.elf

$(IMGVIEW_BUILD)/imgview.o: $(IMGVIEW_DIR)/imgview.c $(IMGVIEW_DIR)/stb_image.h
	mkdir -p $(IMGVIEW_BUILD)
	$(CC) -c $(CFLAGS_USER) -I src/userspace/libc -O2 -w -o $@ $<

$(IMGVIEW_ELF): $(IMGVIEW_BUILD)/imgview.o $(LIBC_CRT0) $(LIBC_A) src/userspace/libc/libc.ld
	mkdir -p dist/userspace
	$(LD) --no-relax -T src/userspace/libc/libc.ld -o $@ \
	      $(LIBC_CRT0) $< $(LIBC_A)

imgview: $(IMGVIEW_ELF)
