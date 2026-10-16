#include "request.h"

#include <string.h>

// A single daemon token. Anything at or below ASCII space (space, tab, CR, LF)
// would be split by the daemon's strtok, or would end the line early.
static bool token_ok(const char *s)
{
    if (!s || !*s)
        return false;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++)
        if (*p <= ' ' || *p == 0x7f)
            return false;
    return true;
}

bool peri_request_line(char *buf, size_t size, const char *verb, char *const *words, int nwords)
{
    if (buf && size > 0)
        buf[0] = '\0';
    if (!buf || size == 0 || !token_ok(verb) || nwords < 0 || (nwords > 0 && !words))
        return false;
    for (int i = 0; i < nwords; i++)
        if (!token_ok(words[i]))
            return false;

    // Fit the daemon's read first, then the caller's buffer. A caller that
    // passes a larger buffer must still not produce a line perid would cut.
    char line[PERI_REQUEST_MAX + 1];
    size_t len = strlen(verb);
    if (len + 1 > PERI_REQUEST_MAX)
        return false;
    memcpy(line, verb, len);
    for (int i = 0; i < nwords; i++) {
        size_t wlen = strlen(words[i]);
        if (len + 1 + wlen + 1 > PERI_REQUEST_MAX)
            return false;
        line[len++] = ' ';
        memcpy(line + len, words[i], wlen);
        len += wlen;
    }
    line[len++] = '\n';
    line[len] = '\0';
    if (len + 1 > size)
        return false;
    memcpy(buf, line, len + 1);
    return true;
}
