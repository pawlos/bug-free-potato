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

        /* Catch truly unknown specifiers before we touch va_arg.
         * d/i/u/x/X/o/p/f/F/e/E/g/G are handled below. */
        if (spec != 'd' && spec != 'i' && spec != 'u' &&
            spec != 'x' && spec != 'X' && spec != 'o' &&
            spec != 'p' &&
            spec != 'f' && spec != 'F' &&
            spec != 'e' && spec != 'E' &&
            spec != 'g' && spec != 'G') {
            EMIT('%');
            EMIT(spec);
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
        else if (spec == 'f' || spec == 'F' ||
                 spec == 'e' || spec == 'E' ||
                 spec == 'g' || spec == 'G') {
            /* floating-point: floats are promoted to double in varargs */
            double dval = va_arg(ap, double);
            if (prec < 0) prec = 6;
            int neg = 0;
            if (dval < 0.0) { neg = 1; dval = -dval; }
            /* round to requested precision */
            double rounder = 0.5;
            for (int i = 0; i < prec; i++) rounder *= 0.1;
            dval += rounder;
            /* split into integer and fractional parts */
            unsigned long long ipart = (unsigned long long)dval;
            double fpart = dval - (double)ipart;
            char istr[32]; int ilen = 0;
            if (ipart == 0) { istr[ilen++] = '0'; }
            else {
                unsigned long long t2 = ipart;
                while (t2) { istr[ilen++] = (char)('0' + t2 % 10); t2 /= 10; }
                for (int i = 0, j = ilen-1; i < j; i++, j--)
                    { char t = istr[i]; istr[i] = istr[j]; istr[j] = t; }
            }
            istr[ilen] = '\0';
            /* fractional digits */
            char fstr[32]; int flen = prec;
            for (int i = 0; i < prec; i++) {
                fpart *= 10.0;
                int d = (int)fpart; if (d > 9) d = 9;
                fstr[i] = (char)('0' + d);
                fpart -= (double)d;
            }
            fstr[flen] = '\0';
            /* %g/%G: strip trailing zeros from fraction */
            if (spec == 'g' || spec == 'G') {
                while (flen > 0 && fstr[flen-1] == '0') flen--;
            }
            int pfxlen = (neg || flag_plus || flag_space) ? 1 : 0;
            int total = pfxlen + ilen + (flen > 0 ? 1 + flen : 0);
            char padc = (flag_zero && !flag_left) ? '0' : ' ';
            if (!flag_left) for (int i = total; i < width; i++) EMIT(padc);
            if (neg) EMIT('-');
            else if (flag_plus) EMIT('+');
            else if (flag_space) EMIT(' ');
            EMITSTR(istr, (size_t)ilen);
            if (flen > 0) { EMIT('.'); EMITSTR(fstr, (size_t)flen); }
            if (flag_left) for (int i = total; i < width; i++) EMIT(' ');
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

int serial_printf(const char *fmt, ...)
{
    char out[1024];
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(out, sizeof(out), fmt, ap);
    va_end(ap);
    if (n > (int)(sizeof(out) - 1)) n = (int)(sizeof(out) - 1);
    sys_write_serial(out, (size_t)n);
    return n;
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

/* ── vsscanf / sscanf ────────────────────────────────────────────────────── *
 * Supports: %d %i %u %o %x %X %s %c %f %e %g %% %n %[...] %[^...]
 * Flags:    * (suppress), width, l (long / double)
 * ─────────────────────────────────────────────────────────────────────── */

static int is_ws(char c) { return c==' '||c=='\t'||c=='\n'||c=='\r'||c=='\v'||c=='\f'; }

int vsscanf(const char *str, const char *fmt, va_list ap)
{
    const char *s = str;
    int count = 0;

    while (*fmt) {
        if (*fmt != '%') {
            if (is_ws(*fmt)) {
                while (is_ws(*s)) s++;
                fmt++;
            } else {
                if (*s != *fmt) return count;
                s++; fmt++;
            }
            continue;
        }
        fmt++; /* skip '%' */

        int suppress = 0;
        if (*fmt == '*') { suppress = 1; fmt++; }

        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') width = width*10 + (*fmt++ - '0');

        int is_long = 0;
        if      (*fmt == 'l') { is_long = 1; fmt++; }
        else if (*fmt == 'h') { fmt++; if (*fmt == 'h') fmt++; }

        char spec = *fmt++;

        if (spec == '%') { if (*s != '%') return count; s++; continue; }

        if (spec == 'c') {
            if (!*s) return count;
            if (!suppress) { *va_arg(ap, char*) = *s; count++; }
            s++;
            continue;
        }
        if (spec == 'n') {
            if (!suppress) *va_arg(ap, int*) = (int)(s - str);
            continue;
        }

        /* all others: skip leading whitespace */
        while (is_ws(*s)) s++;
        if (!*s) return count;

        if (spec == 's') {
            char *dst = suppress ? (char*)0 : va_arg(ap, char*);
            int n = 0;
            while (*s && !is_ws(*s)) {
                if (width && n >= width) break;
                if (dst) dst[n] = *s;
                n++; s++;
            }
            if (dst) dst[n] = '\0';
            if (!suppress) count++;
            continue;
        }

        if (spec == '[') {
            int negate = 0;
            if (*fmt == '^') { negate = 1; fmt++; }
            char cls[256] = {0};
            if (*fmt == ']') { cls[(unsigned char)']'] = 1; fmt++; }
            while (*fmt && *fmt != ']') { cls[(unsigned char)*fmt++] = 1; }
            if (*fmt == ']') fmt++;
            char *dst = suppress ? (char*)0 : va_arg(ap, char*);
            int n = 0;
            while (*s) {
                int in = cls[(unsigned char)*s];
                if (negate ? in : !in) break;
                if (width && n >= width) break;
                if (dst) dst[n] = *s;
                n++; s++;
            }
            if (dst) dst[n] = '\0';
            if (!suppress) count++;
            continue;
        }

        if (spec == 'd' || spec == 'i') {
            long val = 0; int neg = 0;
            if (*s == '-') { neg = 1; s++; }
            else if (*s == '+') s++;
            int base = 10;
            if (spec == 'i') {
                if (*s == '0') {
                    s++;
                    if (*s == 'x' || *s == 'X') { base = 16; s++; }
                    else base = 8;
                }
            }
            int n = 0;
            while (*s) {
                int d;
                if (*s>='0'&&*s<='9') d = *s-'0';
                else if (base==16&&*s>='a'&&*s<='f') d = *s-'a'+10;
                else if (base==16&&*s>='A'&&*s<='F') d = *s-'A'+10;
                else break;
                if (d >= base) break;
                val = val*base + d; s++; n++;
                if (width && n >= width) break;
            }
            if (!n) return count;
            if (neg) val = -val;
            if (!suppress) {
                if (is_long) *va_arg(ap, long*)        = val;
                else         *va_arg(ap, int*)          = (int)val;
                count++;
            }
            continue;
        }

        if (spec == 'u' || spec == 'o' || spec == 'x' || spec == 'X') {
            int base = (spec=='o') ? 8 : (spec=='u') ? 10 : 16;
            unsigned long val = 0;
            if (base==16 && *s=='0' && (*(s+1)=='x'||*(s+1)=='X')) s+=2;
            int n = 0;
            while (*s) {
                int d;
                if (*s>='0'&&*s<='9') d = *s-'0';
                else if (base==16&&*s>='a'&&*s<='f') d = *s-'a'+10;
                else if (base==16&&*s>='A'&&*s<='F') d = *s-'A'+10;
                else break;
                if (d >= base) break;
                val = val*base + d; s++; n++;
                if (width && n >= width) break;
            }
            if (!n) return count;
            if (!suppress) {
                if (is_long) *va_arg(ap, unsigned long*) = val;
                else         *va_arg(ap, unsigned int*)  = (unsigned int)val;
                count++;
            }
            continue;
        }

        if (spec=='f' || spec=='e' || spec=='g' || spec=='E' || spec=='G') {
            double val = 0.0; int neg = 0;
            if (*s=='-') { neg=1; s++; } else if (*s=='+') s++;
            while (*s>='0' && *s<='9') { val = val*10.0 + (*s++ - '0'); }
            if (*s == '.') {
                s++; double frac = 0.1;
                while (*s>='0' && *s<='9') { val += (*s++ - '0')*frac; frac *= 0.1; }
            }
            /* exponent */
            if (*s=='e' || *s=='E') {
                s++; int eneg=0, exp=0;
                if (*s=='-') { eneg=1; s++; } else if (*s=='+') s++;
                while (*s>='0' && *s<='9') exp = exp*10 + (*s++ - '0');
                double scale = 1.0;
                while (exp--) scale *= 10.0;
                if (eneg) val /= scale; else val *= scale;
            }
            if (neg) val = -val;
            if (!suppress) {
                if (is_long) *va_arg(ap, double*) = val;
                else         *va_arg(ap, float*)  = (float)val;
                count++;
            }
            continue;
        }
    }
    return count;
}

int sscanf(const char *str, const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    int r = vsscanf(str, fmt, ap);
    va_end(ap); return r;
}

void perror(const char *s)
{
    if (s && *s) {
        printf("%s: error\n", s);
    } else {
        printf("error\n");
    }
}

int fileno(FILE *stream)
{
    (void)stream;
    return -1;
}
