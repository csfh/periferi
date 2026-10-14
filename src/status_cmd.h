#pragma once

#include <stddef.h>

// Daemon side of "peri status <module-id>": read what an installed module's device reports.
// args is the module id; reply gets "ok key=value ..." or "err message", newline ended.
void status_handle(const char *args, char *reply, size_t size);
