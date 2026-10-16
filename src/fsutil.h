#pragma once

#include <stdbool.h>
#include <stddef.h>

// Small file helpers shared by the module installer and the state store.

// mkdir -p (mode 0755). True if the directory exists afterwards: a file where the
// directory should be is ENOTDIR, and an empty path is ENOENT.
// On failure errno is set, including ENAMETOOLONG when the path does not fit,
// so a caller can perror instead of creating a truncated directory.
bool peri_mkdir_p(const char *dir);

// write() until everything is written, retrying on EINTR. False on any error.
bool peri_write_all(int fd, const char *buf, size_t len);
