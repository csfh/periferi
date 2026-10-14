#include "rgb_settings.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "color.h"
#include "common.h"

[[gnu::format(printf, 3, 4)]]
static bool fail(char *err, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, size, fmt, ap);
    va_end(ap);
    return false;
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

static bool no_limit(const char *module_id, char *err, size_t size)
{
    return fail(err, size,
                "this keyboard's module sets no brightness limit; update it with `peri use %s`",
                module_id);
}

bool rgb_apply_setting(struct xap_rgb *cfg, const char *setting, uint8_t max_val,
                       const char *module_id, char *err, size_t err_size)
{
    const char *eq = strchr(setting, '=');
    const char *value = eq ? eq + 1 : "";
    size_t key_len = eq ? (size_t)(eq - setting) : 0;
    struct xap_rgb next = *cfg;
    uint8_t number;
    bool ok = false;

    if (key_is(setting, key_len, "effect")) {
        int id = xap_effect_id(value);
        if (id < 0 && parse_u8(value, &number) && xap_effect_name(number))
            id = number;
        if (id >= 0) {
            next.mode = (uint8_t)id;
            ok = true;
        }
    } else if (key_is(setting, key_len, "color")) { // "#rrggbb" or "rrggbb": hue, sat and val
        if (max_val == 0)
            return no_limit(module_id, err, err_size);
        const char *hex = value[0] == '#' ? value + 1 : value;
        uint8_t rgb[3];
        ok = strlen(hex) == 6 && peri_parse_hex(hex, rgb, sizeof rgb) == 3;
        if (ok) {
            peri_rgb_to_hsv(rgb[0], rgb[1], rgb[2], &next.hue, &next.sat, &next.val);
            if (next.val > max_val)
                next.val = max_val;
        }
    } else if (key_is(setting, key_len, "enable")) {
        ok = parse_u8(value, &number) && number <= 1;
        if (ok)
            next.enable = number;
    } else if (key_is(setting, key_len, "hue")) {
        ok = parse_u8(value, &next.hue);
    } else if (key_is(setting, key_len, "sat")) {
        ok = parse_u8(value, &next.sat);
    } else if (key_is(setting, key_len, "val")) {
        if (max_val == 0)
            return no_limit(module_id, err, err_size);
        ok = parse_u8(value, &next.val);
        if (ok && next.val > max_val)
            return fail(err, err_size, "brightness %u is above this keyboard's limit of %u",
                        next.val, max_val);
    } else if (key_is(setting, key_len, "speed")) {
        ok = parse_u8(value, &next.speed);
    } else if (key_is(setting, key_len, "flags")) {
        ok = parse_u8(value, &next.flags);
    }

    if (!ok)
        return fail(err, err_size, "bad setting '%s' (color, effect, enable, hue, sat, val, speed, flags)",
                    setting);
    *cfg = next;
    return true;
}

bool rgb_effects_csv(uint64_t mask, char *out, size_t size)
{
    size_t len = 0;
    if (size == 0)
        return false;
    out[0] = '\0';
    for (unsigned id = 0; id < 64; id++) {
        const char *name = (mask >> id & 1) ? xap_effect_name((uint8_t)id) : nullptr;
        if (!name)
            continue;
        int n = snprintf(out + len, size - len, "%s%s", len ? "," : "", name);
        if (n < 0 || (size_t)n >= size - len) {
            out[0] = '\0';
            return false;
        }
        len += (size_t)n;
    }
    return true;
}
