#include "fsutil.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

bool peri_mkdir_p(const char *dir)
{
    char path[PATH_MAX];
    if (!dir) {
        errno = EINVAL;
        return false;
    }
    if (!*dir) {
        errno = ENOENT; // as mkdir("") says
        return false;
    }
    int n = snprintf(path, sizeof path, "%s", dir);
    // A path that does not fit must not be created truncated: setup's perror
    // is how that refusal is reported.
    if (n < 0) {
        errno = EINVAL;
        return false;
    }
    if (n >= (int)sizeof path) {
        errno = ENAMETOOLONG;
        return false;
    }
    for (char *p = path + 1; *p; p++) {
        if (*p != '/')
            continue;
        *p = '\0';
        int r = mkdir(path, 0755);
        *p = '/';
        if (r < 0 && errno != EEXIST)
            return false;
    }
    if (mkdir(path, 0755) == 0)
        return true;
    if (errno != EEXIST)
        return false;
    // EEXIST is success only for a directory: a file in the way is not one.
    struct stat st;
    if (stat(path, &st) != 0)
        return false;
    if (!S_ISDIR(st.st_mode)) {
        errno = ENOTDIR;
        return false;
    }
    return true;
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
