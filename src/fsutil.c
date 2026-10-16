#include "fsutil.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
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

bool peri_write_file_atomic(const char *path, const char *data, size_t len)
{
    if (!path || (len > 0 && !data)) {
        errno = EINVAL;
        return false;
    }
    // The temp file sits next to the target so rename stays on one filesystem.
    char tmp[PATH_MAX];
    const char *slash = strrchr(path, '/');
    int n = slash ? snprintf(tmp, sizeof tmp, "%.*s/.%s.tmp", (int)(slash - path), path, slash + 1)
                  : snprintf(tmp, sizeof tmp, ".%s.tmp", path);
    if (n < 0 || n >= (int)sizeof tmp) {
        errno = ENAMETOOLONG;
        return false;
    }
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0644);
    if (fd < 0)
        return false;
    // peri_write_all can fail without setting errno (a write that returns 0),
    // so the failure is its own flag and errno only names it.
    errno = 0;
    bool ok = peri_write_all(fd, data, len);
    int err = ok ? 0 : (errno ? errno : EIO);
    // A full disk can show only at close: that is a failed write too.
    if (close(fd) != 0 && ok) {
        ok = false;
        err = errno ? errno : EIO;
    }
    if (ok && rename(tmp, path) < 0) {
        ok = false;
        err = errno ? errno : EIO;
    }
    if (!ok) {
        unlink(tmp); // the old file stays as it was
        errno = err;
        return false;
    }
    return true;
}
