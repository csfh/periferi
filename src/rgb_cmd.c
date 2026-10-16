// Daemon side of "peri rgb": find the XAP keyboard, apply settings, report.

#include "rgb_cmd.h"

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "common.h"
#include "module.h"
#include "request.h"
#include "rgb_settings.h"
#include "scan.h"
#include "xap.h"

[[gnu::format(printf, 3, 4)]]
static void fail(char *reply, size_t size, const char *fmt, ...)
{
    int n = snprintf(reply, size, "err ");
    va_list ap;
    va_start(ap, fmt);
    if (n > 0 && (size_t)n < size)
        vsnprintf(reply + n, size - (size_t)n, fmt, ap);
    va_end(ap);
    size_t len = strlen(reply);
    if (len + 2 <= size)
        memcpy(reply + len, "\n", 2);
}

static bool drives_rgb(const struct peri_module *m)
{
    return m->rgb && strcmp(m->engine, "xap") == 0;
}

// A connected device that a bundled module could drive, but that is not installed.
static const struct peri_module *suggest(const struct peri_device *devs, size_t dev_count,
                                         const struct peri_module *catalog, size_t catalog_count)
{
    for (size_t c = 0; c < catalog_count; c++)
        for (size_t i = 0; i < dev_count; i++)
            if (drives_rgb(&catalog[c]) && peri_module_matches(&catalog[c], &devs[i]))
                return &catalog[c];
    return nullptr;
}

// Devices are not addressed by node number (it changes on re-enumeration).
// Only a device that an installed module matches is touched. With one such
// keyboard attached there is nothing to choose.
static bool find_xap_node(char *node, size_t node_size, struct peri_module *module, char *reply,
                          size_t size)
{
    struct peri_device devs[32];
    size_t count = peri_scan(devs, 32);

    char dir[PATH_MAX];
    struct peri_module installed[32], catalog[32];
    size_t installed_count = 0, catalog_count = 0;
    if (peri_installed_dir(dir, sizeof dir))
        installed_count = peri_modules_scan(dir, installed, 32);

    size_t found = 0;
    for (size_t i = 0; i < count; i++)
        for (size_t m = 0; m < installed_count; m++)
            if (drives_rgb(&installed[m]) && peri_module_matches(&installed[m], &devs[i])
                && found++ == 0) {
                snprintf(node, node_size, "%s", devs[i].node);
                *module = installed[m];
            }
    if (found == 0) {
        if (peri_catalog_dir(dir, sizeof dir))
            catalog_count = peri_modules_scan(dir, catalog, 32);
        const struct peri_module *m = suggest(devs, count, catalog, catalog_count);
        if (m)
            fail(reply, size, "no module installed for %04x:%04x; run `peri use %s`", m->vid,
                 m->pid, m->id);
        else
            fail(reply, size, "no keyboard found for an installed module (see `peri modules`)");
        return false;
    }
    if (found > 1) {
        fail(reply, size, "%zu XAP keyboards found; choosing one is not supported yet", found);
        return false;
    }
    return true;
}

// maxval is the brightness limit the keyboard's module sets (0 if it sets none).
static void report(const struct xap_rgb *cfg, uint8_t maxval, const char *extra, char *reply,
                   size_t size)
{
    const char *effect = xap_effect_name(cfg->mode);
    snprintf(reply, size,
             "ok enabled=%s effect=%s mode=0x%02x hue=%u sat=%u val=%u speed=%u flags=0x%02x "
             "maxval=%u%s\n",
             cfg->enable ? "yes" : "no", effect ? effect : "unknown", cfg->mode, cfg->hue,
             cfg->sat, cfg->val, cfg->speed, cfg->flags, maxval, extra);
}

void rgb_handle(const char *args, char *reply, size_t size)
{
    char line[PERI_REQUEST_MAX + 1];
    snprintf(line, sizeof line, "%s", args);
    char *rest;
    const char *verb = strtok_r(line, " \n", &rest);
    bool set = verb && strcmp(verb, "set") == 0;
    bool save = verb && strcmp(verb, "save") == 0;
    bool effects = verb && strcmp(verb, "effects") == 0;
    if (!verb || (!set && !save && !effects && strcmp(verb, "get") != 0)) {
        fail(reply, size, "usage: rgb get | set <key=value>... | save | effects");
        return;
    }
    if (!set && strtok_r(nullptr, " \n", &rest)) { // everything but set takes no arguments
        fail(reply, size, "'%s' takes no arguments", verb);
        return;
    }

    char node[32];
    struct peri_module module;
    if (!find_xap_node(node, sizeof node, &module, reply, size))
        return;
    int fd = xap_open(node);
    if (fd < 0) {
        fail(reply, size, "cannot open %s (see `journalctl --user -u perid`)", node);
        return;
    }

    if (effects) { // the firmware's own list of enabled effects
        uint64_t mask;
        char names[900];
        if (xap_rgb_effects(fd, &mask) < 0 || !rgb_effects_csv(mask, names, sizeof names))
            fail(reply, size, "could not read the effect list from %s", node);
        else
            snprintf(reply, size, "ok effects=%s\n", names);
        close(fd);
        return;
    }

    // Read the current config first so a set only changes what it names.
    struct xap_rgb cfg;
    bool ok = xap_rgb_get(fd, &cfg) == 0;
    if (!ok)
        fail(reply, size, "could not read the lighting config from %s", node);

    if (ok && set) {
        int settings = 0;
        for (const char *s; ok && (s = strtok_r(nullptr, " \n", &rest)); settings++) {
            char err[200];
            ok = rgb_apply_setting(&cfg, s, module.max_brightness, module.id, err, sizeof err);
            if (!ok)
                fail(reply, size, "%s", err);
        }
        if (ok && settings == 0) {
            fail(reply, size, "set needs at least one key=value");
            ok = false;
        }
        // Send it, then read it back so the reply is what the keyboard holds.
        if (ok && (xap_rgb_set(fd, &cfg) < 0 || xap_rgb_get(fd, &cfg) < 0)) {
            fail(reply, size, "the keyboard did not accept the change");
            ok = false;
        }
    }
    // Persist the running config to the keyboard's EEPROM. Only on this explicit verb.
    if (ok && save && xap_rgb_save(fd) < 0) {
        fail(reply, size, "the keyboard did not save the config");
        ok = false;
    }
    close(fd);

    if (ok)
        report(&cfg, module.max_brightness, save ? " saved=yes" : "", reply, size);
}
