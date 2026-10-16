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

    // A verb is a whole word. "list" is not "listen", and "set" is not "status".
    const char *rest = "stale";
    CHECK(peri_request_verb("list\n", "list", &rest) && rest == nullptr);
    CHECK(peri_request_verb("list", "list", &rest) && rest == nullptr);
    CHECK(peri_request_verb("list --all\n", "list", &rest) && strcmp(rest, "--all\n") == 0);
    CHECK(!peri_request_verb("listen", "list", &rest) && strcmp(rest, "--all\n") == 0);
    CHECK(!peri_request_verb("listfoo", "list", nullptr));
    CHECK(peri_request_verb("ping\n", "ping", nullptr));
    CHECK(peri_request_verb("ping extra", "ping", &rest) && strcmp(rest, "extra") == 0);
    CHECK(peri_request_verb("status id\n", "status", &rest) && strcmp(rest, "id\n") == 0);
    CHECK(!peri_request_verb("status id\n", "set", nullptr));
    CHECK(peri_request_verb("set brightness=1\n", "set", &rest) && rest[0] == 'b');
    CHECK(!peri_request_verb("settings\n", "set", nullptr));
    CHECK(peri_request_verb("use\n", "use", &rest) && rest == nullptr);
    CHECK(peri_request_verb("use mic\n", "use", &rest) && strcmp(rest, "mic\n") == 0);
    CHECK(!peri_request_verb("user\n", "use", nullptr));
    CHECK(!peri_request_verb(nullptr, "list", nullptr));
    CHECK(!peri_request_verb("list", "", nullptr));
    CHECK(!peri_request_verb("list", nullptr, nullptr));

    if (failures) {
        printf("%d failed\n", failures);
        return 1;
    }
    printf("ok\n");
    return 0;
}
