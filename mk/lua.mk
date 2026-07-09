# ── Lua 5.4 interpreter (userspace tool) ─────────────────────────────────
# Standalone userspace Lua: REPL + script runner, plus the potatos.* binding
# module. Cloned on demand (github.com/lua/lua keeps sources at repo root, no
# src/ subdir). Almost all adaptation lives in potato_lua.h (force-included);
# the only source patch is registering potatos in linit.c's loadedlibs[].
LUA_DIR      = src/userspace/lua
LUA_SRC_DIR  = $(LUA_DIR)/lua-src
LUA_BUILD    = build/userspace/lua
LUA_ELF      = dist/userspace/lua.elf
LUA_MARKER   = $(LUA_DIR)/.cloned

LUA_TAG     ?= v5.4.7
LUA_URL     ?= https://github.com/lua/lua.git

CFLAGS_LUA = -ffreestanding -fno-stack-protector -fno-builtin \
             -fno-asynchronous-unwind-tables \
             -m64 -nostdlib -O2 -w \
             -isystem src/userspace/libc \
             -I src/userspace \
             -I $(LUA_SRC_DIR) \
             -I $(LUA_DIR) \
             -include $(LUA_DIR)/potato_lua.h

# Stock Lua 5.4 compilation units. lua.c = standalone frontend (REPL/script
# runner); luac.c = bytecode compiler — excluded.
LUA_CORE = lapi lcode lctype ldebug ldo ldump lfunc lgc llex lmem \
           lobject lopcodes lparser lstate lstring ltable ltm lundump \
           lvm lzio
LUA_LIB  = lauxlib lbaselib lcorolib ldblib liolib lmathlib loadlib \
           loslib lstrlib ltablib lutf8lib linit

LUA_C_OBJS    = $(addprefix $(LUA_BUILD)/, $(addsuffix .o, $(LUA_CORE) $(LUA_LIB)))
LUA_FRONT_OBJ = $(LUA_BUILD)/lua.o
LUA_GLUE_OBJ  = $(LUA_BUILD)/potatos.o
LUA_LIBC_OBJ  = $(LUA_BUILD)/potato_libc.o

# Clone + patch. Marker guards against re-clone; sources appear at LUA_SRC_DIR.
$(LUA_MARKER):
	git clone --depth=1 --branch $(LUA_TAG) $(LUA_URL) $(LUA_SRC_DIR)
	@echo "Applying potatOS patch to Lua $(LUA_TAG) (register potatos module)..."
	# Register potatos in loadedlibs[] so luaL_openlibs opens it as a global.
	# LUA_POTATOSLIBNAME + luaopen_potatos come from potato_lua.h (force-included).
	sed -i 's|{LUA_LOADLIBNAME, luaopen_package},|{LUA_LOADLIBNAME, luaopen_package},\n  {LUA_POTATOSLIBNAME, luaopen_potatos},|' $(LUA_SRC_DIR)/linit.c
	@echo "Patch applied."
	@touch $(LUA_MARKER)

$(LUA_BUILD)/%.o: $(LUA_SRC_DIR)/%.c $(LUA_MARKER) $(LUA_DIR)/potato_lua.h
	mkdir -p $(LUA_BUILD)
	$(CC) -c $(CFLAGS_LUA) -o $@ $<

$(LUA_GLUE_OBJ): $(LUA_DIR)/potatos.c $(LUA_MARKER) $(LUA_DIR)/potato_lua.h
	mkdir -p $(LUA_BUILD)
	$(CC) -c $(CFLAGS_LUA) -o $@ $<

$(LUA_LIBC_OBJ): $(LUA_DIR)/potato_libc.c $(LUA_MARKER) $(LUA_DIR)/potato_lua.h
	mkdir -p $(LUA_BUILD)
	$(CC) -c $(CFLAGS_LUA) -o $@ $<

$(LUA_ELF): $(LUA_C_OBJS) $(LUA_FRONT_OBJ) $(LUA_GLUE_OBJ) $(LUA_LIBC_OBJ) $(LIBC_CRT0) $(LIBC_A) src/userspace/libc/libc.ld
	mkdir -p dist/userspace
	$(LD) --no-relax -T src/userspace/libc/libc.ld -o $@ \
	      $(LIBC_CRT0) $(LUA_C_OBJS) $(LUA_FRONT_OBJ) $(LUA_GLUE_OBJ) $(LUA_LIBC_OBJ) $(LIBC_A)

# Two-phase build: the LUA_*_OBJS paths reference sources that don't exist
# until the clone runs. First recursive make creates the marker (clone+patch);
# the second sees the freshly-cloned sources. Mirrors mk/wolf3d.mk.
lua:
	@$(MAKE) --no-print-directory -f Makefile $(LUA_MARKER)
	@$(MAKE) --no-print-directory -f Makefile $(LUA_ELF)

clean-lua:
	-rm -rf $(LUA_BUILD) $(LUA_ELF)

# Re-clone from scratch (removes the cloned source + marker).
distclean-lua:
	-rm -rf $(LUA_BUILD) $(LUA_ELF) $(LUA_SRC_DIR) $(LUA_MARKER)
