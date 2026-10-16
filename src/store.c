#include "store.h"

#include <ctype.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "fsutil.h"
#include "module.h"
#include "razer.h"
#include "tomlc17.h"
#include "xdg.h"

bool peri_look_dir(char *buf, size_t size)
{
    return peri_xdg_path(buf, size, PERI_XDG_CONFIG, "peri/state");
}

static const char *const which_names[] = { "unmuted", "muted" };

bool peri_look_which_parse(const char *name, enum peri_look_which *which)
{
    for (int i = 0; i < 2; i++)
        if (name && strcmp(name, which_names[i]) == 0) {
            *which = (enum peri_look_which)i;
            return true;
        }
    return false;
}

const char *peri_look_which_name(enum peri_look_which which)
{
    return which_names[which == PERI_LOOK_MUTED ? 1 : 0];
}

// Six hex digits, copied lowercased into out (7 bytes).
static bool lower_hex(const char *s, char out[7])
{
    if (!s || strlen(s) != 6)
        return false;
    for (int i = 0; i < 6; i++) {
        if (!isxdigit((unsigned char)s[i]))
            return false;
        out[i] = (char)tolower((unsigned char)s[i]);
    }
    out[6] = '\0';
    return true;
}

// The rules for a look, shared by save and load: a known effect (or none), a colour exactly when
// the effect is "static", a brightness of 0-255 (or none). On success *out is the clean look.
static bool clean(const struct peri_look *in, struct peri_look *out)
{
    *out = PERI_LOOK_EMPTY;
    if (in->effect[0]) {
        int effect = razer_effect_id(in->effect);
        if (effect < 0 || strlen(in->effect) >= sizeof out->effect)
            return false;
        snprintf(out->effect, sizeof out->effect, "%s", in->effect);
        bool takes_colors = effect == RAZER_EFFECT_STATIC || effect == RAZER_EFFECT_BREATHING;
        if (effect == RAZER_EFFECT_STATIC && !lower_hex(in->color, out->color))
            return false;
        // Breathing may have a colour; a bad one spoils the look, a missing one means random.
        if (takes_colors && effect != RAZER_EFFECT_STATIC && in->color[0] && !lower_hex(in->color, out->color))
            return false;
        // A second colour needs a first, and only breathing takes two.
        if (out->color[0] && effect != RAZER_EFFECT_STATIC && in->color2[0]
            && !lower_hex(in->color2, out->color2))
            return false;
    }
    // A speed belongs to the wave effects only; anything else drops it, a bad one spoils the look.
    if (in->speed[0]) {
        if (strcmp(in->speed, "slow") != 0 && strcmp(in->speed, "fast") != 0)
            return false;
        if (out->effect[0] && razer_effect_id(out->effect) == RAZER_EFFECT_WAVE)
            snprintf(out->speed, sizeof out->speed, "%s", in->speed);
    }
    if (in->brightness < -1 || in->brightness > 255)
        return false;
    out->brightness = in->brightness;
    return true;
}

static const char look_effect_problem[] =
    "effect must be none, static, breathing, spectrum, wave or wave-reverse";

bool peri_look_take(const char *token, struct peri_look_seen *seen, struct peri_look_field *field)
{
    if (!token || !seen || !field)
        return false;
    // The "=" right after the key name is what tells color2 from color.
    struct {
        const char *key;
        size_t eq;
        bool *slot;
    } keys[] = {
        { "color2", 7, &seen->color2 },
        { "effect", 7, &seen->effect },
        { "color", 6, &seen->color },
        { "speed", 6, &seen->speed },
        { "brightness", 11, &seen->brightness },
    };
    for (size_t i = 0; i < sizeof keys / sizeof *keys; i++) {
        if (strncmp(token, keys[i].key, keys[i].eq - 1) != 0 || token[keys[i].eq - 1] != '=')
            continue;
        if (*keys[i].slot)
            return false;
        *keys[i].slot = true;
        field->key = keys[i].key;
        field->value = token + keys[i].eq;
        return true;
    }
    return false;
}

