#include "json.h"

#include <stdint.h>

// Length of the valid UTF-8 sequence at s (1 to 4), or 0 if there is none.
static int utf8_len(const uint8_t *s)
{
    if (s[0] < 0x80)
        return 1;
    int len;
    uint8_t lo = 0x80, hi = 0xBF; // allowed range of the second byte
    if (s[0] >= 0xC2 && s[0] <= 0xDF) {
        len = 2;
    } else if (s[0] >= 0xE0 && s[0] <= 0xEF) {
        len = 3;
        if (s[0] == 0xE0)
            lo = 0xA0; // no overlong forms
        if (s[0] == 0xED)
            hi = 0x9F; // no surrogates
    } else if (s[0] >= 0xF0 && s[0] <= 0xF4) {
        len = 4;
        if (s[0] == 0xF0)
            lo = 0x90;
        if (s[0] == 0xF4)
            hi = 0x8F; // nothing above U+10FFFF
    } else {
        return 0;
    }
    if (s[1] < lo || s[1] > hi)
        return 0;
    for (int i = 2; i < len; i++)
        if (s[i] < 0x80 || s[i] > 0xBF) // a NUL ends the string, so a short tail fails here
            return 0;
    return len;
}

void peri_json_string(FILE *f, const char *s)
{
    fputc('"', f);
    for (const uint8_t *p = (const uint8_t *)s; *p;) {
        if (*p == '"' || *p == '\\') {
            fputc('\\', f);
            fputc(*p++, f);
        } else if (*p < 0x20 || *p == 0x7F) {
            fprintf(f, "\\u%04x", *p++);
        } else if (*p < 0x80) {
            fputc(*p++, f);
        } else {
            int len = utf8_len(p);
            if (len == 0) {
                fputs("\\ufffd", f);
                p++;
            } else {
                fwrite(p, 1, (size_t)len, f);
                p += len;
            }
        }
    }
    fputc('"', f);
}
