#pragma once

#include <stdbool.h>
#include <stddef.h>

// The looks peri keeps for a device that cannot be asked for its lighting. The mic reports its
// brightness but neither its effect nor its colour, so what a user chose in the panel has to be
// kept on our side. One small file per module: $XDG_CONFIG_HOME/peri/state/<module-id>.toml (else
// ~/.config/peri/state/), with one table per look:
//
//   [unmuted]
//   effect = "static"
//   color = "0000ff"     ("static" needs one; "breathing" and "starlight" may have one or two)
//   color2 = "ff0000"    (a second colour, only with a first, only for "breathing"/"starlight")
//   brightness = 102     (0-255)
//
//   [muted]
//   effect = "breathing"
//
// A look is the user's saved SETTINGS, not what the device is showing: saving one never touches
// the device, and applying it is a separate, deliberate step. Any field may be missing.

enum peri_look_which { PERI_LOOK_UNMUTED, PERI_LOOK_MUTED };

struct peri_look {
    char effect[16];    // see razer_effect_lookup(); "" if not set
    char color[7];      // six lowercase hex digits; "" if the effect takes none or none was chosen
    char color2[7];     // a second colour; "" unless the effect takes two and has a first
    int brightness;     // 0-255, or -1 if not set
};

#define PERI_LOOK_EMPTY ((struct peri_look){ .brightness = -1 })

bool peri_look_dir(char *buf, size_t size);

// "unmuted" or "muted".
bool peri_look_which_parse(const char *name, enum peri_look_which *which);
const char *peri_look_which_name(enum peri_look_which which);

// Save one look, keeping the other as it is. Atomic (temp file, then rename). False on any
// problem, including an invalid module id, effect, colour or brightness; the file is then left
// as it was. "static" needs a colour; effects that take no colour drop any given, and a second
// colour is dropped unless the effect takes two and a first colour is there.
bool peri_look_save(const char *module_id, enum peri_look_which which, const struct peri_look *look);

// A saved look, or an empty one when there is none, the file cannot be read, or what is in it is
// not something this program would have written. A file in an older format loads as empty. Never
// fails loudly; every field is validated again here.
struct peri_look peri_look_load(const char *module_id, enum peri_look_which which);
