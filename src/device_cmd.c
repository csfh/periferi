// Daemon side of "peri status" and "peri set": find the device an installed module drives, then
// read it or change its running brightness. The only frames sent are the ones in razer_dev.h.

#include "device_cmd.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "audio.h"
#include "controls.h"
#include "module.h"
#include "razer_dev.h"
#include "scan.h"
#include "store.h"

// What went wrong with a frame the device was sent, in the device's own words: its status if it
// answered ("refused: not supported"), else that it did not answer or could not be written to.
static void device_said(char *buf, size_t size, int rc)
{
    char tmp[16];
    if (rc == RAZER_ERR_STATUS)
        snprintf(buf, size, "the device answered \"%s\"", razer_status_name(razer_last_status(), tmp));
    else if (rc == RAZER_ERR_NO_REPLY)
        snprintf(buf, size, "the device did not answer");
    else if (rc == RAZER_ERR_IO)
        snprintf(buf, size, "the frame could not be written to the device");
    else
        snprintf(buf, size, "the device sent a reply that did not check out (%d)", rc);
}

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

static const char effect_names[] =
    "effect must be none, static, breathing, spectrum, wave or wave-reverse";

// An effect name and up to two colours (hex text, or null) as the options the frame builder takes.
// False if the name is unknown, a colour is not six hex digits, a second colour has no first, or
// the effect lacks what it needs (static needs a colour). `speed` is "slow", "fast" or null, and
// only the wave effects use it. *effect is the effect id.
static bool effect_opts(const char *name, const char *color, const char *color2, const char *speed,
                        struct razer_effect_opts *opts, int *effect)
{
    bool reverse = false;
    *effect = razer_effect_lookup(name, &reverse);
    *opts = (struct razer_effect_opts){ .reverse = reverse };
    if (*effect < 0 || (color2 && !color))
        return false;
    if (color && !parse_color(color, opts->rgb[0]))
        return false;
    if (color2 && !parse_color(color2, opts->rgb[1]))
        return false;
    opts->colors = (uint8_t)(color ? (color2 ? 2 : 1) : 0);
    opts->speed = speed && strcmp(speed, "fast") == 0 ? RAZER_WAVE_FAST : RAZER_WAVE_SLOW; // wave only
    uint8_t args[RAZER_EFFECT_ARGS_MAX];
    return razer_effect_build(0, (uint8_t)*effect, opts, args) != 0;
}

