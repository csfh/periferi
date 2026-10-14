#pragma once

#include <stdbool.h>
#include <stddef.h>

// What peri last sent to a device that cannot be asked for its lighting. The mic reports its
// brightness but neither its effect nor its colour, so the panel can only show a stable choice if
// we keep a record. One small file per module: $XDG_CONFIG_HOME/peri/state/<module-id>.toml
// (else ~/.config/peri/state/), holding what the device ACCEPTED, written only after it did:
//
//   effect = "static"
//   color = "0000ff"     (only for "static"; every other effect has none)
//
// It is a record of what peri sent, not of what the device shows: a change made any other way
// is not in it.

struct peri_look {
    char effect[16]; // "none", "static", "breathing" or "spectrum"; "" if unknown
    char color[7];   // six lowercase hex digits for "static"; "" otherwise
};

bool peri_look_dir(char *buf, size_t size);

// Save the look a device accepted. `color` is needed for "static" and ignored for the other
// effects, which clear any earlier colour. Atomic (temp file, then rename). False on any problem,
// including an invalid module id, effect or colour; the old record is then left as it was.
bool peri_look_save(const char *module_id, const char *effect, const char *color);

// The saved look, or an empty one (effect "") when there is none, the file cannot be read, or
// its contents are not a look this program would have written. Never fails loudly.
struct peri_look peri_look_load(const char *module_id);
