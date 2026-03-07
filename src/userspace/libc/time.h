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

struct tm {
    int tm_sec;    /* 0-60 */
    int tm_min;    /* 0-59 */
    int tm_hour;   /* 0-23 */
    int tm_mday;   /* 1-31 */
    int tm_mon;    /* 0-11 */
    int tm_year;   /* years since 1900 */
    int tm_wday;   /* 0-6, Sunday=0 */
    int tm_yday;   /* 0-365 */
    int tm_isdst;  /* DST flag */
};

time_t     time (time_t *t);
clock_t    clock(void);
int        gettimeofday(struct timeval *tv, struct timezone *tz);
struct tm *localtime(const time_t *timep);
