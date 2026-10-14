#pragma once

#include <stddef.h>

// Daemon side of "peri status" and "peri set": talk to the device an installed module drives.
// Both reply "ok key=value ..." or "err message", newline ended.

// args: "<module-id>". Read-only.
void status_handle(const char *args, char *reply, size_t size);

// args: "<module-id> get|save|apply <unmuted|muted> [effect=<name>] [color=<rrggbb>]
// [brightness=<0-255>]". A module keeps two saved looks. get and save only read and write the
// saved file, so they never touch the device and work while it is unplugged; save replaces the
// whole look with the settings it is given. apply sends the saved look's effect and brightness
// to the device, and is the only one of the three that writes to it.
void look_handle(const char *args, char *reply, size_t size);

// args: "<module-id> brightness=<0-255>". Asks for a live-only write (storage 0), capped by the
// module's limits.max_brightness. The Seiren's stored value followed it anyway (verified
// 2026-10-14), so treat every set as a write to the device's memory.
void set_handle(const char *args, char *reply, size_t size);
