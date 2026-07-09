/* potato_lua.h — force-included (-include) into every Lua translation unit.
   Adapts Lua 5.4 to the potatOS freestanding userspace environment.

   Because this header is processed before lua.c/luaconf.h/loadlib.c, every
   macro below lands before Lua's own `#if !defined(...)` guards, so Lua adopts
   our definitions and its default (POSIX/readline) blocks are skipped. This
   avoids patching the upstream sources for anything except registering the
   potatos module (a single sed on linit.c). */
#ifndef POTATO_LUA_H
#define POTATO_LUA_H

/* Name of our built-in binding module: require-able as "potatos". */
#define LUA_POTATOSLIBNAME "potatos"

/* --- Search paths (guarded by #if !defined in luaconf.h) ------------------ */
/* FAT32 disk layout; VFAT is case-insensitive so lowercase ?.lua matches
   uppercase 8.3 names like HELLO.LUA. No C modules (no dynamic linking). */
#define LUA_PATH_DEFAULT  "GAMES/LUA/?.lua;GAMES/LUA/?/init.lua;BIN/?.lua;./?.lua"
#define LUA_CPATH_DEFAULT ""

/* --- REPL line input (guarded by #if !defined(lua_readline) in lua.c) ----- */
/* libc stdin is a stub, so Lua's default fgets(stdin) reader can't work.
   Route the REPL through potato_readline (see potatos.c). LUA_MAXINPUT is the
   frontend's line-buffer size; fix it here so potatos.c and lua.c agree. */
#define LUA_MAXINPUT          512
#define lua_initreadline(L)   ((void)L)
#define lua_readline(L,b,p)   potato_readline(L,b,p)
#define lua_saveline(L,line)  ((void)L,(void)line)
#define lua_freeline(L,b)     ((void)L,(void)b)

#ifndef __ASSEMBLER__
struct lua_State;
/* REPL line reader (see potatos.c). Returns 1 if a line was read into buf
   (without trailing newline), 0 on EOF. */
int potato_readline(struct lua_State *L, char *buf, const char *prompt);
/* Registers the potatos.* module. Defined in potatos.c. */
int luaopen_potatos(struct lua_State *L);
#endif

#endif /* POTATO_LUA_H */
