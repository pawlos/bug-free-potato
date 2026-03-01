#pragma once

typedef long time_t;
typedef long clock_t;
typedef long suseconds_t;

#define CLOCKS_PER_SEC  1000000L   /* clock() now returns microseconds */

#ifndef __timeval_defined
#define __timeval_defined
struct timeval {
    time_t      tv_sec;   /* seconds */
    suseconds_t tv_usec;  /* microseconds */
};
#endif

#ifndef __timezone_defined
#define __timezone_defined
struct timezone {
    int tz_minuteswest;
    int tz_dsttime;
};
#endif

time_t  time (time_t *t);
clock_t clock(void);
int     gettimeofday(struct timeval *tv, struct timezone *tz);
