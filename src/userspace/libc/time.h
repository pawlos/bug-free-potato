#pragma once

typedef long time_t;
typedef long clock_t;

#define CLOCKS_PER_SEC  1000000L   /* clock() now returns microseconds */

time_t  time (time_t *t);
clock_t clock(void);
