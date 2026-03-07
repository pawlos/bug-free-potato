/*
 * mathtest.c — comprehensive libm validation for potatOS
 *
 * Tests all math functions Q2 uses against known-good values.
 * Uses integer-scaled comparisons since printf has no %f support.
 * Tolerance: 1 ULP at the scaled precision (typically 1e-6).
 */

#include "libc/stdio.h"
#include "libc/math.h"
#include "libc/string.h"
#include "libc/syscall.h"

/* Extract raw IEEE 754 bits for inspection */
static unsigned int  float_bits(float f)  { unsigned int  u; memcpy(&u, &f, 4); return u; }
static unsigned long double_bits(double d) { unsigned long u; memcpy(&u, &d, 8); return u; }

static int pass_count = 0;
static int fail_count = 0;

/* Check that actual (scaled to integer) matches expected.
   tol = allowed deviation in integer units. */
static void check_int(const char* name, long actual, long expected, long tol) {
    long diff = actual - expected;
    if (diff < 0) diff = -diff;
    if (diff <= tol) {
        printf("  PASS  %-28s  got %ld  (expect %ld)\n", name, actual, expected);
        pass_count++;
    } else {
        printf("  FAIL  %-28s  got %ld  (expect %ld, diff %ld)\n",
               name, actual, expected, diff);
        sys_write_serial("!", 1);
        /* Also log to serial so we can see failures even without copy */
        {
            char buf[128];
            int i = 0;
            const char* p = "FAIL: ";
            while (*p) buf[i++] = *p++;
            p = name;
            while (*p && i < 60) buf[i++] = *p++;
            buf[i++] = '\n';
            buf[i] = '\0';
            { int len = 0; while (buf[len]) len++; sys_write_serial(buf, len); }
        }
        fail_count++;
    }
}

/* Check raw hex bits of a double */
static void check_hex64(const char* name, double val, unsigned long expected) {
    unsigned long bits = double_bits(val);
    if (bits == expected) {
        printf("  PASS  %-28s  0x%lx\n", name, bits);
        pass_count++;
    } else {
        printf("  FAIL  %-28s  0x%lx  (expect 0x%lx)\n", name, bits, expected);
        {
            char buf[128];
            int i = 0;
            const char* p = "FAIL: ";
            while (*p) buf[i++] = *p++;
            p = name;
            while (*p && i < 60) buf[i++] = *p++;
            buf[i++] = '\n';
            buf[i] = '\0';
            { int len = 0; while (buf[len]) len++; sys_write_serial(buf, len); }
        }
        fail_count++;
    }
}

/* Check raw hex bits of a float */
static void check_hex32(const char* name, float val, unsigned int expected) {
    unsigned int bits = float_bits(val);
    if (bits == expected) {
        printf("  PASS  %-28s  0x%x\n", name, bits);
        pass_count++;
    } else {
        printf("  FAIL  %-28s  0x%x  (expect 0x%x)\n", name, bits, expected);
        {
            char buf[128];
            int i = 0;
            const char* p = "FAIL: ";
            while (*p) buf[i++] = *p++;
            p = name;
            while (*p && i < 60) buf[i++] = *p++;
            buf[i++] = '\n';
            buf[i] = '\0';
            { int len = 0; while (buf[len]) len++; sys_write_serial(buf, len); }
        }
        fail_count++;
    }
}

