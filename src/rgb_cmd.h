#pragma once

#include <stddef.h>

// Handle a daemon "rgb" request. args is what follows "rgb ": "get" or
// "set key=value...". Writes one line to reply: "ok key=value ..." with the
// keyboard's resulting config, or "err <message>". Never touches EEPROM.
void rgb_handle(const char *args, char *reply, size_t size);
