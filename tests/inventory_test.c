#include <stdio.h>
#include <string.h>

#include "../src/inventory.h"

static int failures;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
            failures++;                                                    \
        }                                                                  \
    } while (0)

static struct peri_device iface(const char *node, const char *group, uint16_t vid, uint16_t pid,
                                uint16_t page, uint16_t usage, const char *name)
{
    struct peri_device d = { .vid = vid, .pid = pid, .usage_page = page, .usage = usage };
    snprintf(d.node, sizeof d.node, "%s", node);
    snprintf(d.group, sizeof d.group, "%s", group);
    snprintf(d.name, sizeof d.name, "%s", name);
    return d;
}

static struct peri_module module(const char *id, uint16_t vid, uint16_t pid, uint16_t page,
                                 uint16_t usage)
{
    struct peri_module m = { .vid = vid, .pid = pid, .has_usage = true,
                             .usage_page = page, .usage = usage };
    snprintf(m.id, sizeof m.id, "%s", id);
    return m;
}

int main(void)
{
    // The CTRL's shape: three interfaces, and the one the module matches is the LAST.
    struct peri_device devs[] = {
        iface("hidraw5", "kbd", 0x359b, 9, 0x01, 0x06, "Drop CTRL v2"),
        iface("hidraw6", "kbd", 0x359b, 9, 0x01, 0x02, "Drop CTRL v2"),
        iface("hidraw7", "kbd", 0x359b, 9, 0xff51, 0x58, "Drop CTRL v2"),
        iface("hidraw1", "mic", 0x1532, 0x56f, 0xff53, 0x04, "Seiren"),
        iface("hidraw8", "key", 0x1050, 0x406, 0xf1d0, 0x01, "YubiKey"),
    };
    devs[4].fido = true;
    peri_classify(devs, 5);

    struct peri_module ctrl = module("drop-ctrl-v2", 0x359b, 9, 0xff51, 0x58);
    struct peri_module key_mod = module("claims-the-key", 0x1050, 0x406, 0xf1d0, 0x01);
    struct peri_item items[8];

    // Installed module matching a non-first interface: the whole device is installed.
    size_t n = peri_inventory(devs, 5, &ctrl, 1, &ctrl, 1, items, 8);
    CHECK(n == 3); // 5 interfaces, 3 physical devices, in scan order
    CHECK(strcmp(items[0].group, "kbd") == 0 && strcmp(items[1].group, "mic") == 0
          && strcmp(items[2].group, "key") == 0);
    CHECK(items[0].status == PERI_STATUS_INSTALLED && strcmp(items[0].module, "drop-ctrl-v2") == 0);
    CHECK(items[0].vid == 0x359b && items[0].pid == 9 && strcmp(items[0].name, "Drop CTRL v2") == 0);
    CHECK(items[1].status == PERI_STATUS_NONE && items[1].module[0] == '\0');

    // Only in the catalog: available. Installed but not in the catalog: still installed.
    n = peri_inventory(devs, 5, nullptr, 0, &ctrl, 1, items, 8);
    CHECK(n == 3 && items[0].status == PERI_STATUS_AVAILABLE
          && strcmp(items[0].module, "drop-ctrl-v2") == 0);
    n = peri_inventory(devs, 5, &ctrl, 1, nullptr, 0, items, 8);
    CHECK(n == 3 && items[0].status == PERI_STATUS_INSTALLED);

    // Installed beats available, even when the catalog has a different module for it.
    struct peri_module other = module("other-ctrl", 0x359b, 9, 0xff51, 0x58);
    n = peri_inventory(devs, 5, &ctrl, 1, &other, 1, items, 8);
    CHECK(strcmp(items[0].module, "drop-ctrl-v2") == 0 && items[0].status == PERI_STATUS_INSTALLED);

    // Nothing at all: every device is none.
    n = peri_inventory(devs, 5, nullptr, 0, nullptr, 0, items, 8);
    CHECK(n == 3 && items[0].status == PERI_STATUS_NONE && items[1].status == PERI_STATUS_NONE);

    // A hidden device stays in the output with its reason, and no module ever gets it.
    n = peri_inventory(devs, 5, &key_mod, 1, &key_mod, 1, items, 8);
    CHECK(n == 3 && items[2].hidden == PERI_HIDDEN_FIDO && items[2].status == PERI_STATUS_NONE
          && items[2].module[0] == '\0');

    // The output cap holds, and interfaces of items already listed still fold in.
    n = peri_inventory(devs, 5, &ctrl, 1, &ctrl, 1, items, 2);
    CHECK(n == 2 && items[0].status == PERI_STATUS_INSTALLED);
    CHECK(peri_inventory(devs, 0, &ctrl, 1, &ctrl, 1, items, 8) == 0);

    CHECK(strcmp(peri_status_name(PERI_STATUS_NONE), "none") == 0);
    CHECK(strcmp(peri_status_name(PERI_STATUS_AVAILABLE), "available") == 0);
    CHECK(strcmp(peri_status_name(PERI_STATUS_INSTALLED), "installed") == 0);
    return failures ? 1 : 0;
}
