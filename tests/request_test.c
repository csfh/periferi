#include <stdio.h>
#include <string.h>

#include "../src/request.h"

static int failures;

#define CHECK(cond)                                                \
    do {                                                           \
        if (!(cond)) {                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                            \
        }                                                          \
    } while (0)

int main(void)
{
    char buf[PERI_REQUEST_MAX + 8];

    CHECK(peri_request_line(buf, sizeof buf, "ping", nullptr, 0));
    CHECK(strcmp(buf, "ping\n") == 0);

    char *words[] = { "brio", "effect=static", "color=0000ff" };
    CHECK(peri_request_line(buf, sizeof buf, "set", words, 3));
    CHECK(strcmp(buf, "set brio effect=static color=0000ff\n") == 0);

    // The line the daemon can read whole: 255 bytes including the newline.
    char word[PERI_REQUEST_MAX];
    memset(word, 'a', 250);
    word[250] = '\0';
    CHECK(peri_request_line(buf, sizeof buf, "set", (char *[]){ word }, 1));
    CHECK(strlen(buf) == PERI_REQUEST_MAX);
    CHECK(buf[PERI_REQUEST_MAX - 1] == '\n');

    // One byte over the daemon's read, even though the caller buffer is larger.
    word[250] = 'b';
    word[251] = '\0';
    strcpy(buf, "stale");
    CHECK(!peri_request_line(buf, sizeof buf, "set", (char *[]){ word }, 1));
    CHECK(buf[0] == '\0');

    // A verb alone that cannot fit: 255 letters plus the newline is one over.
    char verb[PERI_REQUEST_MAX + 1];
    memset(verb, 'v', PERI_REQUEST_MAX);
    verb[PERI_REQUEST_MAX] = '\0';
    CHECK(!peri_request_line(buf, sizeof buf, verb, nullptr, 0));

    char small[8];
    strcpy(small, "stale");
    CHECK(!peri_request_line(small, sizeof small, "status", (char *[]){ "brio" }, 1));
    CHECK(small[0] == '\0');

    CHECK(!peri_request_line(buf, sizeof buf, "set", (char *[]){ "a b" }, 1));
    CHECK(!peri_request_line(buf, sizeof buf, "set", (char *[]){ "a\nb" }, 1));
    CHECK(!peri_request_line(buf, sizeof buf, "set", (char *[]){ "a\tb" }, 1));
    CHECK(!peri_request_line(buf, sizeof buf, "set", (char *[]){ "" }, 1));
    CHECK(!peri_request_line(buf, sizeof buf, "", nullptr, 0));
    CHECK(!peri_request_line(buf, sizeof buf, "set", nullptr, 1));
    CHECK(!peri_request_line(buf, sizeof buf, "set", words, -1));
    CHECK(!peri_request_line(nullptr, 8, "ping", nullptr, 0));
    CHECK(!peri_request_line(buf, 0, "ping", nullptr, 0));

    if (failures) {
        printf("%d failed\n", failures);
        return 1;
    }
    printf("ok\n");
    return 0;
}
