#pragma once

#include "module.h"

// Where `peri setup` installs the rule. 60-: must sort before 70-uaccess.rules.
static const char peri_udev_rules_path[] = "/etc/udev/rules.d/60-peri.rules";

// The rule file for these modules: one line per distinct vid:pid, sorted, and nothing but
// those numbers in lowercase hex. Root installs this file but the modules are user-writable
// files, so no module text (names, ids) is ever copied into it. USB only: ATTRS{idVendor}
// does not exist for Bluetooth devices, which a module cannot be granted this way.
// Returns the length, or 0 if it does not fit in size (including the NUL).
// Does the module's device need a line in the hidraw rule? Not a camera: the v4l2 engine talks to
// the video node, whose access the system already grants to the logged-in user (the HID interface
// the module is anchored on is only there to identify the device).
bool peri_udev_needs_rule(const struct peri_module *m);

size_t peri_udev_rules(const struct peri_module *mods, size_t count, char *out, size_t size);

// True if `rules` (the text of an installed rule file) has the exact line for this module.
bool peri_udev_covers(const char *rules, const struct peri_module *m);

// The installed rule file's text, or null if there is none. Caller frees.
char *peri_udev_read_installed(void);
