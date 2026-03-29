/*
 * dvlx_stubs.cpp -- Stub implementations for disabled DevilutionX features.
 * Most POSIX/glibc/wchar stubs are in dvlx_wchar.c (compiled as plain C).
 * This file contains C++-specific stubs (namespaced functions, templates).
 */
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <expected.hpp>
#include <function_ref.hpp>

/* Pull in our sol stubs (need complete types) */
#include <sol/sol.hpp>

extern "C" {

void *malloc(size_t size);
void  free(void *ptr);
void  _exit(int code);

void abort(void) { _exit(99); __builtin_unreachable(); }
void *aligned_alloc(size_t align, size_t size) { (void)align; return malloc(size); }

int __sprintf_chk(char *buf, int flag, size_t buflen, const char *fmt, ...) {
    (void)flag;
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    extern int vsnprintf(char *, size_t, const char *, __builtin_va_list);
    int ret = vsnprintf(buf, buflen, fmt, ap);
    __builtin_va_end(ap);
    return ret;
}

int open(const char *path, int flags, ...) {
    extern int sys_open(const char*);
    (void)flags;
    return sys_open(path);
}
int close(int fd) { extern int sys_close(int); return sys_close(fd); }
long read(int fd, void *buf, size_t n) { extern long sys_read(int,void*,size_t); return sys_read(fd,buf,n); }
long write(int fd, const void *buf, size_t n) { extern long sys_write(int,const void*,size_t); return sys_write(fd,buf,n); }
long writev(int fd, const void *iov, int iovcnt) { (void)fd; (void)iov; (void)iovcnt; return -1; }
long lseek64(int fd, long offset, int whence) { extern long sys_lseek(int,long,int); return sys_lseek(fd,offset,whence); }

} // extern "C"


namespace devilution {

struct Player;
struct Monster;

/* -- Lua stubs -- */

void LuaInitialize() {}
void LuaReloadActiveMods() {}
void LuaShutdown() {}

sol::state &GetLuaState() { static sol::state s; return s; }
sol::environment CreateLuaSandbox() { return {}; }
sol::object SafeCallResult(sol::protected_function_result, bool) { return {}; }
sol::table *GetLuaEvents() { return nullptr; }
void AddModsChangedHandler(tl::function_ref<void()>) {}

namespace lua {
void GameStart() {}
void GameDrawComplete() {}
void ItemDataLoaded() {}
void MonsterDataLoaded() {}
void UniqueItemDataLoaded() {}
void UniqueMonsterDataLoaded() {}
void OnMonsterTakeDamage(Monster const *, int, int) {}
void OnPlayerGainExperience(Player const *, unsigned int) {}
void OnPlayerTakeDamage(Player const *, int, int) {}
void StoreOpened(std::string_view) {}
} // namespace lua

/* -- SVid stubs -- */

void SVidMute() {}
void SVidUnmute() {}
void SVidPlayBegin(const char *, int) {}
bool SVidPlayContinue() { return false; }
void SVidPlayEnd() {}

/* -- Sound stubs (NOSOUND) -- */

struct SoundSample;
bool SndFileSpecialEffect(int) { return false; }

/* -- Utf8 stubs -- */

size_t CopyUtf8(char *dst, std::string_view src, size_t maxBytes) {
    size_t n = src.size() < maxBytes - 1 ? src.size() : maxBytes - 1;
    for (size_t i = 0; i < n; i++) dst[i] = src[i];
    dst[n] = '\0';
    return n;
}

size_t DecodeFirstUtf8CodePoint(std::string_view sv, size_t *bytes) {
    if (sv.empty()) { if (bytes) *bytes = 0; return 0; }
    if (bytes) *bytes = 1;
    return static_cast<unsigned char>(sv[0]);
}

std::string_view TruncateUtf8(std::string_view sv, size_t maxBytes) {
    if (sv.size() <= maxBytes) return sv;
    return sv.substr(0, maxBytes);
}

} // namespace devilution
