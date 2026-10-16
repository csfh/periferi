#include <stdio.h>
#include <string.h>

#include "../src/service.h"

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
    char buf[1024];

    CHECK(peri_user_unit(buf, sizeof buf, "/usr/bin/perid", "/home/a/.config",
                         "/home/a/.local/share"));
    CHECK(strcmp(buf,
                 "[Unit]\n"
                 "Description=peri peripheral daemon\n"
                 "\n"
                 "[Service]\n"
                 "ExecStart=\"/usr/bin/perid\"\n"
                 "# Directories `peri setup` resolved. A user service does not see the\n"
                 "# shell's XDG_* variables, so these pin the ones the modules were\n"
                 "# installed under. The runtime directory stays the session's own.\n"
                 "Environment=\"XDG_CONFIG_HOME=/home/a/.config\"\n"
                 "Environment=\"XDG_DATA_HOME=/home/a/.local/share\"\n"
                 "Restart=on-failure\n"
                 "\n"
                 "[Install]\n"
                 "WantedBy=default.target\n") == 0);
    // The session runtime dir must not be pinned; it has no default and moves.
    CHECK(strstr(buf, "XDG_RUNTIME_DIR") == nullptr);

    // A space stays inside the quotes, so the assignment is still one value.
    CHECK(peri_user_unit(buf, sizeof buf, "/opt/my peri/perid", "/home/a/.config",
                         "/home/a/.local/share"));
    CHECK(strstr(buf, "ExecStart=\"/opt/my peri/perid\"\n") != nullptr);

    const char *bad_perid[] = { "perid", "relative/perid", "/usr/bin/perid\"",
                                "/usr/bin/perid\\x", "/usr/bin/%h/perid", "/usr/bin/${HOME}/perid",
                                "/usr/bin/perid\n", nullptr };
    for (int i = 0; bad_perid[i]; i++) {
        strcpy(buf, "stale");
        CHECK(!peri_user_unit(buf, sizeof buf, bad_perid[i], "/cfg", "/data"));
        CHECK(buf[0] == '\0');
    }
    CHECK(!peri_user_unit(buf, sizeof buf, "/usr/bin/perid", "cfg", "/data"));
    CHECK(!peri_user_unit(buf, sizeof buf, "/usr/bin/perid", "/cfg", "/data%"));
    CHECK(!peri_user_unit(buf, sizeof buf, "/usr/bin/perid", nullptr, "/data"));
    CHECK(!peri_user_unit(nullptr, 8, "/usr/bin/perid", "/cfg", "/data"));
    CHECK(!peri_user_unit(buf, 0, "/usr/bin/perid", "/cfg", "/data"));

    // Too small to hold the unit: refuse, and do not leave a partial file.
    char tiny[32];
    strcpy(tiny, "stale");
    CHECK(!peri_user_unit(tiny, sizeof tiny, "/usr/bin/perid", "/home/a/.config",
                          "/home/a/.local/share"));
    CHECK(tiny[0] == '\0');

    if (failures) {
        printf("%d failed\n", failures);
        return 1;
    }
    printf("ok\n");
    return 0;
}
