/* potatos.c — potatOS glue for Lua: REPL input + the potatos.* module.
   Task 1 ships a stub readline (EOF); Task 2 implements the real one and
   Task 3+ fills in luaopen_potatos. */
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include <syscall.h>   /* potatOS libc syscalls */

/* Stub: no interactive input yet — behave as immediate EOF so the REPL
   exits cleanly and script mode is unaffected. Replaced in Task 2. */
int potato_readline(lua_State *L, char *buf, const char *prompt) {
    (void)L; (void)buf; (void)prompt;
    return 0;
}

/* Placeholder module opener — real bindings added in Task 3. */
int luaopen_potatos(lua_State *L) {
    lua_newtable(L);
    return 1;
}
