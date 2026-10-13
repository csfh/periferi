#pragma once

#include "module.h"
#include "scan.h"

enum peri_status : uint8_t {
    PERI_STATUS_NONE,      // no module, bundled or installed, drives this device
    PERI_STATUS_AVAILABLE, // a bundled (catalog) module would, but it is not installed
    PERI_STATUS_INSTALLED, // an installed module drives it
};

// One physical device: the interfaces that share a group, collapsed into one.
struct peri_item {
    char group[64];
    uint16_t vid, pid;
    char name[96];            // from the first interface, in scan order
    enum peri_hidden hidden;
    enum peri_status status;
    char module[49];          // the module that gives the status, or ""
};

// Collapse interfaces into physical devices, in scan order, and give each a status.
// A module matching ANY interface of a device counts, not just the first one; installed
// beats available. A hidden device has no status (modules never match it).
// Returns the item count (at most max).
size_t peri_inventory(const struct peri_device *devs, size_t dev_count,
                      const struct peri_module *installed, size_t installed_count,
                      const struct peri_module *catalog, size_t catalog_count,
                      struct peri_item *out, size_t max);

const char *peri_status_name(enum peri_status status);
