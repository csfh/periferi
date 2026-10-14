// Daemon side of "peri status": find the device an installed module drives and read it.
// Read-only: the only frames sent are the two queries in razer_dev.h.

#include "status_cmd.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "module.h"
#include "razer_dev.h"
#include "scan.h"

static void err(char *reply, size_t size, const char *what, const char *id)
{
    snprintf(reply, size, "err %s: %s\n", id, what);
}

void status_handle(const char *args, char *reply, size_t size)
{
    char id[64];
    snprintf(id, sizeof id, "%s", args);
    id[strcspn(id, " \n")] = '\0';
    if (!peri_module_id_valid(id)) {
        snprintf(reply, size, "err usage: status <module-id> (see `peri modules`)\n");
        return;
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
        return;
    case PERI_SELECT_NOT_CONNECTED:
        err(reply, size, "the device is not connected", id);
        return;
    case PERI_SELECT_AMBIGUOUS:
        err(reply, size, "more than one device matches; choosing one is not supported yet", id);
        return;
    }
    if (strcmp(m->engine, "razer") != 0) {
        err(reply, size, "this module has no status to read yet", id);
        return;
    }

    int fd = razer_open(node);
    if (fd < 0) {
        err(reply, size, "cannot open the device (see `journalctl --user -u perid`)", id);
        return;
    }
    uint8_t firmware[2], brightness;
    bool ok = razer_firmware(fd, firmware) == 0 && razer_brightness(fd, &brightness) == 0;
    close(fd);
    if (!ok) {
        err(reply, size, "the device did not answer a read query", id);
        return;
    }
    // The firmware bytes are shown raw: what they mean is not established.
    snprintf(reply, size, "ok module=%s firmware=%02x%02x brightness=%u\n", m->id, firmware[0],
             firmware[1], brightness);
}
