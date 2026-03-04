#pragma once
#include "syscall.h"

/* Matches kernel StatResult layout exactly. */
struct stat_buf {
    unsigned int   file_size;
    unsigned short create_time;
    unsigned short create_date;
    unsigned short modify_time;
    unsigned short modify_date;
};

static inline int stat(const char *filename, struct stat_buf *buf)
{
    return (int)sys_stat(filename, buf);
}

/* Decode FAT date: bits[15:9]=year-1980, bits[8:5]=month, bits[4:0]=day */
static inline int fat_date_year (unsigned short d) { return (d >> 9) + 1980; }
static inline int fat_date_month(unsigned short d) { return (d >> 5) & 0xF; }
static inline int fat_date_day  (unsigned short d) { return d & 0x1F; }

/* Decode FAT time: bits[15:11]=hours, bits[10:5]=minutes, bits[4:0]=seconds/2 */
static inline int fat_time_hour(unsigned short t) { return t >> 11; }
static inline int fat_time_min (unsigned short t) { return (t >> 5) & 0x3F; }
static inline int fat_time_sec (unsigned short t) { return (t & 0x1F) * 2; }
