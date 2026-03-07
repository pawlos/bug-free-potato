#include "dirent.h"
#include "stdlib.h"
#include "string.h"
#include "syscall.h"

DIR *opendir(const char *path)
{
    DIR *d = (DIR *)malloc(sizeof(DIR));
    if (!d) return (DIR *)0;
    d->index = 0;
    d->path[0] = '\0';
    if (path) {
        int i = 0;
        while (path[i] && i < 127) { d->path[i] = path[i]; i++; }
        d->path[i] = '\0';
    }
    return d;
}

struct dirent *readdir(DIR *dirp)
{
    if (!dirp) return (struct dirent *)0;

    unsigned int size = 0;
    unsigned char type = 0;
    const char* dir_path = dirp->path[0] ? dirp->path : (const char *)0;
    long r = sys_readdir_ex(dirp->index, dirp->ent.d_name, &size, dir_path, &type);
    if (!r) return (struct dirent *)0;  /* end of directory */

    dirp->ent.d_ino    = (unsigned long)dirp->index;
    dirp->ent.d_off    = 0;
    dirp->ent.d_reclen = sizeof(struct dirent);
    dirp->ent.d_type   = (type == 1) ? DT_DIR : DT_REG;
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
