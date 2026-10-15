#pragma once

#include <stdbool.h>
#include <stddef.h>

// Daemon side of "peri status", "peri controls" and "peri set": talk to the device an installed module drives.
// Both reply "ok key=value ..." or "err message", newline ended.

// args: "<module-id>". Read-only.
void status_handle(const char *args, char *reply, size_t size);

// `controls <module-id>` for a camera (the v4l2 engine): every control it lists with its type, range, step,
// value and flags, and the options of each menu, in the form peri_cam_describe() gives, then the field of view
// as one more menu row when the camera has that extension unit. A read (control queries and extension-unit
// GETs only). Any other engine is refused.
void controls_handle(const char *args, char *reply, size_t size);

// `fov <module-id> <query> [size]` for a camera: ONE read of the field-of-view extension unit's selector 5
// (query: info, len, cur, min, max, res or def; size 1 to 4 bytes, default 1, 2 for len, 1 for info), one
// query per request. The kernel itself sends GET_LEN and GET_INFO the first time it touches an extension-unit
// control (both reads), and refuses a size that is not the control's own length. The unit is found by its GUID
// in the camera's own descriptor and must list selector 5; a query writes nothing, and no other unit or
// selector can be reached (see camera.h).
//
// `fov <module-id> set <wide|medium|narrow|0-2>` writes the field of view, after reading its length, capabilities, maximum and
// current value (the restore target); it writes nothing unless the camera says the control is writable.
void fov_handle(const char *args, char *reply, size_t size);

// args: "<module-id> get" or "<module-id> set gain=<0-100> monitor=<0-100> monitor_on=<yes|no>
// headphones=<0-100>" (any of the keys). The microphone's standard USB audio controls, through
// the sound card (see audio.h): the capture gain, the monitor mix into the headphones, and the
// headphone volume. Live mixer state, nothing is stored in the microphone by peri.
void audio_handle(const char *args, char *reply, size_t size);

// args: "<module-id> get|save|apply <unmuted|muted> [effect=<name>] [color=<rrggbb>]
// [brightness=<0-255>]". A module keeps two saved looks. get and save only read and write the
// saved file, so they never touch the device and work while it is unplugged; save replaces the
// whole look with the settings it is given. apply sends the saved look's effect and brightness
// to the device, and is the only one of the three that writes to it.
void look_handle(const char *args, char *reply, size_t size);

// The mic's mute state changed: apply the saved look for the new state, if following is on (the
// default) and a look is saved for it. Called by the daemon after a quiet period, never by a request.
void follow_mute_apply(const char *module_id, bool unmuted);

// args: "<module-id> brightness=<0-255>". Asks for a live-only write (storage 0), capped by the
// module's limits.max_brightness. The Seiren's stored value followed it anyway (verified
// 2026-10-14), so treat every set as a write to the device's memory.
void set_handle(const char *args, char *reply, size_t size);
