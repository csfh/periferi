#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../src/device_cmd.h"
#include "../src/request.h"

static int failures;

#define CHECK(cond)                                                \
    do {                                                           \
        if (!(cond)) {                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                            \
        }                                                          \
    } while (0)

// Every request here is refused before a device is opened: a bad value, or a
// shape that is not one brightness or one effect. Nothing in this file is a
// set the daemon would send.
static void refuse(const char *args, const char *expect)
{
    char reply[512] = "";
    set_handle(args, reply, sizeof reply);
    if (strncmp(reply, "err ", 4) != 0 || !strstr(reply, expect)) {
        printf("FAIL set %s\n  got: %s\n", args, reply);
        failures++;
    }
}

// A fov request of the longest line the CLI may send: perid hands the handler
// everything after "fov ", up to PERI_REQUEST_MAX bytes with the newline. The
// handler's copy must hold all of it: a cut value would be a different, valid one.
static void fov_refuse_padded(void)
{
    char args[PERI_REQUEST_MAX + 1] = "BRIO set ";
    size_t n = strlen(args);
    size_t total = PERI_REQUEST_MAX - strlen("fov "); // the newline is part of it
    memset(args + n, '0', total - n - 3);
    memcpy(args + total - 3, "10\n", 4);
    char reply[512] = "";
    // An uppercase id is no module id: nothing here can reach a device.
    fov_handle(args, reply, sizeof reply);
    if (strncmp(reply, "err ", 4) != 0 || !strstr(reply, "wide, medium or narrow")) {
        printf("FAIL fov padded\n  got: %s\n", reply);
        failures++;
    }
}

int main(void)
{
    char root[] = "/tmp/peri-set-test-XXXXXX";
    if (!mkdtemp(root))
        return 2;
    // No installed modules, so a request cannot be routed at a real device.
    setenv("XDG_DATA_HOME", root, 1);

    refuse("", "usage:");
    refuse("brio", "usage:");
    refuse("brio brightness=10 effect=none", "usage:");
    refuse("brio color=0000ff", "usage:");
    refuse("brio effect=static effect=none", "usage:");

    refuse("brio speed=warp effect=static color=0000ff", "speed must be slow or fast");
    refuse("brio color=zz speed=warp", "color must be six hex digits, like 0000ff");
    refuse("brio effect=rainbow",
           "effect must be none, static, breathing, spectrum, wave or wave-reverse");
    refuse("brio brightness=256", "brightness must be a whole number from 0 to 255");
    refuse("brio color2=12345", "color2 must be six hex digits, like ff0000");
    refuse("brio effect=static", "this effect needs a valid color=<rrggbb>");
    refuse("brio effect=breathing color2=ff0000", "a second colour needs a first");

    fov_refuse_padded();

    if (rmdir(root) != 0)
        failures++;
    if (failures) {
        printf("%d failed\n", failures);
        return 1;
    }
    printf("ok\n");
    return 0;
}
