#pragma once

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>

// Fill addr with $XDG_RUNTIME_DIR/peri.sock. Returns 0, or -1 if unavailable.
static inline int peri_socket_addr(struct sockaddr_un *addr)
{
    const char *dir = getenv("XDG_RUNTIME_DIR");
    if (!dir || !*dir)
        return -1;

    *addr = (struct sockaddr_un){ .sun_family = AF_UNIX };
    int n = snprintf(addr->sun_path, sizeof addr->sun_path, "%s/peri.sock", dir);
    return (n < 0 || (size_t)n >= sizeof addr->sun_path) ? -1 : 0;
}

// "0102" -> {0x01, 0x02}. Returns the byte count, or -1 if malformed.
static inline ssize_t peri_parse_hex(const char *s, uint8_t *out, size_t max)
{
    size_t len = strlen(s);
    if (len == 0 || len % 2 != 0 || len / 2 > max)
        return -1;
    for (size_t i = 0; i < len; i += 2) {
        if (!isxdigit((unsigned char)s[i]) || !isxdigit((unsigned char)s[i + 1]))
            return -1;
        char pair[3] = { s[i], s[i + 1], '\0' };
        out[i / 2] = (uint8_t)strtoul(pair, nullptr, 16);
    }
    return (ssize_t)(len / 2);
}
