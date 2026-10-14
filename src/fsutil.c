#include "fsutil.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

bool peri_mkdir_p(const char *dir)
{
    char path[PATH_MAX];
    if (snprintf(path, sizeof path, "%s", dir) >= (int)sizeof path)
        return false;
    for (char *p = path + 1; *p; p++) {
        if (*p != '/')
            continue;
        *p = '\0';
        int r = mkdir(path, 0755);
        *p = '/';
        if (r < 0 && errno != EEXIST)
            return false;
    }
    return mkdir(path, 0755) == 0 || errno == EEXIST;
}

bool peri_write_all(int fd, const char *buf, size_t len)
{
    while (len > 0) {
        ssize_t n = write(fd, buf, len);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return false;
        buf += n;
        len -= (size_t)n;
    }
    return true;
}
