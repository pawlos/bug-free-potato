#pragma once

typedef long time_t;
typedef long clock_t;

#define CLOCKS_PER_SEC  50L   /* matches our 50 Hz kernel timer */

time_t  time (time_t *t);
clock_t clock(void);
