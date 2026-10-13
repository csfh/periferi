#include "inventory.h"

#include <stdio.h>
#include <string.h>

const char *peri_status_name(enum peri_status status)
{
    return status == PERI_STATUS_INSTALLED   ? "installed"
           : status == PERI_STATUS_AVAILABLE ? "available"
                                             : "none";
}

static const struct peri_module *first_match(const struct peri_module *mods, size_t count,
                                             const struct peri_device *d)
{
    for (size_t i = 0; i < count; i++)
        if (peri_module_matches(&mods[i], d))
            return &mods[i];
    return nullptr;
}

size_t peri_inventory(const struct peri_device *devs, size_t dev_count,
                      const struct peri_module *installed, size_t installed_count,
                      const struct peri_module *catalog, size_t catalog_count,
                      struct peri_item *out, size_t max)
{
    size_t count = 0;
    for (size_t i = 0; i < dev_count; i++) {
        const struct peri_device *d = &devs[i];

        struct peri_item *item = nullptr;
        for (size_t j = 0; j < count && !item; j++)
            if (strcmp(out[j].group, d->group) == 0)
                item = &out[j];
        if (!item) {
            if (count == max)
                continue;
            item = &out[count++];
            *item = (struct peri_item){ .vid = d->vid, .pid = d->pid, .hidden = d->hidden };
            snprintf(item->group, sizeof item->group, "%s", d->group);
            snprintf(item->name, sizeof item->name, "%s", d->name);
        }

        const struct peri_module *m = first_match(installed, installed_count, d);
        if (m && item->status != PERI_STATUS_INSTALLED) {
            item->status = PERI_STATUS_INSTALLED;
            snprintf(item->module, sizeof item->module, "%s", m->id);
        } else if (!m && item->status == PERI_STATUS_NONE
                   && (m = first_match(catalog, catalog_count, d))) {
            item->status = PERI_STATUS_AVAILABLE;
            snprintf(item->module, sizeof item->module, "%s", m->id);
        }
    }
    return count;
}
