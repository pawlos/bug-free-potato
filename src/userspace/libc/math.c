/*
 * math.c — libm for potatOS userspace
 *
 * All hardware computation is done with the x87 FPU via inline asm
 * (AT&T syntax).  Userspace is compiled without -masm=intel.
 *
 * x87 register constraints used:
 *   "t"  = ST(0)  (top of FPU stack)
 *   "u"  = ST(1)  (second from top)
 */

#include "math.h"

/* ── trig ─────────────────────────────────────────────────────────────────── */

double sin(double x) {
    double r;
    __asm__("fsin" : "=t"(r) : "0"(x));
    return r;
}

double cos(double x) {
    double r;
    __asm__("fcos" : "=t"(r) : "0"(x));
    return r;
}

double tan(double x) {
    double r;
    /* fptan computes tan(ST0) then pushes 1.0, leaving ST0=1.0 ST1=tan(x).
       fstp %st(0) pops ST0, making the old ST1=tan(x) the new ST0.        */
    __asm__("fptan\n\t"
            "fstp %%st(0)"
            : "=t"(r)
            : "0"(x));
    return r;
}

/* fpatan computes atan(ST1/ST0), pops both, pushes result.
   We need ST0=x, ST1=y so that atan(y/x) = atan2(y,x).                    */
double atan2(double y, double x) {
    double r;
    __asm__("fpatan"
            : "=t"(r)
            : "0"(x), "u"(y)
            : "st(1)");
    return r;
}

double atan(double x)  { return atan2(x, 1.0); }
double asin(double x)  { return atan2(x,       sqrt(1.0 - x*x)); }
double acos(double x)  { return atan2(sqrt(1.0 - x*x), x);       }

/* ── sqrt / fabs ─────────────────────────────────────────────────────────── */

double sqrt(double x) {
    double r;
    __asm__("fsqrt" : "=t"(r) : "0"(x));
    return r;
}

double fabs(double x) {
    double r;
    __asm__("fabs" : "=t"(r) : "0"(x));
    return r;
}

double hypot(double x, double y) { return sqrt(x*x + y*y); }

/* ── logarithms ──────────────────────────────────────────────────────────── */

/* fyl2x: ST0 = ST1 * log2(ST0), pops both.
   We load the constant into ST1 via fldXX, then swap so x ends up in ST0. */

double log2(double x) {
    double r;
    /* fld1 pushes 1.0 → ST0=1, ST1=x; fxch → ST0=x, ST1=1 */
    __asm__("fld1\n\t"
            "fxch\n\t"
            "fyl2x"
            : "=t"(r)
            : "0"(x));
    return r;
}

double log(double x) {
    double r;
    /* fldln2 pushes ln(2) → ST0=ln2, ST1=x; fxch → ST0=x, ST1=ln2 */
    __asm__("fldln2\n\t"
            "fxch\n\t"
            "fyl2x"
            : "=t"(r)
            : "0"(x));
    return r;
}

double log10(double x) {
    double r;
    /* fldlg2 pushes log10(2) */
    __asm__("fldlg2\n\t"
            "fxch\n\t"
            "fyl2x"
            : "=t"(r)
            : "0"(x));
    return r;
}

/* ── exp and pow ─────────────────────────────────────────────────────────── */

/* Compute 2^z using the x87 f2xm1/fscale pair.
   z is in ST0 on entry; result is in ST0 on exit.                          */
static double _pow2(double z) {
    double r;
    __asm__(
        "fld %%st(0)\n\t"        /* ST0=z,       ST1=z            */
        "frndint\n\t"            /* ST0=round(z),ST1=z            */
        "fxch %%st(1)\n\t"       /* ST0=z,       ST1=round(z)     */
        "fsub %%st(1)\n\t"       /* ST0=frac(z), ST1=round(z)     */
        "f2xm1\n\t"              /* ST0=2^frac-1                  */
        "fld1\n\t"               /* ST0=1, ST1=2^frac-1, ST2=n   */
        "faddp\n\t"              /* ST0=2^frac,  ST1=n            */
        "fscale\n\t"             /* ST0=2^z,     ST1=n (unchanged)*/
        "fstp %%st(1)"           /* discard n; ST0=2^z            */
        : "=t"(r)
        : "0"(z));
    return r;
}

double exp(double x) {
    double z;
    /* z = x * log2(e) */
    __asm__("fldl2e\n\t"   /* ST0=log2(e), ST1=x          */
            "fmulp"        /* ST0 = x * log2(e), pop      */
            : "=t"(z)
            : "0"(x));
    return _pow2(z);
}

