// Daemon side of "peri rgb": find the XAP keyboard, apply settings, report.

#include "rgb_cmd.h"

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "color.h"
#include "common.h"
#include "module.h"
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
static bool find_xap_node(char *node, size_t node_size, char *reply, size_t size)
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
                && found++ == 0)
                snprintf(node, node_size, "%s", devs[i].node);
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

// 0-255, decimal or 0x-prefixed. Writes *out only on success.
static bool parse_u8(const char *s, uint8_t *out)
{
    char *end;
    errno = 0;
    int base = s[0] == '0' && (s[1] == 'x' || s[1] == 'X') ? 16 : 10; // "010" is ten, not eight
    unsigned long n = strtoul(s, &end, base);
    if (*s == '\0' || *end != '\0' || errno != 0 || n > 255)
        return false;
    *out = (uint8_t)n;
    return true;
}

static bool key_is(const char *setting, size_t key_len, const char *key)
{
    return strlen(key) == key_len && strncmp(setting, key, key_len) == 0;
}

// Apply one "key=value" to cfg. On a bad setting, writes the error to reply.
static bool apply_setting(struct xap_rgb *cfg, const char *setting, char *reply, size_t size)
{
    const char *eq = strchr(setting, '=');
    const char *value = eq ? eq + 1 : "";
    size_t key_len = eq ? (size_t)(eq - setting) : 0;
    uint8_t number;
    bool ok = false;

    if (key_is(setting, key_len, "effect")) {
        int id = xap_effect_id(value);
        if (id < 0 && parse_u8(value, &number) && xap_effect_name(number))
            id = number;
        if (id >= 0) {
            cfg->mode = (uint8_t)id;
            ok = true;
        }
    } else if (key_is(setting, key_len, "color")) { // "#rrggbb" or "rrggbb"; sets hue, sat and val
        const char *hex = value[0] == '#' ? value + 1 : value;
        uint8_t rgb[3];
        ok = strlen(hex) == 6 && peri_parse_hex(hex, rgb, sizeof rgb) == 3;
        if (ok)
            peri_rgb_to_hsv(rgb[0], rgb[1], rgb[2], &cfg->hue, &cfg->sat, &cfg->val);
    } else if (key_is(setting, key_len, "enable")) {
        ok = parse_u8(value, &number) && number <= 1;
        if (ok)
            cfg->enable = number;
    } else if (key_is(setting, key_len, "hue")) {
        ok = parse_u8(value, &cfg->hue);
    } else if (key_is(setting, key_len, "sat")) {
        ok = parse_u8(value, &cfg->sat);
    } else if (key_is(setting, key_len, "val")) {
        ok = parse_u8(value, &cfg->val);
    } else if (key_is(setting, key_len, "speed")) {
        ok = parse_u8(value, &cfg->speed);
    } else if (key_is(setting, key_len, "flags")) {
        ok = parse_u8(value, &cfg->flags);
    }

    if (!ok)
        fail(reply, size, "bad setting '%s' (color, effect, enable, hue, sat, val, speed, flags)",
             setting);
    return ok;
}

static void report(const struct xap_rgb *cfg, const char *extra, char *reply, size_t size)
{
    const char *effect = xap_effect_name(cfg->mode);
    snprintf(reply, size,
             "ok enabled=%s effect=%s mode=0x%02x hue=%u sat=%u val=%u speed=%u flags=0x%02x%s\n",
             cfg->enable ? "yes" : "no", effect ? effect : "unknown", cfg->mode, cfg->hue,
             cfg->sat, cfg->val, cfg->speed, cfg->flags, extra);
}

void rgb_handle(const char *args, char *reply, size_t size)
{
    char line[256];
    snprintf(line, sizeof line, "%s", args);
    char *rest;
    const char *verb = strtok_r(line, " \n", &rest);
    bool set = verb && strcmp(verb, "set") == 0;
    bool save = verb && strcmp(verb, "save") == 0;
    if (!verb || (!set && !save && strcmp(verb, "get") != 0)) {
        fail(reply, size, "usage: rgb get | set <key=value>... | save");
        return;
    }
    if (!set && strtok_r(nullptr, " \n", &rest)) { // get and save take no arguments
        fail(reply, size, "'%s' takes no arguments", verb);
        return;
    }

    char node[32];
    if (!find_xap_node(node, sizeof node, reply, size))
        return;
    int fd = xap_open(node);
    if (fd < 0) {
        fail(reply, size, "cannot open %s (see `journalctl --user -u perid`)", node);
        return;
    }

    // Read the current config first so a set only changes what it names.
    struct xap_rgb cfg;
    bool ok = xap_rgb_get(fd, &cfg) == 0;
    if (!ok)
        fail(reply, size, "could not read the lighting config from %s", node);

    if (ok && set) {
        int settings = 0;
        for (const char *s; ok && (s = strtok_r(nullptr, " \n", &rest)); settings++)
            ok = apply_setting(&cfg, s, reply, size);
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
        report(&cfg, save ? " saved=yes" : "", reply, size);
}
