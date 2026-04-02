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

/* ── debug tracing ── */
extern "C" int serial_printf(const char *fmt, ...);

/* Wrap libmpq functions to trace MPQ operations */
struct mpq_archive_s;
extern "C" int32_t __real_libmpq__archive_open(mpq_archive_s **archive, const char *filename, int64_t offset);
extern "C" int32_t __wrap_libmpq__archive_open(mpq_archive_s **archive, const char *filename, int64_t offset)
{
    int32_t err = __real_libmpq__archive_open(archive, filename, offset);
    serial_printf("[DVX] mpq_open('%s') = %d\n", filename, err);
    return err;
}

extern "C" int32_t __real_libmpq__file_number_from_hash(mpq_archive_s*, uint32_t, uint32_t, uint32_t, uint32_t*);
extern "C" int32_t __wrap_libmpq__file_number_from_hash(mpq_archive_s *a, uint32_t h1, uint32_t h2, uint32_t h3, uint32_t *num)
{
    int32_t err = __real_libmpq__file_number_from_hash(a, h1, h2, h3, num);
    static int cnt = 0;
    if (cnt < 30) {
        serial_printf("[DVX] hash_lookup(0x%x,0x%x,0x%x) = %d fnum=%u\n", h1, h2, h3, err, num ? *num : 0);
        cnt++;
    }
    return err;
}

/* Wrap libmpq__file_size_unpacked and block_open to trace extraction */
extern "C" int32_t __real_libmpq__file_size_unpacked(mpq_archive_s*, uint32_t, int64_t*);
extern "C" int32_t __wrap_libmpq__file_size_unpacked(mpq_archive_s *a, uint32_t fnum, int64_t *size)
{
    int32_t err = __real_libmpq__file_size_unpacked(a, fnum, size);
    serial_printf("[DVX] file_size_unpacked(fnum=%u) = %d size=%ld\n", fnum, err, size ? (long)*size : -1);
    return err;
}

extern "C" int32_t __real_libmpq__block_open_offset_with_filename_s(mpq_archive_s*, uint32_t, const char*, size_t);
extern "C" int32_t __wrap_libmpq__block_open_offset_with_filename_s(mpq_archive_s *a, uint32_t fnum, const char *fn, size_t fnlen)
{
    int32_t err = __real_libmpq__block_open_offset_with_filename_s(a, fnum, fn, fnlen);
    serial_printf("[DVX] block_open(fnum=%u) = %d\n", fnum, err);
    return err;
}

extern "C" int32_t __real_libmpq__block_read_with_temporary_buffer(mpq_archive_s*, uint32_t, uint32_t, uint8_t*, int64_t, uint8_t*, int64_t, int64_t*);
extern "C" int32_t __wrap_libmpq__block_read_with_temporary_buffer(mpq_archive_s *a, uint32_t fnum, uint32_t blocknum, uint8_t *out, int64_t outsize, uint8_t *tmp, int64_t tmpsize, int64_t *transferred)
{
    int32_t err = __real_libmpq__block_read_with_temporary_buffer(a, fnum, blocknum, out, outsize, tmp, tmpsize, transferred);
    serial_printf("[DVX] block_read(fnum=%u blk=%u, outsize=%ld) = %d xfer=%ld\n", fnum, blocknum, (long)outsize, err, transferred ? (long)*transferred : -1);
    return err;
}

/* Also wrap file_hash_s to see what filenames produce which hashes */
extern "C" void __real_libmpq__file_hash_s(const char*, size_t, uint32_t*, uint32_t*, uint32_t*);
extern "C" void __wrap_libmpq__file_hash_s(const char *fn, size_t len, uint32_t *h1, uint32_t *h2, uint32_t *h3)
{
    __real_libmpq__file_hash_s(fn, len, h1, h2, h3);
    static int cnt = 0;
    if (cnt < 15) {
        /* Print filename (may contain backslashes) */
        char buf[64];
        size_t n = len < 60 ? len : 60;
        for (size_t i = 0; i < n; i++) buf[i] = fn[i];
        buf[n] = 0;
        serial_printf("[DVX] hash('%s') = 0x%x,0x%x,0x%x\n", buf, *h1, *h2, *h3);
        cnt++;
    }
}

/* ── main wrapper: provide clean argc/argv (kernel doesn't set up the stack) ── */
namespace devilution { int DiabloMain(int argc, char **argv); }

extern "C" int main(int /*argc*/, char ** /*argv*/)
{
    static char *fake_argv[] = { (char*)"diablo", nullptr };
    return devilution::DiabloMain(1, fake_argv);
}

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
bool SVidPlayBegin(const char *, int) { return false; }
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