double pow(double x, double y) {
    if (y == 0.0) return 1.0;
    if (x == 0.0) return 0.0;
    /* Integer exponent fast-path: repeated multiplication avoids log2
       rounding that makes e.g. pow(3,3) return 26.999… instead of 27. */
    if (y == (double)(long long)y && y >= 1.0 && y <= 63.0) {
        long long n = (long long)y;
        int neg = 0;
        if (x < 0.0) { neg = n & 1; x = -x; }
        double r = 1.0;
        double base = x;
        while (n) {
            if (n & 1) r *= base;
            base *= base;
            n >>= 1;
        }
        return neg ? -r : r;
    }
    if (x < 0.0) {
        long long n = (long long)y;
        if ((double)n != y) return 0.0;    /* would be NaN */
        double r = exp(-y * log(-x));
        return (n & 1) ? -r : r;
    }
    /* x^y = 2^(y * log2(x)) */
    double z;
    __asm__("fyl2x"
            : "=t"(z)
            : "0"(x), "u"(y)
            : "st(1)");
    return _pow2(z);
}

/* ── rounding ────────────────────────────────────────────────────────────── */

/* Implemented via integer cast — correct for values in [-2^63, 2^63).      */
double trunc(double x) { return (double)(long long)x; }

double floor(double x) {
    double t = (double)(long long)x;
    return (t > x) ? t - 1.0 : t;
}

double ceil(double x) {
    double t = (double)(long long)x;
    return (t < x) ? t + 1.0 : t;
}

double round(double x) {
    return (x >= 0.0) ? floor(x + 0.5) : ceil(x - 0.5);
}

/* ── fmod ────────────────────────────────────────────────────────────────── */

/* fprem computes the partial remainder; C2 set means "not finished yet",
   so loop until C2 clears.  C2 is bit 10 of the FPU status word = bit 2
   of the high byte when read via fnstsw %%ax.                              */
double fmod(double x, double y) {
    double r = x;
    __asm__("1:\n\t"
            "fprem\n\t"
            "fnstsw %%ax\n\t"
            "testb $4, %%ah\n\t"
            "jnz 1b"
            : "+t"(r)
            : "u"(y)
            : "ax", "cc");
    return r;
}

/* ── ldexp / frexp ───────────────────────────────────────────────────────── */

/* fscale: ST0 *= 2^trunc(ST1) */
double ldexp(double x, int n) {
    double fn = (double)n;
    double r;
    __asm__("fscale" : "=t"(r) : "0"(x), "u"(fn));
    return r;
}

/* Extract mantissa and binary exponent from a IEEE 754 double.             */
double frexp(double x, int *exp) {
    if (x == 0.0) { *exp = 0; return 0.0; }
    typedef unsigned long long u64;
    union { double d; u64 i; } u;
    u.d = x;
    int e = (int)((u.i >> 52) & 0x7FFu) - 1022;
    *exp  = e;
    /* Set biased exponent to 1022 so mantissa is in [0.5, 1.0) */
    u.i = (u.i & ~((u64)0x7FF << 52)) | ((u64)1022 << 52);
    return u.d;
}

/* ── float variants ──────────────────────────────────────────────────────── */

float sinf  (float x)           { return (float)sin  ((double)x); }
float cosf  (float x)           { return (float)cos  ((double)x); }
float tanf  (float x)           { return (float)tan  ((double)x); }
float asinf (float x)           { return (float)asin ((double)x); }
float acosf (float x)           { return (float)acos ((double)x); }
float atanf (float x)           { return (float)atan ((double)x); }
float atan2f(float y, float x)  { return (float)atan2((double)y, (double)x); }
float sqrtf (float x)           { return (float)sqrt ((double)x); }
float fabsf (float x)           { return (float)fabs ((double)x); }
float hypotf(float x, float y)  { return (float)hypot((double)x, (double)y); }
float floorf(float x)           { return (float)floor((double)x); }
float ceilf (float x)           { return (float)ceil ((double)x); }
float roundf(float x)           { return (float)round((double)x); }
float truncf(float x)           { return (float)trunc((double)x); }
float fmodf (float x, float y)  { return (float)fmod ((double)x, (double)y); }
float logf  (float x)           { return (float)log  ((double)x); }
float log2f (float x)           { return (float)log2 ((double)x); }
float log10f(float x)           { return (float)log10((double)x); }
float expf  (float x)           { return (float)exp  ((double)x); }
float powf  (float x, float y)  { return (float)pow  ((double)x, (double)y); }
float ldexpf(float x, int n)    { return (float)ldexp((double)x, n); }
