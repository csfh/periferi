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

bool peri_look_dir(char *buf, size_t size)
{
    const char *cfg = getenv("XDG_CONFIG_HOME"), *home = getenv("HOME");
    int n;
    if (cfg && *cfg)
        n = snprintf(buf, size, "%s/peri/state", cfg);
    else if (home && *home)
        n = snprintf(buf, size, "%s/.config/peri/state", home);
    else
        return false;
    return n > 0 && (size_t)n < size;
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

bool peri_look_save(const char *module_id, const char *effect, const char *color)
{
    if (!module_id || !peri_module_id_valid(module_id) || !effect || razer_effect_id(effect) < 0)
        return false;
    char hex[7] = "";
    bool is_static = strcmp(effect, "static") == 0;
    if (is_static && !lower_hex(color, hex))
        return false;

    char dir[PATH_MAX], path[PATH_MAX], tmp[PATH_MAX];
    if (!peri_look_dir(dir, sizeof dir)
        || snprintf(path, sizeof path, "%s/%s.toml", dir, module_id) >= (int)sizeof path
        || snprintf(tmp, sizeof tmp, "%s/.%s.toml.tmp", dir, module_id) >= (int)sizeof tmp
        || !peri_mkdir_p(dir))
        return false;

    char text[128];
    int len = is_static ? snprintf(text, sizeof text, "effect = \"%s\"\ncolor = \"%s\"\n", effect, hex)
                        : snprintf(text, sizeof text, "effect = \"%s\"\n", effect);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0644);
    if (fd < 0)
        return false;
    bool wrote = peri_write_all(fd, text, (size_t)len);
    bool closed = close(fd) == 0; // always close, even after a failed write
    if (!wrote || !closed || rename(tmp, path) < 0) {
        unlink(tmp);
        return false;
    }
    return true;
}

struct peri_look peri_look_load(const char *module_id)
{
    struct peri_look none = {};
    char dir[PATH_MAX], path[PATH_MAX];
    if (!module_id || !peri_module_id_valid(module_id) || !peri_look_dir(dir, sizeof dir)
        || snprintf(path, sizeof path, "%s/%s.toml", dir, module_id) >= (int)sizeof path)
        return none;

    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0)
        return none;
    char text[1024];
    struct stat st;
    ssize_t n = fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_size < (off_t)sizeof text
                    ? read(fd, text, sizeof text - 1) : -1;
    close(fd);
    if (n <= 0)
        return none;

    toml_result_t result = toml_parse(text, (int)n);
    struct peri_look look = {};
    if (result.ok) {
        toml_datum_t effect = toml_seek(result.toptab, "effect");
        toml_datum_t color = toml_seek(result.toptab, "color");
        bool known = effect.type == TOML_STRING && effect.u.s && razer_effect_id(effect.u.s) >= 0
                     && strlen(effect.u.s) < sizeof look.effect;
        if (known) {
            bool is_static = strcmp(effect.u.s, "static") == 0;
            char hex[7] = "";
            // A static look needs a valid colour; every other effect has none, whatever the file says.
            if (!is_static || (color.type == TOML_STRING && lower_hex(color.u.s, hex))) {
                snprintf(look.effect, sizeof look.effect, "%s", effect.u.s);
                memcpy(look.color, hex, sizeof hex);
            }
        }
    }
    toml_free(result);
    return look;
}
