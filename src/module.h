#pragma once

#include <stddef.h>
#include <stdint.h>

// A module teaches the daemon about one device: which HID interface it drives
// (match) and which built-in engine speaks to it. Modules are data, in a
// module.toml file (see catalog/drop-ctrl-v2/module.toml for the format).
struct peri_module {
    char id[49];       // [a-z0-9-], equals the module's directory name
    char name[65];
    char version[33];
    char engine[16];   // "xap"
    uint16_t vid, pid;
    bool has_usage;    // usage_page and usage given: the match picks one interface
    uint16_t usage_page, usage;
    bool rgb;          // capabilities.rgb
};

// Lowercase letters, digits and '-' (not at either end), 1-48 characters.
// Also what makes an id safe to use as a path component.
bool peri_module_id_valid(const char *id);

// Parse module.toml text. False with a one-line reason in err on any problem.
bool peri_module_parse(const char *text, size_t len, struct peri_module *m,
                       char *err, size_t err_size);

// Load a module.toml file (regular files only, no symlinks, at most 64 KiB).
bool peri_module_load(const char *path, struct peri_module *m, char *err, size_t err_size);

struct peri_device;

// True if the module drives this interface: vid, pid, usage page and usage all equal,
// and the device is not hidden (no module can ever claim a security key or a camera).
bool peri_module_matches(const struct peri_module *m, const struct peri_device *d);

// Copy <catalog_dir>/<id>/module.toml to <installed_dir>/<id>/module.toml (replacing
// any installed copy). The bytes written are the bytes that were validated.
// out may be null. False with a one-line reason in err.
bool peri_module_install(const char *catalog_dir, const char *installed_dir, const char *id,
                         struct peri_module *out, char *err, size_t err_size);

// Delete <installed_dir>/<id>. Refuses a symlinked <id>, or a directory holding
// anything besides module.toml. Never recursive.
bool peri_module_remove(const char *installed_dir, const char *id, char *err, size_t err_size);

// Load every <dir>/<id>/module.toml whose directory name equals its id,
// sorted by id. Anything invalid is skipped with a note on stderr.
size_t peri_modules_scan(const char *dir, struct peri_module *out, size_t max);

// Where installed modules live: $XDG_DATA_HOME/peri/modules (~/.local/share/...).
bool peri_installed_dir(char *buf, size_t size);

// The catalog shipped with this build: <dir of the executable>/../share/peri/catalog.
bool peri_catalog_dir(char *buf, size_t size);
