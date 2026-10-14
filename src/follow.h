#pragma once

#include <stdbool.h>

// Deciding when to apply a saved look because the mic's mute state changed. The mic reports every tap
// as it happens, a double tap arrives as two events 150-400 ms apart, and the firmware changes its
// own light a moment after a tap. So a change is acted on only after a short quiet period, once,
// and only if the state after the burst differs from the state before it: a double tap changes
// nothing, a triple tap changes the state once.

#define FOLLOW_QUIET_MS 500

struct follow {
    bool armed;        // a change is waiting for its quiet period to end
    int before;        // the mute state before the burst: -1 unknown, 0 muted, 1 unmuted
    int now;           // the state after the latest event in the burst
    long deadline_ms;  // when the quiet period ends
};

#define FOLLOW_INIT ((struct follow){ .before = -1, .now = -1 })

// A mute event at time `now_ms`: the state was `was` before it and is `is` after it (0 muted, 1
// unmuted). Starts a burst, or extends the one under way.
void follow_event(struct follow *f, int was, int is, long now_ms);

enum follow_action {
    FOLLOW_WAIT = -2,     // the quiet period is not over (or nothing is pending)
    FOLLOW_NOTHING = -1,  // the burst ended where it began
    FOLLOW_MUTED = 0,     // apply the Muted look
    FOLLOW_UNMUTED = 1,   // apply the Unmuted look
};

// At time `now_ms`: what to do. Returns FOLLOW_WAIT until the quiet period is over, then reports the
// outcome once and clears the burst.
int follow_due(struct follow *f, long now_ms);

// Milliseconds until the quiet period ends (0 if it is over), or -1 if nothing is pending. For a
// poll() timeout.
long follow_timeout_ms(const struct follow *f, long now_ms);
