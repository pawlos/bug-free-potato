#include "libc/stdio.h"
#include "libc/math.h"

int main(void)
{
    puts("=== libm test ===");

    /* Basic trig */
    printf("sin(0)       = %d.000000  (expect 0)\n", (int)sin(0.0));
    printf("cos(0)       = %d.000000  (expect 1)\n", (int)cos(0.0));

    /* sqrt */
    double s2 = sqrt(2.0);
    int    s2i = (int)(s2 * 1000000);          /* 1414213 */
    printf("sqrt(2)*1e6  = %d  (expect 1414213)\n", s2i);

    /* atan2: atan2(1,1) = pi/4 */
    double a = atan2(1.0, 1.0);
    int    ai = (int)(a * 1000000);            /* 785398 */
    printf("atan2(1,1)*1e6 = %d  (expect 785398)\n", ai);

    /* log / exp */
    double e = exp(1.0);
    int    ei = (int)(e * 1000000);            /* 2718282 */
    printf("exp(1)*1e6   = %d  (expect 2718282)\n", ei);

    double l = log(e);
    int    li = (int)(l * 1000000);            /* 1000000 */
    printf("log(e)*1e6   = %d  (expect 1000000)\n", li);

    /* pow */
    int p = (int)pow(2.0, 10.0);               /* 1024 */
    printf("pow(2,10)    = %d  (expect 1024)\n", p);

    /* floor / ceil */
    printf("floor(2.7)   = %d  (expect 2)\n", (int)floor(2.7));
    printf("ceil(2.3)    = %d  (expect 3)\n", (int)ceil(2.3));

    /* fabs */
    printf("fabs(-3.5)*10 = %d  (expect 35)\n", (int)(fabs(-3.5) * 10));

    /* fmod */
    double fm = fmod(5.3, 2.0);               /* ~1.3 */
    printf("fmod(5.3,2)*10 = %d  (expect 13)\n", (int)(fm * 10));

    /* float variants */
    int sf = (int)(sinf(0.0f) * 1000);        /* 0 */
    int cf = (int)(cosf(0.0f) * 1000);        /* 1000 */
    printf("sinf(0)*1000 = %d  (expect 0)\n", sf);
    printf("cosf(0)*1000 = %d  (expect 1000)\n", cf);

    puts("=== done ===");
    return 0;
}
