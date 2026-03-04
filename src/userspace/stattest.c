#include "libc/stdio.h"
#include "libc/stat.h"

int main(void)
{
    struct stat_buf sb;

    printf("stat HELLO.ELF: ");
    if (stat("HELLO.ELF", &sb) == 0) {
        printf("size=%u\n", sb.file_size);
        printf("  created: %04d-%02d-%02d %02d:%02d:%02d\n",
               fat_date_year(sb.create_date), fat_date_month(sb.create_date),
               fat_date_day(sb.create_date),
               fat_time_hour(sb.create_time), fat_time_min(sb.create_time),
               fat_time_sec(sb.create_time));
        printf("  modified: %04d-%02d-%02d %02d:%02d:%02d\n",
               fat_date_year(sb.modify_date), fat_date_month(sb.modify_date),
               fat_date_day(sb.modify_date),
               fat_time_hour(sb.modify_time), fat_time_min(sb.modify_time),
               fat_time_sec(sb.modify_time));
    } else {
        printf("FAILED\n");
    }

    printf("stat NONEXIST: ");
    if (stat("NONEXIST", &sb) == 0) {
        printf("unexpected success\n");
    } else {
        printf("correctly returned -1\n");
    }

    return 0;
}
