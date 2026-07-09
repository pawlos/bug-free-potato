/* potato_libc.c — libc functions Lua 5.4 references that the potatOS libc
   does not provide. Compiled only into lua.elf (kept out of the shared libc so
   the C++ ports are untouched). Declarations come from the libc/host headers
   Lua already compiled against; definitions here satisfy the linker.

   Fidelity note: potatOS time is "seconds since boot" and libc localtime() is
   a crude decomposition (see stdlib.c). gmtime/mktime/strftime mirror that
   fidelity — enough for os.time()/os.date() to work without pretending to know
   the wall-clock calendar. The truly host-dependent calls (locale, temp files)
   are graceful stubs, matching the port's "unsupported → fail safe" contract. */

#include "time.h"     /* struct tm, time_t, size_t, prototypes */
#include "stdio.h"    /* FILE */
#include "stdlib.h"   /* exit */
#include "string.h"   /* memset, strlen, memcpy */
#include <locale.h>   /* struct lconv (host header; libc has none) */

/* ── stdlib ──────────────────────────────────────────────────────────────── */
void abort(void)
{
    /* No signals on potatOS; terminate the task (128 + SIGABRT). */
    exit(134);
    for (;;) { }   /* unreachable; keeps the compiler happy about noreturn */
}

/* ── locale (minimal "C" locale) ─────────────────────────────────────────── */
char *setlocale(int category, const char *locale)
{
    (void)category; (void)locale;
    return (char *)"C";
}

struct lconv *localeconv(void)
{
    static struct lconv lc;
    static char dot[] = ".", empty[] = "";
    lc.decimal_point = dot;
    lc.thousands_sep = empty;
    lc.grouping      = empty;
    return &lc;
}

/* ── time: crude, boot-relative (consistent with libc localtime) ─────────── */
struct tm *gmtime(const time_t *timep)
{
    static struct tm result;
    memset(&result, 0, sizeof(result));
    if (timep) {
        time_t t = *timep;
        result.tm_sec  = (int)(t % 60);
        result.tm_min  = (int)((t / 60) % 60);
        result.tm_hour = (int)((t / 3600) % 24);
        result.tm_mday = 1;
    }
    return &result;
}

time_t mktime(struct tm *tm)
{
    if (!tm) return (time_t)-1;
    return (time_t)tm->tm_sec
         + (time_t)tm->tm_min  * 60
         + (time_t)tm->tm_hour * 3600
         + (time_t)tm->tm_yday * 86400;
}

/* strftime: common specifiers used by Lua's os.date. Unknown specifiers are
   emitted verbatim. Returns chars written (excluding NUL), or 0 on overflow. */
static size_t put2(char *s, size_t max, size_t i, int v)
{
    if (i + 2 > max) return (size_t)-1;
    s[i]   = (char)('0' + (v / 10) % 10);
    s[i+1] = (char)('0' + v % 10);
    return i + 2;
}

size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tm)
{
    size_t i = 0;
    if (max == 0) return 0;
    for (const char *p = fmt; *p; p++) {
        if (*p != '%') {
            if (i + 1 >= max) return 0;
            s[i++] = *p;
            continue;
        }
        p++;
        size_t n = i;
        switch (*p) {
            case 'Y': {
                int y = tm->tm_year + 1900;
                if (i + 4 >= max) return 0;
                s[i++] = (char)('0' + (y / 1000) % 10);
                s[i++] = (char)('0' + (y / 100) % 10);
                s[i++] = (char)('0' + (y / 10) % 10);
                s[i++] = (char)('0' + y % 10);
                n = i;
                break;
            }
            case 'y': n = put2(s, max, i, (tm->tm_year + 1900) % 100); break;
            case 'm': n = put2(s, max, i, tm->tm_mon + 1); break;
            case 'd': n = put2(s, max, i, tm->tm_mday);    break;
            case 'H': n = put2(s, max, i, tm->tm_hour);    break;
            case 'M': n = put2(s, max, i, tm->tm_min);     break;
            case 'S': n = put2(s, max, i, tm->tm_sec);     break;
            case '%': if (i + 1 >= max) return 0; s[i++] = '%'; n = i; break;
            default:  /* unknown: emit "%" + char verbatim */
                if (i + 2 >= max) return 0;
                s[i++] = '%';
                if (*p) s[i++] = *p;
                n = i;
                break;
        }
        if (n == (size_t)-1) return 0;
        i = n;
        if (!*p) break;
    }
    if (i >= max) return 0;
    s[i] = '\0';
    return i;
}

/* ── stdio: unsupported on potatOS → graceful failure ────────────────────── */
FILE *freopen(const char *path, const char *mode, FILE *stream)
{
    (void)path; (void)mode; (void)stream;
    return (FILE *)0;
}

FILE *tmpfile(void)
{
    return (FILE *)0;
}

char *tmpnam(char *s)
{
    (void)s;
    return (char *)0;
}
