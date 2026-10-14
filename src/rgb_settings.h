#pragma once

#include "xap.h"

// Apply one "key=value" (color, effect, enable, hue, sat, val, speed, flags) to cfg.
// max_val is the keyboard's brightness limit from its module (0 = the module gives none);
// module_id names that module in the message that tells the user to update it.
// Brightness is never sent above the limit: `val=` above it is refused, `color=` is
// clamped to it, and with no limit known both are refused. False with a one-line reason
// in err; cfg is untouched on failure.
bool rgb_apply_setting(struct xap_rgb *cfg, const char *setting, uint8_t max_val,
                       const char *module_id, char *err, size_t err_size);

// Names of the effects in an enabled-effects mask (bit n = effect id n), comma separated.
// Bits with no known effect are skipped. False if it does not fit.
bool rgb_effects_csv(uint64_t mask, char *out, size_t size);
