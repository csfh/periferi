#pragma once

#include "razer.h"

// The daemon's reader of the microphone's own controls (mute tap, dial). It holds one read-only,
// non-blocking fd on the control interface of the installed "razer" module's device and keeps
// the last reported state. perid polls controls_fd() along with its client socket.
//
// The state starts unknown and is lost when the device goes away or perid restarts: the mic
// cannot be asked for it. hidraw drops new reports when a reader's queue is full (64), so a
// daemon stuck for seconds while the dial spins can miss the last turns; the level is absolute,
// so the next event puts it right.

// The fd to poll for POLLIN, or -1 while no device is attached.
int controls_fd(void);

// Look for the device (an installed razer module whose device is plugged in) and open it.
// Does nothing if already attached or if no such module is installed. Cheap enough to call
// every few seconds while detached.
void controls_rescan(void);

// The fd was readable (or errored): read every pending report. A read error or a hang-up
// detaches, and the state is unknown again.
void controls_service(short revents);

// Detach now, e.g. after a module was installed or removed; the next rescan decides again.
void controls_detach(void);

// The state for a module's device: unknown unless that module's device is attached.
struct razer_state controls_state(const char *module_id);