// The first field that is not a single look setting. Order is the user's, so
// the reason names the setting they wrote first.
const char *peri_look_read(const struct peri_look_field *fields, size_t count, struct peri_look *raw)
{
    if (count > 0 && !fields)
        return "not a look setting";
    struct peri_look built = PERI_LOOK_EMPTY;
    for (size_t i = 0; i < count; i++) {
        const char *key = fields[i].key;
        const char *value = fields[i].value ? fields[i].value : "";
        if (!key)
            return "not a look setting";
        if (strcmp(key, "effect") == 0) {
            if (razer_effect_id(value) < 0 || strlen(value) >= sizeof built.effect)
                return look_effect_problem;
            snprintf(built.effect, sizeof built.effect, "%s", value);
        } else if (strcmp(key, "color") == 0) {
            if (!lower_hex(value, built.color))
                return "color must be six hex digits, like 0000ff";
        } else if (strcmp(key, "color2") == 0) {
            if (!lower_hex(value, built.color2))
                return "color2 must be six hex digits, like ff0000";
        } else if (strcmp(key, "speed") == 0) {
            if (strcmp(value, "slow") != 0 && strcmp(value, "fast") != 0)
                return "speed must be slow or fast";
            snprintf(built.speed, sizeof built.speed, "%s", value);
        } else if (strcmp(key, "brightness") == 0) {
            char *end = nullptr;
            unsigned long v = strtoul(value, &end, 10);
            // A digit first: strtoul alone would take "+7" and "-0".
            if (!isdigit((unsigned char)value[0]) || !end || *end != '\0' || v > 255)
                return "brightness must be a whole number from 0 to 255";
            built.brightness = (int)v;
        } else
            return "not a look setting";
    }
    if (raw)
        *raw = built;
    return nullptr;
}

const char *peri_look_stored(const struct peri_look *raw, struct peri_look *out)
{
    if (!raw)
        return "not a look setting";
    // Asked for, so a request must say so. A file may still drop these: a hand
    // edit is not a request, and load keeps the rest of that look.
    if (raw->effect[0] && strcmp(raw->effect, "static") == 0 && !raw->color[0])
        return "the static effect needs a color=<rrggbb>";
    if (raw->color2[0] && !raw->color[0])
        return "a second colour needs a first";
    struct peri_look stored;
    if (!clean(raw, &stored))
        return "these settings do not make a look that can be saved";
    if (out)
        *out = stored;
    return nullptr;
}

static bool look_path(const char *module_id, char *dir, size_t dir_size, char *path, size_t size)
{
    return module_id && peri_module_id_valid(module_id) && peri_look_dir(dir, dir_size)
           && snprintf(path, size, "%s/%s.toml", dir, module_id) < (int)size;
}

// Read both looks, and the follow flag if `follow` is not null, from the file; anything missing
// or invalid is an empty look, and the flag is on unless the file says `follow_mute = false`.
static void load_both(const char *module_id, struct peri_look looks[2], bool *follow)
{
    looks[0] = looks[1] = PERI_LOOK_EMPTY;
    if (follow)
        *follow = true;
    char dir[PATH_MAX], path[PATH_MAX];
    if (!look_path(module_id, dir, sizeof dir, path, sizeof path))
        return;

    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0)
        return;
    char text[2048];
    struct stat st;
    ssize_t n = fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_size < (off_t)sizeof text
                    ? read(fd, text, sizeof text - 1) : -1;
    close(fd);
    if (n <= 0)
        return;

    toml_result_t result = toml_parse(text, (int)n);
    if (result.ok) {
        toml_datum_t flag = toml_seek(result.toptab, "follow_mute");
        if (follow && flag.type == TOML_BOOLEAN && !flag.u.boolean)
            *follow = false;
        for (int i = 0; i < 2; i++) {
            char key[48];
            struct peri_look raw = PERI_LOOK_EMPTY;

            snprintf(key, sizeof key, "%s.effect", which_names[i]);
            toml_datum_t effect = toml_seek(result.toptab, key);
            snprintf(key, sizeof key, "%s.color", which_names[i]);
            toml_datum_t color = toml_seek(result.toptab, key);
            snprintf(key, sizeof key, "%s.color2", which_names[i]);
            toml_datum_t color2 = toml_seek(result.toptab, key);
            snprintf(key, sizeof key, "%s.speed", which_names[i]);
            toml_datum_t speed = toml_seek(result.toptab, key);
            snprintf(key, sizeof key, "%s.brightness", which_names[i]);
            toml_datum_t brightness = toml_seek(result.toptab, key);

            if (effect.type == TOML_STRING && effect.u.s && strlen(effect.u.s) < sizeof raw.effect)
                snprintf(raw.effect, sizeof raw.effect, "%s", effect.u.s);
            if (color.type == TOML_STRING && color.u.s && strlen(color.u.s) == 6)
                snprintf(raw.color, sizeof raw.color, "%s", color.u.s);
            if (color2.type == TOML_STRING && color2.u.s && strlen(color2.u.s) == 6)
                snprintf(raw.color2, sizeof raw.color2, "%s", color2.u.s);
            if (speed.type == TOML_STRING && speed.u.s && strlen(speed.u.s) < sizeof raw.speed)
                snprintf(raw.speed, sizeof raw.speed, "%s", speed.u.s);
            if (brightness.type == TOML_INT64 && brightness.u.int64 >= 0 && brightness.u.int64 <= 255)
                raw.brightness = (int)brightness.u.int64;

            // A bad field spoils only itself: an invalid effect or a static one without a colour
            // is dropped, the brightness is kept. A colour on another effect is ignored.
            struct peri_look good;
            if (!clean(&raw, &good)) {
                raw.effect[0] = '\0';
                raw.color[0] = '\0';
                raw.color2[0] = '\0';
                raw.speed[0] = '\0';
                clean(&raw, &good);
            }
            looks[i] = good;
        }
    }
    toml_free(result);
}

