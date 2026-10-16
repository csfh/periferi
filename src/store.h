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
//   color = "0000ff"     ("static" needs one; "breathing" may have one or two)
//   color2 = "ff0000"    (a second colour, only with a first, only for "breathing")
//   speed = "fast"       ("slow" or "fast", only for the wave effects)
//   brightness = 102     (0-255)
//
//   [muted]
//   effect = "breathing"
//
// A look is the user's saved SETTINGS, not what the device is showing: saving one never touches
// the device, and applying it is a separate step: by the Apply button, or by the daemon when the
// mic's mute state changes and `follow_mute` is on (the default). Any field may be missing. A
// `follow_mute = false` line at the top of the file turns the following off.

enum peri_look_which { PERI_LOOK_UNMUTED, PERI_LOOK_MUTED };

struct peri_look {
    char effect[16];    // see razer_effect_lookup(); "" if not set
    char color[7];      // six lowercase hex digits; "" if the effect takes none or none was chosen
    char color2[7];     // a second colour; "" unless the effect takes two and has a first
    char speed[5];      // "slow" or "fast" for a wave effect; "" otherwise (the default speed)
    int brightness;     // 0-255, or -1 if not set
};

#define PERI_LOOK_EMPTY ((struct peri_look){ .brightness = -1 })

bool peri_look_dir(char *buf, size_t size);

// "unmuted" or "muted".
bool peri_look_which_parse(const char *name, enum peri_look_which *which);
const char *peri_look_which_name(enum peri_look_which which);

// One setting of a look save, in the order the user wrote them. key is
// "effect", "color", "color2", "speed" or "brightness".
struct peri_look_field {
    const char *key;
    const char *value;
};

// Which of the five settings a request has already given. Zero means none.
struct peri_look_seen {
    bool effect, color, color2, speed, brightness;
};

// "effect=static" is one look setting. True, and *field filled, when token is
// one that *seen has not recorded yet; *seen then records it. False when the
// token is not a look setting or that setting was already given: both are a
// usage error for the caller, so a second effect= is not a different failure
// from an unknown word. color2 is recognized as itself, not as color.
bool peri_look_take(const char *token, struct peri_look_seen *seen, struct peri_look_field *field);

// Copy the fields into *raw. The first value that cannot be stored is the
// reason (unknown effect, a colour that is not six hex digits, a speed other
// than slow or fast, a brightness outside 0-255), walking the list in order.
// Null if every field is well-formed. A repeated key overwrites. This does not
// yet ask whether the fields belong together.
const char *peri_look_read(const struct peri_look_field *fields, size_t count, struct peri_look *raw);

// What would be stored. Static needs a colour, and a second colour needs a
// first: those are reasons, not silent drops, so a request cannot lose a
// colour it was given. A colour or a speed the effect does not take is
// dropped, and a colour is lowercase. Null on success. The module's brightness
// limit is not applied here.
const char *peri_look_stored(const struct peri_look *raw, struct peri_look *out);

// Save one look, keeping the other as it is. Atomic (temp file, then rename). False on any
// problem, including an invalid module id, effect, colour or brightness; the file is then left
// as it was. "static" needs a colour; effects that take no colour drop any given, and a second
// colour is dropped unless the effect takes two and a first colour is there. A speed is kept only
// for the wave effects, and must be "slow" or "fast".
bool peri_look_save(const char *module_id, enum peri_look_which which, const struct peri_look *look);

// Whether the daemon applies the matching look when the mic's mute state changes (see follow.h).
// On by default: only a file that says `follow_mute = false` turns it off. Saving a look never
// changes it. peri_look_set_follow() returns false for an invalid module id or on a write error.
bool peri_look_follow(const char *module_id);
bool peri_look_set_follow(const char *module_id, bool on);

// A saved look, or an empty one when there is none, the file cannot be read, or what is in it is
// not something this program would have written. A file in an older format loads as empty. Never
// fails loudly; every field is validated again here.
struct peri_look peri_look_load(const char *module_id, enum peri_look_which which);
