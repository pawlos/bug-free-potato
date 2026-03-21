#ifndef SDL_log_h_
#define SDL_log_h_

#include "SDL_stdinc.h"

#ifndef SDL_LOG_CATEGORY_APPLICATION
#define SDL_LOG_CATEGORY_APPLICATION 0
#define SDL_LOG_CATEGORY_ERROR       1
#define SDL_LOG_CATEGORY_ASSERT      2
#define SDL_LOG_CATEGORY_SYSTEM      3
#define SDL_LOG_CATEGORY_AUDIO       4
#define SDL_LOG_CATEGORY_VIDEO       5
#define SDL_LOG_CATEGORY_RENDER      6
#define SDL_LOG_CATEGORY_INPUT       7
#define SDL_LOG_CATEGORY_CUSTOM      19
#define SDL_LOG_CATEGORY_TEST        20
#endif

#ifndef SDL_LOG_PRIORITY_VERBOSE
#define SDL_LOG_PRIORITY_VERBOSE  1
#define SDL_LOG_PRIORITY_DEBUG    2
#define SDL_LOG_PRIORITY_INFO     3
#define SDL_LOG_PRIORITY_WARN     4
#define SDL_LOG_PRIORITY_ERROR    5
#define SDL_LOG_PRIORITY_CRITICAL 6
#endif

typedef int SDL_LogPriority;

/* All logging is a no-op for now (could route to serial). */
static inline void SDL_Log(const char *fmt, ...) { (void)fmt; }
static inline void SDL_LogError(int cat, const char *fmt, ...) { (void)cat; (void)fmt; }
static inline void SDL_LogWarn(int cat, const char *fmt, ...) { (void)cat; (void)fmt; }
static inline void SDL_LogInfo(int cat, const char *fmt, ...) { (void)cat; (void)fmt; }
static inline void SDL_LogDebug(int cat, const char *fmt, ...) { (void)cat; (void)fmt; }
static inline void SDL_LogCritical(int cat, const char *fmt, ...) { (void)cat; (void)fmt; }
static inline void SDL_LogVerbose(int cat, const char *fmt, ...) { (void)cat; (void)fmt; }
static inline void SDL_LogSetAllPriority(int priority) { (void)priority; }
static inline void SDL_LogSetPriority(int cat, int priority) { (void)cat; (void)priority; }
static inline SDL_LogPriority SDL_LogGetPriority(int cat) { (void)cat; return SDL_LOG_PRIORITY_INFO; }
typedef void (*SDL_LogOutputFunction)(void *userdata, int category, SDL_LogPriority priority, const char *message);
static inline void SDL_LogGetOutputFunction(SDL_LogOutputFunction *cb, void **ud) { if(cb)*cb=0; if(ud)*ud=0; }
static inline void SDL_LogSetOutputFunction(SDL_LogOutputFunction cb, void *ud) { (void)cb; (void)ud; }
static inline void SDL_LogMessage(int cat, SDL_LogPriority pri, const char *fmt, ...)
    { (void)cat; (void)pri; (void)fmt; }

#endif
