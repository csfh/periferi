// The daemon's reader of the microphone's controls: see controls.h.

#include "controls.h"

#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "module.h"
#include "razer_dev.h"
#include "scan.h"

static struct {
    int fd;
    char module[49];
    struct razer_state state;
} attached = { .fd = -1, .state = RAZER_STATE_UNKNOWN };

int controls_fd(void)
{
    return attached.fd;
}

void controls_detach(void)
{
    if (attached.fd >= 0)
        close(attached.fd);
    attached = (typeof(attached)){ .fd = -1, .state = RAZER_STATE_UNKNOWN };
}

void controls_rescan(void)
{
    if (attached.fd >= 0)
        return;

    char dir[PATH_MAX];
    struct peri_module mods[32];
    size_t count = peri_installed_dir(dir, sizeof dir) ? peri_modules_scan(dir, mods, 32) : 0;

    struct peri_device devs[32];
    size_t dev_count = 0;
    bool scanned = false;
    for (size_t i = 0; i < count; i++) {
        if (strcmp(mods[i].engine, "razer") != 0)
            continue;
        if (!scanned) { // only touch sysfs when there is a module that could need it
            dev_count = peri_scan(devs, 32);
            scanned = true;
        }
        char node[32];
        if (peri_module_select(devs, dev_count, mods, count, mods[i].id, node, sizeof node,
                               nullptr) != PERI_SELECT_OK)
            continue;
        int fd = razer_open_events(node);
        if (fd < 0)
            continue;
        attached.fd = fd;
        snprintf(attached.module, sizeof attached.module, "%s", mods[i].id);
        attached.state = RAZER_STATE_UNKNOWN;
        return;
    }
}

void controls_service(short revents)
{
    if (attached.fd < 0)
        return;
    if (revents & (POLLERR | POLLHUP | POLLNVAL)) {
        controls_detach();
        return;
    }

    for (;;) { // drain: the fd is non-blocking, so this never waits
        uint8_t report[64];
        ssize_t n = read(attached.fd, report, sizeof report);
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0 && errno == EAGAIN)
            return;
        if (n <= 0) { // unplugged (ENODEV, EIO) or closed
            controls_detach();
            return;
        }
        struct razer_event ev;
        if (razer_event_decode(report, (size_t)n, &ev))
            razer_state_apply(&attached.state, &ev);
    }
}

struct razer_state controls_state(const char *module_id)
{
    if (attached.fd >= 0 && strcmp(attached.module, module_id) == 0)
        return attached.state;
    return RAZER_STATE_UNKNOWN;
}
