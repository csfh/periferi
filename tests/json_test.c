#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/json.h"

static int failures;

// What peri_json_string writes for s.
static char *encode(const char *s)
{
    char *buf = nullptr;
    size_t size = 0;
    FILE *f = open_memstream(&buf, &size);
    peri_json_string(f, s);
    fclose(f);
    return buf;
}

static void expect(const char *input, const char *want)
{
    char *got = encode(input);
    if (strcmp(got, want) != 0) {
        printf("FAIL: encoded to %s, wanted %s\n", got, want);
        failures++;
    }
    free(got);
}

int main(void)
{
    expect("", "\"\"");
    expect("Drop CTRL v2", "\"Drop CTRL v2\"");
    expect("say \"hi\"\\", "\"say \\\"hi\\\"\\\\\"");
    expect("a\nb\tc\x01" "d\x7f", "\"a\\u000ab\\u0009c\\u0001d\\u007f\"");

    // Valid UTF-8 passes through: 2, 3 and 4 byte characters.
    expect("caf\xC3\xA9", "\"caf\xC3\xA9\"");
    expect("\xE2\x82\xAC", "\"\xE2\x82\xAC\"");
    expect("\xF0\x9F\x8E\xB9", "\"\xF0\x9F\x8E\xB9\"");

    // A character cut in half by truncation: every leftover byte is replaced.
    expect("ab\xC3", "\"ab\\ufffd\"");
    expect("ab\xE2\x82", "\"ab\\ufffd\\ufffd\"");
    expect("ab\xF0\x9F\x8E", "\"ab\\ufffd\\ufffd\\ufffd\"");

    // Stray continuation, overlong, surrogate, beyond U+10FFFF, 0xFF.
    expect("\x80", "\"\\ufffd\"");
    expect("\xC0\x80", "\"\\ufffd\\ufffd\"");
    expect("\xED\xA0\x80", "\"\\ufffd\\ufffd\\ufffd\"");
    expect("\xF4\x90\x80\x80", "\"\\ufffd\\ufffd\\ufffd\\ufffd\"");
    expect("\xFF", "\"\\ufffd\"");
    expect("\xF5\x80\x80\x80", "\"\\ufffd\\ufffd\\ufffd\\ufffd\""); // lead bytes above F4
    expect("\xC1\xBF", "\"\\ufffd\\ufffd\""); // C0 and C1 are never valid leads

    // Text after bad bytes still comes through.
    expect("\xE2" "x", "\"\\ufffdx\"");
    return failures ? 1 : 0;
}
