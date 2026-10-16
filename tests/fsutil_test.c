#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../src/fsutil.h"

static int failures;

#define CHECK(cond)                                                \
    do {                                                           \
        if (!(cond)) {                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                            \
        }                                                          \
    } while (0)

static int rm_entry(const char *path, const struct stat *, int, struct FTW *)
{
    return remove(path);
}

int main(void)
{
    char root[] = "/tmp/peri-fsutil-XXXXXX";
    if (!mkdtemp(root))
        return 2;

    char nested[PATH_MAX];
    snprintf(nested, sizeof nested, "%s/a/b/c", root);
    CHECK(peri_mkdir_p(nested));
    struct stat st;
    CHECK(stat(nested, &st) == 0 && S_ISDIR(st.st_mode));
    CHECK(peri_mkdir_p(nested)); // already there is success, not an error

    char file[PATH_MAX], under[PATH_MAX];
    snprintf(file, sizeof file, "%s/file", root);
    snprintf(under, sizeof under, "%s/file/nope", root);
    int fd = open(file, O_CREAT | O_WRONLY | O_CLOEXEC, 0644);
    CHECK(fd >= 0);
    if (fd >= 0)
        close(fd);
    errno = 0;
    CHECK(!peri_mkdir_p(under));
    CHECK(errno == ENOTDIR);

    char too_long[PATH_MAX + 8];
    memset(too_long, 'a', PATH_MAX);
    too_long[PATH_MAX] = '\0';
    errno = 0;
    CHECK(!peri_mkdir_p(too_long));
    CHECK(errno == ENAMETOOLONG);

    // The path itself being a file is not "the directory exists".
    errno = 0;
    CHECK(!peri_mkdir_p(file));
    CHECK(errno == ENOTDIR);

    errno = 0;
    CHECK(!peri_mkdir_p(""));
    CHECK(errno == ENOENT);

    errno = 0;
    CHECK(!peri_mkdir_p(nullptr));
    CHECK(errno == EINVAL);

    nftw(root, rm_entry, 16, FTW_DEPTH | FTW_PHYS);
    if (failures) {
        printf("%d failed\n", failures);
        return 1;
    }
    printf("ok\n");
    return 0;
}
