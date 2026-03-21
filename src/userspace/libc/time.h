#pragma once

#ifndef _POTATO_SIZE_T
#define _POTATO_SIZE_T
typedef unsigned long size_t;
#endif

typedef long time_t;
typedef long clock_t;
typedef long suseconds_t;
typedef int  clockid_t;

#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 1

#define TIME_UTC 1

#ifndef _STRUCT_TIMESPEC
#define _STRUCT_TIMESPEC
struct timespec {
    time_t tv_sec;
    long   tv_nsec;
};
#endif

int timespec_get(struct timespec *ts, int base);
int nanosleep(const struct timespec *req, struct timespec *rem);

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
struct tm *gmtime(const time_t *timep);
time_t     mktime(struct tm *tm);
size_t     strftime(char *s, size_t max, const char *fmt, const struct tm *tm);

char      *asctime(const struct tm *tm);
char      *ctime(const time_t *timep);

static inline double difftime(time_t t1, time_t t0) { return (double)(t1 - t0); }