int main(void)
{
    puts("=== libm validation ===\n");

    /* ── Exact values (bit-exact expected) ─────────────────────────────── */
    puts("[exact values]");
    check_hex64("sin(0.0)",      sin(0.0),      0x0000000000000000UL);
    check_hex64("cos(0.0)",      cos(0.0),      0x3FF0000000000000UL); /* 1.0 */
    check_hex64("sqrt(4.0)",     sqrt(4.0),     0x4000000000000000UL); /* 2.0 */
    check_hex64("sqrt(1.0)",     sqrt(1.0),     0x3FF0000000000000UL); /* 1.0 */
    check_hex64("fabs(-1.0)",    fabs(-1.0),    0x3FF0000000000000UL); /* 1.0 */
    check_hex64("fabs(3.5)",     fabs(3.5),     0x400C000000000000UL); /* 3.5 */
    check_hex64("floor(2.0)",    floor(2.0),    0x4000000000000000UL); /* 2.0 */
    check_hex64("ceil(2.0)",     ceil(2.0),     0x4000000000000000UL); /* 2.0 */
    check_hex64("floor(-2.0)",   floor(-2.0),   0xC000000000000000UL); /* -2.0 */
    check_hex64("atan2(0,1)",    atan2(0.0, 1.0), 0x0000000000000000UL); /* 0.0 */

    /* ── Scaled integer checks (tolerance ±1) ──────────────────────────── */
    puts("\n[trig — scaled *1e6]");
    check_int("sin(pi/6)*1e6",   (long)(sin(M_PI/6.0)  * 1000000), 500000,  1);
    check_int("sin(pi/4)*1e6",   (long)(sin(M_PI/4.0)  * 1000000), 707106,  2);
    check_int("sin(pi/2)*1e6",   (long)(sin(M_PI/2.0)  * 1000000), 1000000, 1);
    check_int("sin(pi)*1e6",     (long)(sin(M_PI)       * 1000000), 0,       2);
    check_int("cos(pi/3)*1e6",   (long)(cos(M_PI/3.0)  * 1000000), 500000,  1);
    check_int("cos(pi/2)*1e6",   (long)(cos(M_PI/2.0)  * 1000000), 0,       2);
    check_int("cos(pi)*1e6",     (long)(cos(M_PI)       * 1000000), -1000000, 1);
    check_int("tan(pi/4)*1e6",   (long)(tan(M_PI/4.0)  * 1000000), 1000000, 2);

    puts("\n[atan2 — scaled *1e6]");
    check_int("atan2(1,1)*1e6",  (long)(atan2(1.0,  1.0) * 1000000), 785398,   2);
    check_int("atan2(1,0)*1e6",  (long)(atan2(1.0,  0.0) * 1000000), 1570796,  2);
    check_int("atan2(0,-1)*1e6", (long)(atan2(0.0, -1.0) * 1000000), 3141592,  2);
    check_int("atan2(-1,0)*1e6", (long)(atan2(-1.0, 0.0) * 1000000), -1570796, 2);
    /* All four quadrants */
    check_int("atan2(1,-1)*1e6", (long)(atan2(1.0, -1.0) * 1000000), 2356194,  2);
    check_int("atan2(-1,-1)*1e6",(long)(atan2(-1.0,-1.0) * 1000000), -2356194, 2);

    puts("\n[sqrt — scaled *1e6]");
    check_int("sqrt(2)*1e6",     (long)(sqrt(2.0)  * 1000000), 1414213, 2);
    check_int("sqrt(3)*1e6",     (long)(sqrt(3.0)  * 1000000), 1732050, 2);
    check_int("sqrt(0.5)*1e6",   (long)(sqrt(0.5)  * 1000000), 707106,  2);

    puts("\n[floor/ceil]");
    check_int("floor(2.7)",      (long)floor(2.7),    2, 0);
    check_int("floor(-2.7)",     (long)floor(-2.7),  -3, 0);
    check_int("floor(0.0)",      (long)floor(0.0),    0, 0);
    check_int("floor(-0.1)",     (long)floor(-0.1),  -1, 0);
    check_int("ceil(2.3)",       (long)ceil(2.3),     3, 0);
    check_int("ceil(-2.3)",      (long)ceil(-2.3),   -2, 0);
    check_int("ceil(0.0)",       (long)ceil(0.0),     0, 0);
    check_int("ceil(-0.1)",      (long)ceil(-0.1),    0, 0);

    puts("\n[fmod — scaled *1e6]");
    check_int("fmod(5.3,2)*1e6", (long)(fmod(5.3, 2.0) * 1000000), 1300000, 2);
    check_int("fmod(-5.3,2)*1e6",(long)(fmod(-5.3,2.0) * 1000000),-1300000, 2);
    check_int("fmod(7,3.5)*1e6", (long)(fmod(7.0, 3.5) * 1000000), 0,       2);

    puts("\n[pow / exp / log — scaled *1e6]");
    check_int("pow(2,10)",       (long)pow(2.0, 10.0), 1024, 0);
    check_int("pow(3,3)",        (long)pow(3.0, 3.0),  27,   0);
    check_int("exp(1)*1e6",      (long)(exp(1.0)  * 1000000), 2718281, 2);
    check_int("log(e)*1e6",      (long)(log(M_E)  * 1000000), 1000000, 2);
    check_int("log10(100)*1e6",  (long)(log10(100.0) * 1000000), 2000000, 2);

    /* ── Float variants (Q2 uses these via sinf etc.) ──────────────────── */
    puts("\n[float variants — scaled *1e4]");
    check_int("sinf(pi/6)*1e4",  (long)(sinf((float)M_PI/6.0f) * 10000), 5000,  2);
    check_int("cosf(pi/3)*1e4",  (long)(cosf((float)M_PI/3.0f) * 10000), 5000,  2);
    check_int("sqrtf(2)*1e4",    (long)(sqrtf(2.0f) * 10000),            14142, 2);
    check_int("atan2f(1,1)*1e4", (long)(atan2f(1.0f, 1.0f) * 10000),    7853,  2);
    check_int("floorf(-2.7)",    (long)floorf(-2.7f),                    -3,     0);
    check_int("ceilf(2.3)",      (long)ceilf(2.3f),                      3,     0);

    /* ── Stress: values similar to Q2 vertex transforms ────────────────── */
    puts("\n[Q2-like vertex transforms]");
    /* Typical Q2 view angle rotation: sin/cos of ~45 degrees */
    double angle = 0.785398;  /* ~pi/4 */
    double sa = sin(angle), ca = cos(angle);
    check_int("sin(0.785398)*1e6", (long)(sa * 1000000), 707106, 3);
    check_int("cos(0.785398)*1e6", (long)(ca * 1000000), 707107, 3);

    /* Typical vertex transform: rotate + translate */
    double vx = 128.0, vy = -64.0, vz = 256.0;
    double rx = vx * ca - vy * sa;
    double ry = vx * sa + vy * ca;
    check_int("rot_x*1e3", (long)(rx * 1000), 135764, 5);
    check_int("rot_y*1e3", (long)(ry * 1000), 45254,  5);

    /* Perspective projection: similar to Q2's NEAR_CLIP */
    double near_clip = 0.01;
    double z_inv = 1.0 / (vz + near_clip);
    check_int("z_inv*1e9", (long)(z_inv * 1000000000), 3906097, 5);

    /* Screen coordinate from projection */
    double screen_x = rx * z_inv * 160.0 + 160.0;
    double screen_y = ry * z_inv * 120.0 + 120.0;
    check_int("screen_x*1e3", (long)(screen_x * 1000), 244852, 200);
    check_int("screen_y*1e3", (long)(screen_y * 1000), 141213, 200);

    /* ── Identity checks ───────────────────────────────────────────────── */
    puts("\n[identities]");
    /* sin²(x) + cos²(x) = 1 */
    double x = 1.234;
    double s = sin(x), c = cos(x);
    check_int("sin^2+cos^2 *1e6", (long)((s*s + c*c) * 1000000), 1000000, 2);

    /* atan2(sin(x), cos(x)) = x  for x in (-pi, pi) */
    check_int("atan2(sin,cos)*1e6", (long)(atan2(s, c) * 1000000),
              (long)(x * 1000000), 2);

    /* sqrt(x)^2 = x */
    double sq = sqrt(7.0);
    check_int("sqrt(7)^2 *1e6", (long)(sq * sq * 1000000), 7000000, 2);

    /* ── Summary ───────────────────────────────────────────────────────── */
    printf("\n=== Results: %d passed, %d failed ===\n", pass_count, fail_count);
    /* Log summary to serial */
    {
        char buf[64];
        int i = 0;
        const char* p = "MATHTEST: ";
        while (*p) buf[i++] = *p++;
        /* itoa pass_count */
        if (pass_count == 0) buf[i++] = '0';
        else { char tmp[10]; int t=0; int v=pass_count;
               while(v){tmp[t++]='0'+v%10;v/=10;}
               while(t--) buf[i++]=tmp[t]; }
        p = " pass, ";
        while (*p) buf[i++] = *p++;
        if (fail_count == 0) buf[i++] = '0';
        else { char tmp[10]; int t=0; int v=fail_count;
               while(v){tmp[t++]='0'+v%10;v/=10;}
               while(t--) buf[i++]=tmp[t]; }
        p = " fail\n";
        while (*p) buf[i++] = *p++;
        buf[i] = '\0';
        { int len = 0; while (buf[len]) len++; sys_write_serial(buf, len); }
    }
    return fail_count > 0 ? 1 : 0;
}
