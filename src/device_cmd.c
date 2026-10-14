// Daemon side of "peri status" and "peri set": find the device an installed module drives, then
// read it or change its running brightness. The only frames sent are the ones in razer_dev.h.

#include "device_cmd.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "controls.h"
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
    // value the device keeps (storage 1), not necessarily the one it is showing right now. Mute
    // and dial are what the mic last reported to the daemon, "unknown" until it does.
    struct razer_state controls = controls_state(m.id);
    char dial[8] = "unknown";
    if (controls.dial >= 0)
        snprintf(dial, sizeof dial, "%d", controls.dial);
    snprintf(reply, size, "ok module=%s firmware=%02x%02x brightness=%u mute=%s dial=%s\n", m.id,
             firmware[0], firmware[1], brightness,
             controls.mute < 0 ? "unknown" : controls.mute ? "unmuted" : "muted", dial);
}

// "0000ff" -> {0x00, 0x00, 0xff}. Exactly six hex digits.
static bool parse_color(const char *s, uint8_t rgb[3])
{
    if (strlen(s) != 6)
        return false;
    for (int i = 0; i < 6; i++)
        if (!isxdigit((unsigned char)s[i]))
            return false;
    for (int i = 0; i < 3; i++) {
        char pair[3] = { s[2 * i], s[2 * i + 1], '\0' };
        rgb[i] = (uint8_t)strtoul(pair, nullptr, 16);
    }
    return true;
}

static int effect_id(const char *name)
{
    static const struct { const char *name; int id; } effects[] = {
        { "none", RAZER_EFFECT_NONE }, { "static", RAZER_EFFECT_STATIC },
        { "breathing", RAZER_EFFECT_BREATHING }, { "spectrum", RAZER_EFFECT_SPECTRUM },
    };
    for (size_t i = 0; i < sizeof effects / sizeof *effects; i++)
        if (strcmp(effects[i].name, name) == 0)
            return effects[i].id;
    return -1;
}

// set <module-id> brightness=<0-255>
// set <module-id> effect=<none|static|breathing|spectrum> [color=<rrggbb>]
// One kind of change per request. The device cannot be asked what its effect or colour is, so
// the reply only says what was sent.
void set_handle(const char *args, char *reply, size_t size)
{
    static const char usage[] = "err usage: set <module-id> brightness=<0-255> | "
                                "effect=<none|static|breathing|spectrum> [color=<rrggbb>]\n";
    char line[128], *rest;
    snprintf(line, sizeof line, "%s", args);
    const char *id = strtok_r(line, " \n", &rest);
    if (!id) {
        snprintf(reply, size, "%s", usage);
        return;
    }

    long brightness = -1, effect = -1;
    const char *effect_name = "";
    uint8_t rgb[3];
    bool has_color = false;
    for (const char *t; (t = strtok_r(nullptr, " \n", &rest));) {
        if (strncmp(t, "brightness=", 11) == 0 && brightness < 0) {
            char *end;
            unsigned long v = strtoul(t + 11, &end, 10);
            if (t[11] == '\0' || *end != '\0' || v > 255) {
                err(reply, size, "brightness must be a whole number from 0 to 255", id);
                return;
            }
            brightness = (long)v;
        } else if (strncmp(t, "effect=", 7) == 0 && effect < 0) {
            effect = effect_id(t + 7);
            effect_name = t + 7;
            if (effect < 0) {
                err(reply, size, "effect must be none, static, breathing or spectrum", id);
                return;
            }
        } else if (strncmp(t, "color=", 6) == 0 && !has_color) {
            if (!parse_color(t + 6, rgb)) {
                err(reply, size, "color must be six hex digits, like 0000ff", id);
                return;
            }
            has_color = true;
        } else {
            snprintf(reply, size, "%s", usage);
            return;
        }
    }
    bool bright_set = brightness >= 0, effect_set = effect >= 0;
    if (bright_set == effect_set || (bright_set && has_color) || (has_color && !effect_set)) {
        snprintf(reply, size, "%s", usage);
        return;
    }
    if (effect_set && razer_effect_args(0, (uint8_t)effect, has_color ? rgb : nullptr,
                                        (uint8_t[RAZER_EFFECT_ARGS_MAX]){}) == 0) {
        err(reply, size, "this effect needs a color=<rrggbb>", id);
        return;
    }

    struct peri_module m;
    int fd = open_module_device(id, &m, reply, size);
    if (fd < 0)
        return;

    if (effect_set) {
        int rc = razer_set_effect(fd, RAZER_STORE_NONE, (uint8_t)effect, has_color ? rgb : nullptr);
        close(fd);
        if (rc != 0) {
            err(reply, size, "the device did not accept the effect", id);
            return;
        }
        snprintf(reply, size, "ok module=%s sent=%s color=%s storage=none readback=unavailable\n",
                 m.id, effect_name, has_color ? "yes" : "no");
        return;
    }

    // The module must state a limit, as for the keyboard: no limit, no brightness writes.
    if (m.max_brightness == 0 || brightness > m.max_brightness) {
        close(fd);
        err(reply, size, m.max_brightness ? "brightness is above the module's limit"
                                          : "the module sets no brightness limit", id);
        return;
    }

    uint8_t now;
    bool ok = razer_set_brightness(fd, RAZER_STORE_NONE, (uint8_t)brightness) == 0
              && razer_brightness(fd, RAZER_STORE_NONE, &now) == 0;
    close(fd);
    if (!ok) {
        err(reply, size, "the device did not accept the change", id);
        return;
    }
    snprintf(reply, size, "ok module=%s brightness=%u storage=none\n", m.id, now);
}
