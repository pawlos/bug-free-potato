/* potato_compat.h -- potatOS platform definitions for Chocolate Duke3D
 *
 * Replaces unix_compat.h / win32_compat.h / macos_compat.h.
 * Provides the types, macros, and stubs the BUILD engine expects.
 */

#ifndef DUKE3D_POTATO_COMPAT_H
#define DUKE3D_POTATO_COMPAT_H

/* Suppress libc getchar — we define our own below using sys_read_key() */
#define _POTATO_GETCHAR_DEFINED

#include "libc/stdlib.h"
#include "libc/string.h"
#include "libc/stdio.h"
#include "libc/ctype.h"
#include "libc/math.h"
#include "libc/file.h"
#include "libc/fcntl.h"
#include "libc/unistd.h"
#include "libc/sys/stat.h"

#include <stdint.h>
#include <stdarg.h>
#include <stddef.h>

/* Memory allocation wrappers (BUILD engine uses these) */
#define kmalloc(x) malloc(x)
#define kkmalloc(x) malloc(x)
#define kfree(x) free(x)
#define kkfree(x) free(x)

/* FP_OFF: Watcom allowed casting pointers to 32-bit ints.
 * On 64-bit we MUST use intptr_t to avoid truncation.
 * The engine stores these in int32_t variables (palookupoffs etc.),
 * but our userspace lives below 4GB so the cast is safe. */
#ifdef FP_OFF
#undef FP_OFF
#endif
#define FP_OFF(x) ((intptr_t)(x))

#ifndef max
#define max(x, y) (((x) > (y)) ? (x) : (y))
#endif
#ifndef min
#define min(x, y) (((x) < (y)) ? (x) : (y))
#endif

#define __int64 int64_t
#define O_BINARY 0

/* Byte order: potatOS is always x86_64 little-endian */
#ifndef LITTLE_ENDIAN
#define LITTLE_ENDIAN 1234
#endif
#ifndef BIG_ENDIAN
#define BIG_ENDIAN 4321
#endif
#ifndef BYTE_ORDER
#define BYTE_ORDER LITTLE_ENDIAN
#endif

/* String comparison (case-insensitive) */
int strcasecmp(const char *a, const char *b);
#define stricmp strcasecmp
#define strcmpi strcasecmp

#define S_IREAD 0400

/* No network on potatOS */
#define USER_DUMMY_NETWORK 1

/* PATH_SEP and CURDIR/ROOTDIR for file handling */
#define PATH_SEP_CHAR '/'
#define PATH_SEP_STR  "/"
#define ROOTDIR       "/"
#define CURDIR        "./"

/* Prevent unix_compat.h from being included (we replace it) */
#ifndef Duke3D_unix_compat_h
#define Duke3D_unix_compat_h
#endif

/* Pretend to be UNIX for Chocolate Duke3D game conditionals
 * (file finding, path handling in duke3d.h → dukeunix.h, etc.) */
#ifndef PLATFORM_UNIX
#define PLATFORM_UNIX 1
#endif

/* STUBBED macro for unimplemented functions */
#ifndef STUBBED
#define STUBBED(x)
#endif

/* We do NOT define PLATFORM_SUPPORTS_SDL -- we replace display.c entirely */

/* assert */
#ifndef assert
#define assert(x) ((void)0)
#endif

/* SDL stubs — Duke3D's global.c includes SDL.h for SDL_Quit in Error() */
#define SDL_Quit() ((void)0)

/* getchar/getch stubs (Duke3D uses these for "press any key" prompts).
   Suppress the libc version (which uses fgetc(stdin)). */
#define _POTATO_GETCHAR_DEFINED
static inline int getchar(void) { return sys_read_key(); }
#define getch getchar

/* unlink stub (no file deletion on potatOS FAT32 yet) */
static inline int unlink(const char *path) { (void)path; return -1; }

/* alloca: use GCC builtin */
#define alloca(size) __builtin_alloca(size)

/* _D_EXACT_NAMLEN: glibc dirent macro */
#define _D_EXACT_NAMLEN(d) strlen((d)->d_name)

#endif /* DUKE3D_POTATO_COMPAT_H */
