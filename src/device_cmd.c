// Daemon side of "peri status" and "peri set": find the device an installed module drives, then
// read it or change its running brightness. The only frames sent are the ones in razer_dev.h.

#include "device_cmd.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "module.h"
#include "razer_dev.h"
#include "scan.h"

static void err(char *reply, size_t size, const char *what, const char *id)
{
    snprintf(reply, size, "err %s: %s\n", id, what);
}

// Resolve a module id to an open device. Returns an fd, or -1 with an "err ..." reply.
static int open_module_device(const char *id, struct peri_module *module, char *reply, size_t size)
{
    if (!peri_module_id_valid(id)) {
        snprintf(reply, size, "err usage: <module-id> (see `peri modules`)\n");
        return -1;
    }

    char dir[PATH_MAX];
    struct peri_module installed[32];
    size_t installed_count = peri_installed_dir(dir, sizeof dir)
                                 ? peri_modules_scan(dir, installed, 32) : 0;
    struct peri_device devs[32];
    size_t dev_count = peri_scan(devs, 32);

    char node[32];
    const struct peri_module *m;
    switch (peri_module_select(devs, dev_count, installed, installed_count, id, node, sizeof node,
                               &m)) {
    case PERI_SELECT_OK:
        break;
    case PERI_SELECT_NOT_INSTALLED:
        err(reply, size, "no such installed module; run `peri use <id>` (see `peri modules`)", id);
        return -1;
    case PERI_SELECT_NOT_CONNECTED:
        err(reply, size, "the device is not connected", id);
        return -1;
    case PERI_SELECT_AMBIGUOUS:
        err(reply, size, "more than one device matches; choosing one is not supported yet", id);
        return -1;
    }
    if (strcmp(m->engine, "razer") != 0) {
        err(reply, size, "this module has nothing to read or set yet", id);
        return -1;
    }

    int fd = razer_open(node);
    if (fd < 0) {
        err(reply, size, "cannot open the device (see `journalctl --user -u perid`)", id);
        return -1;
    }
    *module = *m;
    return fd;
}

static void first_word(const char *args, char *out, size_t size)
{
    snprintf(out, size, "%s", args);
    out[strcspn(out, " \n")] = '\0';
}

void status_handle(const char *args, char *reply, size_t size)
{
    char id[64];
    first_word(args, id, sizeof id);

    struct peri_module m;
    int fd = open_module_device(id, &m, reply, size);
    if (fd < 0)
        return;
    uint8_t firmware[2], brightness;
    bool ok = razer_firmware(fd, firmware) == 0
              && razer_brightness(fd, RAZER_STORE_FLASH, &brightness) == 0;
    close(fd);
    if (!ok) {
        err(reply, size, "the device did not answer a read query", id);
        return;
    }
    // The firmware bytes are shown raw: what they mean is not established. Brightness is the
    // value the device keeps (storage 1), not necessarily the one it is showing right now.
    snprintf(reply, size, "ok module=%s firmware=%02x%02x brightness=%u\n", m.id, firmware[0],
             firmware[1], brightness);
}

void set_handle(const char *args, char *reply, size_t size)
{
    char line[128], *rest;
    snprintf(line, sizeof line, "%s", args);
    const char *id = strtok_r(line, " \n", &rest);
    const char *setting = strtok_r(nullptr, " \n", &rest);
    if (!id || !setting || strtok_r(nullptr, " \n", &rest)
        || strncmp(setting, "brightness=", 11) != 0) {
        snprintf(reply, size, "err usage: set <module-id> brightness=<0-255>\n");
        return;
    }
    char *end;
    unsigned long value = strtoul(setting + 11, &end, 10);
    if (setting[11] == '\0' || *end != '\0' || value > 255) {
        err(reply, size, "brightness must be a whole number from 0 to 255", id);
        return;
    }

    struct peri_module m;
    int fd = open_module_device(id, &m, reply, size);
    if (fd < 0)
        return;
    // The module must state a limit, as for the keyboard: no limit, no brightness writes.
    if (m.max_brightness == 0 || value > m.max_brightness) {
        close(fd);
        err(reply, size, m.max_brightness ? "brightness is above the module's limit"
                                          : "the module sets no brightness limit", id);
        return;
    }

    uint8_t now;
    bool ok = razer_set_brightness(fd, RAZER_STORE_NONE, (uint8_t)value) == 0
              && razer_brightness(fd, RAZER_STORE_NONE, &now) == 0;
    close(fd);
    if (!ok) {
        err(reply, size, "the device did not accept the change", id);
        return;
    }
    snprintf(reply, size, "ok module=%s brightness=%u storage=none\n", m.id, now);
}
