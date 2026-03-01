#include "dirent.h"
#include "stdlib.h"
#include "string.h"
#include "syscall.h"

DIR *opendir(const char *path)
{
    (void)path;  /* filesystem is flat; all paths open the root directory */
    DIR *d = (DIR *)malloc(sizeof(DIR));
    if (!d) return (DIR *)0;
    d->index = 0;
    return d;
}

struct dirent *readdir(DIR *dirp)
{
    if (!dirp) return (struct dirent *)0;

    unsigned int size = 0;
    long r = sys_readdir(dirp->index, dirp->ent.d_name, &size);
    if (!r) return (struct dirent *)0;  /* end of directory */

    dirp->ent.d_ino    = (unsigned long)dirp->index;
    dirp->ent.d_off    = 0;
    dirp->ent.d_reclen = sizeof(struct dirent);
    dirp->ent.d_type   = DT_REG;
    dirp->index++;
    return &dirp->ent;
}

int closedir(DIR *dirp)
{
    if (!dirp) return -1;
    free(dirp);
    return 0;
}

void rewinddir(DIR *dirp)
{
    if (dirp) dirp->index = 0;
}