// set <module-id> brightness=<0-255>
// set <module-id> effect=<name> [color=<rrggbb>] [color2=<rrggbb>]
// One kind of change per request. The device cannot be asked what its effect or colour is, so
// the reply only says what was sent.
void set_handle(const char *args, char *reply, size_t size)
{
    static const char usage[] = "err usage: set <module-id> brightness=<0-255> | effect=<name> "
                                "[color=<rrggbb>] [color2=<rrggbb>] [speed=<slow|fast>]\n";
    char line[256], *rest; // the daemon reads at most 255 bytes: never cut a request short
    snprintf(line, sizeof line, "%s", args);
    const char *id = strtok_r(line, " \n", &rest);
    if (!id) {
        snprintf(reply, size, "%s", usage);
        return;
    }

    long brightness = -1, effect = -1;
    const char *effect_name = "";
    const char *color = nullptr, *color2 = nullptr, *speed = nullptr;
    for (const char *t; (t = strtok_r(nullptr, " \n", &rest));) {
        if (strncmp(t, "speed=", 6) == 0 && !speed) {
            if (strcmp(t + 6, "slow") != 0 && strcmp(t + 6, "fast") != 0) {
                err(reply, size, "speed must be slow or fast", id);
                return;
            }
            speed = t + 6;
        } else if (strncmp(t, "brightness=", 11) == 0 && brightness < 0) {
            char *end;
            unsigned long v = strtoul(t + 11, &end, 10);
            if (t[11] == '\0' || *end != '\0' || v > 255) {
                err(reply, size, "brightness must be a whole number from 0 to 255", id);
                return;
            }
            brightness = (long)v;
        } else if (strncmp(t, "effect=", 7) == 0 && effect < 0) {
            effect = razer_effect_id(t + 7);
            effect_name = t + 7;
            if (effect < 0) {
                err(reply, size, effect_names, id);
                return;
            }
        } else if (strncmp(t, "color=", 6) == 0 && !color) {
            uint8_t check[3];
            if (!parse_color(t + 6, check)) {
                err(reply, size, "color must be six hex digits, like 0000ff", id);
                return;
            }
            color = t + 6;
        } else if (strncmp(t, "color2=", 7) == 0 && !color2) {
            uint8_t check[3];
            if (!parse_color(t + 7, check)) {
                err(reply, size, "color2 must be six hex digits, like ff0000", id);
                return;
            }
            color2 = t + 7;
        } else {
            snprintf(reply, size, "%s", usage);
            return;
        }
    }
    bool bright_set = brightness >= 0, effect_set = effect >= 0;
    if (bright_set == effect_set || (bright_set && (color || color2 || speed))
        || ((color || color2 || speed) && !effect_set)) {
        snprintf(reply, size, "%s", usage);
        return;
    }
    struct razer_effect_opts opts;
    int effect_id_checked = -1;
    if (effect_set && !effect_opts(effect_name, color, color2, speed, &opts, &effect_id_checked)) {
        err(reply, size, color2 && !color ? "a second colour needs a first"
                                          : "this effect needs a valid color=<rrggbb>", id);
        return;
    }

    struct peri_module m;
    int fd = open_module_device(id, &m, reply, size);
    if (fd < 0)
        return;

    if (effect_set) {
        int rc = razer_set_effect_opts(fd, RAZER_STORE_NONE, (uint8_t)effect_id_checked, &opts);
        close(fd);
        if (rc != 0) {
            char why[96], text[160];
            device_said(why, sizeof why, rc);
            snprintf(text, sizeof text, "the effect was not accepted: %s", why);
            err(reply, size, text, id);
            return;
        }
        snprintf(reply, size,
                 "ok module=%s sent=%s color=%s device=ok storage=none readback=unavailable\n",
                 m.id, effect_name, color ? (color2 ? "two" : "yes") : "no");
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

// The installed razer module with this id (looked up in the installed directory only), without
// touching the device. Returns false with an "err ..." reply.
static bool installed_razer_module(const char *id, struct peri_module *out, char *reply, size_t size)
{
    char dir[PATH_MAX];
    struct peri_module installed[32];
    size_t count = peri_module_id_valid(id) && peri_installed_dir(dir, sizeof dir)
                       ? peri_modules_scan(dir, installed, 32) : 0;
    for (size_t i = 0; i < count; i++)
        if (strcmp(installed[i].id, id) == 0) {
            if (strcmp(installed[i].engine, "razer") != 0) {
                err(reply, size, "this module has no looks", id);
                return false;
            }
            *out = installed[i];
            return true;
        }
    err(reply, size, "no such installed module; run `peri use <id>` (see `peri modules`)", id);
    return false;
}

static void reply_look(char *reply, size_t size, const char *id, enum peri_look_which which,
                       const struct peri_look *l)
{
    char brightness[8] = "unset";
    if (l->brightness >= 0)
        snprintf(brightness, sizeof brightness, "%d", l->brightness);
    snprintf(reply, size, "ok module=%s look=%s effect=%s color=%s color2=%s speed=%s brightness=%s\n", id,
             peri_look_which_name(which), l->effect[0] ? l->effect : "unset",
             l->color[0] ? l->color : "none", l->color2[0] ? l->color2 : "none",
             l->speed[0] ? l->speed : "default", brightness);
}

void look_handle(const char *args, char *reply, size_t size)
{
    static const char usage[] = "err usage: look <module-id> get|save|apply <unmuted|muted> "
                                "[effect=<name>] [color=<rrggbb>] [color2=<rrggbb>] [speed=<slow|fast>] "
                                "[brightness=<0-255>]\n";
    char line[256], *rest; // as above
    snprintf(line, sizeof line, "%s", args);
    const char *id = strtok_r(line, " \n", &rest);
    const char *verb = strtok_r(nullptr, " \n", &rest);
    const char *which_name = strtok_r(nullptr, " \n", &rest);
    enum peri_look_which which;
    if (!id || !verb || !peri_look_which_parse(which_name, &which)) {
        snprintf(reply, size, "%s", usage);
        return;
    }
    bool get = strcmp(verb, "get") == 0, save = strcmp(verb, "save") == 0,
         apply = strcmp(verb, "apply") == 0;
    if (!get && !save && !apply) {
        snprintf(reply, size, "%s", usage);
        return;
    }

    // The settings of a save: the whole look, so anything left out is "not set". get and apply
    // take none.
    struct peri_look want = PERI_LOOK_EMPTY;
    for (const char *t; (t = strtok_r(nullptr, " \n", &rest));) {
        if (!save) {
            snprintf(reply, size, "%s", usage);
            return;
        }
        if (strncmp(t, "effect=", 7) == 0 && !want.effect[0]) {
            if (razer_effect_id(t + 7) < 0 || strlen(t + 7) >= sizeof want.effect) {
                err(reply, size, effect_names, id);
                return;
            }
            snprintf(want.effect, sizeof want.effect, "%s", t + 7);
        } else if (strncmp(t, "color=", 6) == 0 && !want.color[0]) {
            uint8_t rgb[3];
            if (!parse_color(t + 6, rgb)) {
                err(reply, size, "color must be six hex digits, like 0000ff", id);
                return;
            }
            snprintf(want.color, sizeof want.color, "%s", t + 6);
        } else if (strncmp(t, "speed=", 6) == 0 && !want.speed[0]) {
            if (strcmp(t + 6, "slow") != 0 && strcmp(t + 6, "fast") != 0) {
                err(reply, size, "speed must be slow or fast", id);
                return;
            }
            snprintf(want.speed, sizeof want.speed, "%s", t + 6);
        } else if (strncmp(t, "color2=", 7) == 0 && !want.color2[0]) {
            uint8_t rgb[3];
            if (!parse_color(t + 7, rgb)) {
                err(reply, size, "color2 must be six hex digits, like ff0000", id);
                return;
            }
            snprintf(want.color2, sizeof want.color2, "%s", t + 7);
        } else if (strncmp(t, "brightness=", 11) == 0 && want.brightness < 0) {
            char *end;
            unsigned long v = strtoul(t + 11, &end, 10);
            if (t[11] == '\0' || *end != '\0' || v > 255) {
                err(reply, size, "brightness must be a whole number from 0 to 255", id);
                return;
            }
            want.brightness = (int)v;
        } else {
            snprintf(reply, size, "%s", usage);
            return;
        }
    }

    struct peri_module m;
    if (!installed_razer_module(id, &m, reply, size))
        return;

    if (get) {
        struct peri_look saved = peri_look_load(id, which);
        reply_look(reply, size, id, which, &saved);
        return;
    }

    if (save) {
        if (want.effect[0] && strcmp(want.effect, "static") == 0 && !want.color[0]) {
            err(reply, size, "the static effect needs a color=<rrggbb>", id);
            return;
        }
        if (want.color2[0] && !want.color[0]) {
            err(reply, size, "a second colour needs a first", id);
            return;
        }
        if (want.brightness >= 0 && (m.max_brightness == 0 || want.brightness > m.max_brightness)) {
            err(reply, size, "brightness is above the module's limit", id);
            return;
        }
        if (!peri_look_save(id, which, &want)) {
            err(reply, size, "could not save the look", id);
            return;
        }
        struct peri_look saved = peri_look_load(id, which);
        reply_look(reply, size, id, which, &saved);
        return;
    }

    // apply: the only path here that writes to the device. The saved look is checked again, so
    // a hand-edited file can never send what `set` would refuse.
    struct peri_look l = peri_look_load(id, which);
    if (!l.effect[0] && l.brightness < 0) {
        err(reply, size, "nothing is saved for this look", id);
        return;
    }
    if (l.brightness >= 0 && (m.max_brightness == 0 || l.brightness > m.max_brightness)) {
        err(reply, size, "the saved brightness is above the module's limit", id);
        return;
    }
    struct razer_effect_opts opts;
    int effect_checked = -1;
    if (l.effect[0]
        && !effect_opts(l.effect, l.color[0] ? l.color : nullptr, l.color2[0] ? l.color2 : nullptr,
                        l.speed[0] ? l.speed : nullptr, &opts, &effect_checked)) {
        err(reply, size, "the saved effect is not valid", id);
        return;
    }

    struct peri_module dev;
    int fd = open_module_device(id, &dev, reply, size);
    if (fd < 0)
        return;
    int effect_rc = l.effect[0]
        ? razer_set_effect_opts(fd, RAZER_STORE_NONE, (uint8_t)effect_checked, &opts) : 0;
    if (effect_rc != 0) {
        close(fd);
        char why[96], text[200];
        device_said(why, sizeof why, effect_rc);
        snprintf(text, sizeof text, "the effect was not accepted (%s); nothing else was sent", why);
        err(reply, size, text, id);
        return;
    }
    uint8_t now = 0;
    if (l.brightness >= 0
        && (razer_set_brightness(fd, RAZER_STORE_NONE, (uint8_t)l.brightness) != 0
            || razer_brightness(fd, RAZER_STORE_NONE, &now) != 0)) {
        close(fd);
        err(reply, size, l.effect[0] ? "the effect was applied but the device did not accept the brightness"
                                     : "the device did not accept the brightness", id);
        return;
    }
    close(fd);
    char shown[8] = "-";
    if (l.brightness >= 0)
        snprintf(shown, sizeof shown, "%u", now);
    snprintf(reply, size, "ok module=%s applied=%s effect=%s brightness=%s\n", id,
             peri_look_which_name(which), l.effect[0] ? l.effect : "-", shown);
}

static void reply_audio(char *reply, size_t size, const char *id, int card)
{
    static const struct { const char *key; enum peri_audio_ctl ctl; bool yes_no; } keys[] = {
        { "gain", PERI_AUDIO_GAIN, false }, { "monitor", PERI_AUDIO_MONITOR, false },
        { "monitor_on", PERI_AUDIO_MONITOR_ON, true }, { "headphones", PERI_AUDIO_HEADPHONES, false },
    };
    int n = snprintf(reply, size, "ok module=%s", id);
    for (size_t i = 0; i < sizeof keys / sizeof *keys && n > 0 && (size_t)n < size; i++) {
        int v;
        char shown[8] = "unknown";
        if (peri_audio_get(card, keys[i].ctl, &v)) {
            if (keys[i].yes_no)
                snprintf(shown, sizeof shown, "%s", v ? "yes" : "no");
            else
                snprintf(shown, sizeof shown, "%d", v);
        }
        n += snprintf(reply + n, size - (size_t)n, " %s=%s", keys[i].key, shown);
    }
    if (n > 0 && (size_t)n + 2 <= size)
        memcpy(reply + n, "\n", 2);
}

void audio_handle(const char *args, char *reply, size_t size)
{
    static const char usage[] = "err usage: audio <module-id> get | set gain=<0-100> monitor=<0-100> "
                                "monitor_on=<yes|no> headphones=<0-100>\n";
    char line[256], *rest; // as above
    snprintf(line, sizeof line, "%s", args);
    const char *id = strtok_r(line, " \n", &rest);
    const char *verb = strtok_r(nullptr, " \n", &rest);
    if (!id || !verb || (strcmp(verb, "get") != 0 && strcmp(verb, "set") != 0)) {
        snprintf(reply, size, "%s", usage);
        return;
    }
    bool set = strcmp(verb, "set") == 0;

    // Parse every setting before touching the sound card, so a bad one changes nothing.
    struct { enum peri_audio_ctl ctl; int value; } changes[4];
    size_t count = 0;
    for (const char *t; (t = strtok_r(nullptr, " \n", &rest));) {
        if (!set || count == 4) {
            snprintf(reply, size, "%s", usage);
            return;
        }
        const char *eq = strchr(t, '=');
        size_t klen = eq ? (size_t)(eq - t) : 0;
        if (!eq) {
            snprintf(reply, size, "%s", usage);
            return;
        }
        enum peri_audio_ctl ctl;
        bool level = true;
        if (klen == 4 && strncmp(t, "gain", 4) == 0) ctl = PERI_AUDIO_GAIN;
        else if (klen == 7 && strncmp(t, "monitor", 7) == 0) ctl = PERI_AUDIO_MONITOR;
        else if (klen == 10 && strncmp(t, "monitor_on", 10) == 0) { ctl = PERI_AUDIO_MONITOR_ON; level = false; }
        else if (klen == 10 && strncmp(t, "headphones", 10) == 0) ctl = PERI_AUDIO_HEADPHONES;
        else {
            snprintf(reply, size, "%s", usage);
            return;
        }
        for (size_t i = 0; i < count; i++)
            if (changes[i].ctl == ctl) { // a key given twice is a typo, not a request
                snprintf(reply, size, "%s", usage);
                return;
            }
        int value;
        if (level) {
            char *end;
            unsigned long v = strtoul(eq + 1, &end, 10);
            if (eq[1] == '\0' || *end != '\0' || v > 100) {
                err(reply, size, "levels are whole numbers from 0 to 100", id);
                return;
            }
            value = (int)v;
        } else if (strcmp(eq + 1, "yes") == 0 || strcmp(eq + 1, "no") == 0) {
            value = strcmp(eq + 1, "yes") == 0;
        } else {
            err(reply, size, "monitor_on must be yes or no", id);
            return;
        }
        changes[count].ctl = ctl;
        changes[count++].value = value;
    }
    if (set && count == 0) {
        snprintf(reply, size, "%s", usage);
        return;
    }

    struct peri_module m;
    if (!installed_razer_module(id, &m, reply, size))
        return;
    int card = peri_audio_card(m.vid, m.pid);
    if (card < 0) {
        err(reply, size, "the sound card is not connected", id);
        return;
    }
    for (size_t i = 0; i < count; i++)
        if (!peri_audio_set(card, changes[i].ctl, changes[i].value)) {
            err(reply, size, "the sound card did not accept the change", id);
            return;
        }
    reply_audio(reply, size, id, card);
}
