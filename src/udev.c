#include "udev.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static constexpr size_t max_rules_file = 64 * 1024;

static int line_for(uint16_t vid, uint16_t pid, char *out, size_t size)
{
    return snprintf(out, size,
                    "SUBSYSTEM==\"hidraw\", ATTRS{idVendor}==\"%04x\", ATTRS{idProduct}==\"%04x\", "
                    "TAG+=\"uaccess\"",
                    vid, pid);
}

static int by_key(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

size_t peri_udev_rules(const struct peri_module *mods, size_t count, char *out, size_t size)
{
    uint32_t keys[64];
    size_t n = 0;
    for (size_t i = 0; i < count && n < 64; i++)
        keys[n++] = (uint32_t)mods[i].vid << 16 | mods[i].pid;
    qsort(keys, n, sizeof *keys, by_key);

    int len = snprintf(out, size,
                       "# Installed by `peri setup` from the installed modules. Do not edit.\n"
                       "# USB devices only: ATTRS{idVendor} does not exist for Bluetooth.\n");
    for (size_t i = 0; i < n && len > 0 && (size_t)len < size; i++) {
        if (i > 0 && keys[i] == keys[i - 1])
            continue;
        int l = line_for((uint16_t)(keys[i] >> 16), (uint16_t)keys[i], out + len, size - (size_t)len);
        if (l < 0 || (size_t)l + 1 >= size - (size_t)len)
            return 0;
        len += l;
        out[len++] = '\n';
        out[len] = '\0';
    }
    return len > 0 && (size_t)len < size ? (size_t)len : 0;
}

bool peri_udev_covers(const char *rules, const struct peri_module *m)
{
    char want[256];
    int want_len = line_for(m->vid, m->pid, want, sizeof want);
    if (want_len < 0 || (size_t)want_len >= sizeof want)
        return false;

    for (const char *line = rules; *line;) {
        size_t len = strcspn(line, "\n");
        if (len == (size_t)want_len && memcmp(line, want, len) == 0)
            return true;
        line += len + (line[len] == '\n');
    }
    return false;
}

char *peri_udev_read_installed(void)
{
    FILE *f = fopen(peri_udev_rules_path, "r");
    if (!f)
        return nullptr;
    char *buf = malloc(max_rules_file + 1);
    size_t n = buf ? fread(buf, 1, max_rules_file, f) : 0;
    fclose(f);
    if (!buf)
        return nullptr;
    buf[n] = '\0';
    return buf;
}
