/*
 * dvlx_stubs.cpp — Stub implementations for disabled DevilutionX features.
 * Provides empty definitions for Lua, Sound, SVid, Utf8 functions
 * that are called from core game code but whose source files are excluded.
 */
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <expected.hpp>
#include <function_ref.hpp>

/* Pull in our sol stubs (need complete types) */
#include <sol/sol.hpp>

namespace devilution {

struct Player;
struct Monster;

/* ── Lua stubs ──────────────────────────────────────────────────────────── */

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

/* ── SVid stubs (SmackerDecoder not available) ─────────────────────────── */

void SVidMute() {}
void SVidUnmute() {}
void SVidPlayBegin(const char *, int) {}
bool SVidPlayContinue() { return false; }
void SVidPlayEnd() {}

/* ── Sound stubs (NOSOUND) ─────────────────────────────────────────────── */

struct SoundSample;

bool SndFileSpecialEffect(int) { return false; }

/* ── Utf8 stubs ────────────────────────────────────────────────────────── */

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
