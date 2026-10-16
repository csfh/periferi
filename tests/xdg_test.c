#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/un.h>

#include "../src/common.h"
#include "../src/xdg.h"

static int failures;

#define CHECK(cond)                                                \
    do {                                                           \
        if (!(cond)) {                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                            \
        }                                                          \
    } while (0)

static void clear_bases(void)
{
    unsetenv("XDG_CONFIG_HOME");
    unsetenv("XDG_DATA_HOME");
    unsetenv("XDG_RUNTIME_DIR");
    unsetenv("HOME");
}

int main(void)
{
    char buf[PATH_MAX];

    // A set variable wins over HOME, for both bases that have a default.
    clear_bases();
    setenv("XDG_CONFIG_HOME", "/cfg", 1);
    setenv("XDG_DATA_HOME", "/data", 1);
    setenv("HOME", "/home/ignored", 1);
    CHECK(peri_xdg_path(buf, sizeof buf, PERI_XDG_CONFIG, "peri/state"));
    CHECK(strcmp(buf, "/cfg/peri/state") == 0);
    CHECK(peri_xdg_path(buf, sizeof buf, PERI_XDG_DATA, "peri/modules"));
    CHECK(strcmp(buf, "/data/peri/modules") == 0);
    CHECK(peri_xdg_path(buf, sizeof buf, PERI_XDG_CONFIG, nullptr));
    CHECK(strcmp(buf, "/cfg") == 0);
    CHECK(peri_xdg_path(buf, sizeof buf, PERI_XDG_DATA, ""));
    CHECK(strcmp(buf, "/data") == 0);

    // An empty variable is unset: config and data fall back under HOME.
    clear_bases();
    setenv("XDG_CONFIG_HOME", "", 1);
    setenv("XDG_DATA_HOME", "", 1);
    setenv("HOME", "/home/me", 1);
    CHECK(peri_xdg_path(buf, sizeof buf, PERI_XDG_CONFIG, "systemd/user"));
    CHECK(strcmp(buf, "/home/me/.config/systemd/user") == 0);
    CHECK(peri_xdg_path(buf, sizeof buf, PERI_XDG_DATA, "peri/modules"));
    CHECK(strcmp(buf, "/home/me/.local/share/peri/modules") == 0);

    // Unset, with HOME, is the same fallback. Runtime never uses HOME.
    clear_bases();
    setenv("HOME", "/home/me", 1);
    CHECK(peri_xdg_path(buf, sizeof buf, PERI_XDG_CONFIG, "peri/state"));
    CHECK(strcmp(buf, "/home/me/.config/peri/state") == 0);
    CHECK(!peri_xdg_path(buf, sizeof buf, PERI_XDG_RUNTIME, "peri.sock"));

    // Neither the variable nor HOME: config and data cannot be known.
    clear_bases();
    CHECK(!peri_xdg_path(buf, sizeof buf, PERI_XDG_CONFIG, "peri/state"));
    CHECK(!peri_xdg_path(buf, sizeof buf, PERI_XDG_DATA, "peri/modules"));
    setenv("HOME", "", 1);
    CHECK(!peri_xdg_path(buf, sizeof buf, PERI_XDG_CONFIG, nullptr));

    // A result that does not fit is refused, not truncated.
    clear_bases();
    setenv("XDG_CONFIG_HOME", "/cfg", 1);
    char tiny[4];
    CHECK(!peri_xdg_path(tiny, sizeof tiny, PERI_XDG_CONFIG, nullptr));
    CHECK(!peri_xdg_path(nullptr, 8, PERI_XDG_CONFIG, nullptr));
    CHECK(!peri_xdg_path(buf, 0, PERI_XDG_CONFIG, nullptr));
    CHECK(!peri_xdg_path(buf, sizeof buf, (enum peri_xdg)99, nullptr));

    // The socket address is the runtime dir, and it has to fit sun_path.
    clear_bases();
    setenv("HOME", "/home/me", 1);
    setenv("XDG_RUNTIME_DIR", "/run/user/1000", 1);
    struct sockaddr_un addr;
    CHECK(peri_socket_addr(&addr) == 0);
    CHECK(strcmp(addr.sun_path, "/run/user/1000/peri.sock") == 0);

    char long_runtime[160];
    memset(long_runtime, 'r', sizeof long_runtime - 1);
    long_runtime[0] = '/';
    long_runtime[sizeof long_runtime - 1] = '\0';
    setenv("XDG_RUNTIME_DIR", long_runtime, 1);
    CHECK(peri_xdg_path(buf, sizeof buf, PERI_XDG_RUNTIME, "peri.sock"));
    CHECK(peri_socket_addr(&addr) < 0);

    clear_bases();
    return failures ? 1 : 0;
}
