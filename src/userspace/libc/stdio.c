#include "stdio.h"
#include "stdlib.h"   /* itoa, utoa */
#include "string.h"   /* strlen     */
#include "syscall.h"  /* sys_write  */
#include <stdarg.h>

/* ── vsnprintf core ──────────────────────────────────────────────────────── *
 * Writes at most `size` bytes (including NUL) into buf.
 * Returns the number of characters that *would* have been written
 * (excluding NUL), whether or not they fit.
 *
 * Supported: d/i, u, o, x/X, c, s, p, %, n
 * Flags:     - (left), 0 (zero-pad), + (force sign), ' ' (space sign)
 * Width:     integer
 * Precision: .integer  (strings: max chars; integers: min digits)
 * Length:    h, hh, l, ll, z
 * ─────────────────────────────────────────────────────────────────────── */

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
    /* write one character, counting even if buf full */
    size_t pos = 0;
#define EMIT(c) do { if (buf && pos + 1 < size) buf[pos] = (char)(c); pos++; } while(0)
#define EMITSTR(s, n) do { \
    for (size_t _k = 0; _k < (n); _k++) EMIT((s)[_k]); } while(0)

    char tmp[66];  /* scratch for integer conversion */

    while (*fmt) {
        if (*fmt != '%') { EMIT(*fmt++); continue; }
        fmt++;  /* skip '%' */

        /* ── flags ── */
        int flag_left  = 0;
        int flag_zero  = 0;
        int flag_plus  = 0;
        int flag_space = 0;
        int flag_hash  = 0;
        for (;;) {
            if      (*fmt == '-') { flag_left  = 1; fmt++; }
            else if (*fmt == '0') { flag_zero  = 1; fmt++; }
            else if (*fmt == '+') { flag_plus  = 1; fmt++; }
            else if (*fmt == ' ') { flag_space = 1; fmt++; }
            else if (*fmt == '#') { flag_hash  = 1; fmt++; }
            else break;
        }
        (void)flag_hash;

        /* ── width ── */
        int width = 0;
        if (*fmt == '*') { width = va_arg(ap, int); if (width < 0) { flag_left = 1; width = -width; } fmt++; }
        else while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');

        /* ── precision ── */
        int prec = -1;
        if (*fmt == '.') {
            fmt++;
            prec = 0;
            if (*fmt == '*') { prec = va_arg(ap, int); fmt++; }
            else while (*fmt >= '0' && *fmt <= '9') prec = prec * 10 + (*fmt++ - '0');
        }

        /* ── length modifier ── */
        enum { LEN_INT, LEN_LONG, LEN_LLONG, LEN_SHORT, LEN_CHAR, LEN_SIZE } len = LEN_INT;
        if (*fmt == 'l') {
            fmt++;
            if (*fmt == 'l') { len = LEN_LLONG; fmt++; }
            else len = LEN_LONG;
        } else if (*fmt == 'h') {
            fmt++;
            if (*fmt == 'h') { len = LEN_CHAR; fmt++; }
            else len = LEN_SHORT;
        } else if (*fmt == 'z') {
            len = LEN_SIZE; fmt++;
        }

        char spec = *fmt++;

        /* ── format ── */
        if (spec == '%') { EMIT('%'); continue; }

        if (spec == 'c') {
            char c = (char)va_arg(ap, int);
            if (!flag_left) for (int i = 1; i < width; i++) EMIT(' ');
            EMIT(c);
            if (flag_left)  for (int i = 1; i < width; i++) EMIT(' ');
            continue;
        }

        if (spec == 's') {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            size_t slen = strlen(s);
            if (prec >= 0 && (size_t)prec < slen) slen = (size_t)prec;
            if (!flag_left) for (int i = (int)slen; i < width; i++) EMIT(' ');
            EMITSTR(s, slen);
            if (flag_left)  for (int i = (int)slen; i < width; i++) EMIT(' ');
            continue;
        }

        if (spec == 'n') {
            *va_arg(ap, int *) = (int)pos;
            continue;
        }

        /* integer specifiers */
        int is_signed = (spec == 'd' || spec == 'i');
        int base = 10;
        int upper = 0;
        if (spec == 'x')      { base = 16; }
        else if (spec == 'X') { base = 16; upper = 1; }
        else if (spec == 'o') { base = 8; }
        else if (spec == 'p') {
            /* pointer: 0x + hex */
            unsigned long v = (unsigned long)va_arg(ap, void *);
            int plen = (int)(strlen(utoa(v, tmp, 16)) + 2);
            if (!flag_left) for (int i = plen; i < width; i++) EMIT(' ');
            EMIT('0'); EMIT('x');
            EMITSTR(tmp, strlen(tmp));
            if (flag_left) for (int i = plen; i < width; i++) EMIT(' ');
            continue;
        }

        /* fetch the value */
        unsigned long uval;
        long          sval;
        int negative = 0;
        if (is_signed) {
            if      (len == LEN_LLONG)  sval = (long)va_arg(ap, long long);
            else if (len == LEN_LONG)   sval = va_arg(ap, long);
            else if (len == LEN_SHORT)  sval = (short)va_arg(ap, int);
            else if (len == LEN_CHAR)   sval = (signed char)va_arg(ap, int);
            else if (len == LEN_SIZE)   sval = (long)va_arg(ap, size_t);
            else                        sval = va_arg(ap, int);
            if (sval < 0) { negative = 1; uval = (unsigned long)-sval; }
            else                        uval = (unsigned long)sval;
        } else {
            if      (len == LEN_LLONG)  uval = (unsigned long)va_arg(ap, unsigned long long);
            else if (len == LEN_LONG)   uval = va_arg(ap, unsigned long);
            else if (len == LEN_SHORT)  uval = (unsigned short)va_arg(ap, unsigned int);
            else if (len == LEN_CHAR)   uval = (unsigned char)va_arg(ap, unsigned int);
            else if (len == LEN_SIZE)   uval = va_arg(ap, size_t);
            else                        uval = va_arg(ap, unsigned int);
        }

        utoa(uval, tmp, base);
        if (upper) for (char *p = tmp; *p; p++) if (*p >= 'a' && *p <= 'f') *p -= 32;

        /* build prefix: sign or space */
        char pfxbuf[3] = {0};
        int  pfxlen = 0;
        if (is_signed) {
            if (negative)        pfxbuf[pfxlen++] = '-';
            else if (flag_plus)  pfxbuf[pfxlen++] = '+';
            else if (flag_space) pfxbuf[pfxlen++] = ' ';
        }

        int digits  = (int)strlen(tmp);
        int prec_pad = (prec > digits) ? prec - digits : 0;
        int total   = pfxlen + prec_pad + digits;
        char pad    = (flag_zero && !flag_left && prec < 0) ? '0' : ' ';

        if (!flag_left) for (int i = total; i < width; i++) EMIT(pad);
        EMITSTR(pfxbuf, (size_t)pfxlen);
        for (int i = 0; i < prec_pad; i++) EMIT('0');
        EMITSTR(tmp, (size_t)digits);
        if (flag_left) for (int i = total; i < width; i++) EMIT(' ');
    }

    if (buf) buf[pos < size ? pos : size - 1] = '\0';
    return (int)pos;
#undef EMIT
#undef EMITSTR
}

/* ── public API ──────────────────────────────────────────────────────────── */

int vsprintf(char *buf, const char *fmt, va_list ap)
{
    return vsnprintf(buf, (size_t)-1, fmt, ap);
}

int sprintf(char *buf, const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    int r = vsnprintf(buf, (size_t)-1, fmt, ap);
    va_end(ap); return r;
}

int snprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    int r = vsnprintf(buf, size, fmt, ap);
    va_end(ap); return r;
}

int vprintf(const char *fmt, va_list ap)
{
    char out[1024];
    int n = vsnprintf(out, sizeof(out), fmt, ap);
    if (n > (int)(sizeof(out) - 1)) n = (int)(sizeof(out) - 1);
    sys_write(1, out, (size_t)n);
    return n;
}

int printf(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    int r = vprintf(fmt, ap);
    va_end(ap); return r;
}

int putchar(int c)
{
    char ch = (char)c;
    sys_write(1, &ch, 1);
    return (unsigned char)c;
}

int puts(const char *s)
{
    size_t len = strlen(s);
    sys_write(1, s, len);
    sys_write(1, "\n", 1);
    return 0;
}