struct peri_look peri_look_load(const char *module_id, enum peri_look_which which)
{
    struct peri_look looks[2];
    load_both(module_id, looks, nullptr);
    return looks[which == PERI_LOOK_MUTED ? 1 : 0];
}

static int append_look(char *buf, size_t size, enum peri_look_which which, const struct peri_look *l)
{
    if (!l->effect[0] && l->brightness < 0)
        return 0; // an empty look is not written
    int n = snprintf(buf, size, "[%s]\n", peri_look_which_name(which));
    if (l->effect[0])
        n += snprintf(buf + n, size - (size_t)n, "effect = \"%s\"\n", l->effect);
    if (l->color[0])
        n += snprintf(buf + n, size - (size_t)n, "color = \"%s\"\n", l->color);
    if (l->color2[0])
        n += snprintf(buf + n, size - (size_t)n, "color2 = \"%s\"\n", l->color2);
    if (l->speed[0])
        n += snprintf(buf + n, size - (size_t)n, "speed = \"%s\"\n", l->speed);
    if (l->brightness >= 0)
        n += snprintf(buf + n, size - (size_t)n, "brightness = %d\n", l->brightness);
    return n + snprintf(buf + n, size - (size_t)n, "\n");
}

// Write both looks and the flag as one file, atomically.
static bool write_state(const char *module_id, const struct peri_look looks[2], bool follow)
{
    char dir[PATH_MAX], path[PATH_MAX];
    if (!look_path(module_id, dir, sizeof dir, path, sizeof path))
        return false;

    char text[768];
    int len = 0;
    if (!follow) // the flag is written only when it is off: the default is on
        len = snprintf(text, sizeof text, "follow_mute = false\n\n");
    len += append_look(text + len, sizeof text - (size_t)len, PERI_LOOK_UNMUTED, &looks[0]);
    len += append_look(text + len, sizeof text - (size_t)len, PERI_LOOK_MUTED, &looks[1]);
    if (len >= (int)sizeof text || !peri_mkdir_p(dir))
        return false;

    return peri_write_file_atomic(path, text, (size_t)len);
}

bool peri_look_save(const char *module_id, enum peri_look_which which, const struct peri_look *look)
{
    struct peri_look mine;
    char dir[PATH_MAX], path[PATH_MAX];
    if (!look || !look_path(module_id, dir, sizeof dir, path, sizeof path) || !clean(look, &mine))
        return false;

    struct peri_look looks[2];
    bool follow;
    load_both(module_id, looks, &follow); // keep the other look and the flag as they are
    looks[which == PERI_LOOK_MUTED ? 1 : 0] = mine;
    return write_state(module_id, looks, follow);
}

bool peri_look_follow(const char *module_id)
{
    struct peri_look looks[2];
    bool follow;
    load_both(module_id, looks, &follow);
    return follow;
}

bool peri_look_set_follow(const char *module_id, bool on)
{
    char dir[PATH_MAX], path[PATH_MAX];
    if (!look_path(module_id, dir, sizeof dir, path, sizeof path))
        return false;
    struct peri_look looks[2];
    bool follow;
    load_both(module_id, looks, &follow);
    return write_state(module_id, looks, on);
}
