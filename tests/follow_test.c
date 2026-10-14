#include <stdio.h>

#include "../src/follow.h"

static int failures;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
            failures++;                                                    \
        }                                                                  \
    } while (0)

int main(void)
{
    struct follow f = FOLLOW_INIT;

    // Nothing pending: nothing to wait for, nothing to do.
    CHECK(follow_due(&f, 0) == FOLLOW_WAIT && follow_timeout_ms(&f, 0) == -1);

    // One tap to mute: wait out the quiet period, then apply the Muted look, once.
    follow_event(&f, 1, 0, 1000);
    CHECK(follow_timeout_ms(&f, 1000) == FOLLOW_QUIET_MS);
    CHECK(follow_due(&f, 1200) == FOLLOW_WAIT && follow_timeout_ms(&f, 1200) == FOLLOW_QUIET_MS - 200);
    CHECK(follow_due(&f, 1500) == FOLLOW_MUTED);
    CHECK(follow_due(&f, 1600) == FOLLOW_WAIT && follow_timeout_ms(&f, 1600) == -1);   // reported once

    // One tap to unmute applies the Unmuted look.
    follow_event(&f, 0, 1, 5000);
    CHECK(follow_due(&f, 5499) == FOLLOW_WAIT);
    CHECK(follow_due(&f, 5500) == FOLLOW_UNMUTED);

    // A double tap (as captured: two events about 200 ms apart) ends where it began: nothing.
    follow_event(&f, 1, 0, 9000);
    follow_event(&f, 0, 1, 9200);
    CHECK(follow_due(&f, 9600) == FOLLOW_WAIT);                // the second event restarted the wait
    CHECK(follow_timeout_ms(&f, 9600) == 100);
    CHECK(follow_due(&f, 9700) == FOLLOW_NOTHING);

    // A triple tap changes the state once: one apply, for the final state.
    follow_event(&f, 1, 0, 20000);
    follow_event(&f, 0, 1, 20150);
    follow_event(&f, 1, 0, 20300);
    CHECK(follow_due(&f, 20799) == FOLLOW_WAIT);
    CHECK(follow_due(&f, 20800) == FOLLOW_MUTED);

    // A change from "unknown" counts: the first tap after the daemon started.
    follow_event(&f, -1, 1, 30000);
    CHECK(follow_due(&f, 30500) == FOLLOW_UNMUTED);
    follow_event(&f, -1, 0, 31000);
    CHECK(follow_due(&f, 31500) == FOLLOW_MUTED);

    // An event that arrives after the quiet period is a new burst, not part of the old one.
    follow_event(&f, 1, 0, 40000);
    CHECK(follow_due(&f, 40500) == FOLLOW_MUTED);
    follow_event(&f, 0, 1, 40600);
    CHECK(follow_due(&f, 41100) == FOLLOW_UNMUTED);

    // If the daemon is slow to look, a due burst is reported with a zero timeout first.
    follow_event(&f, 1, 0, 50000);
    CHECK(follow_timeout_ms(&f, 52000) == 0);
    CHECK(follow_due(&f, 52000) == FOLLOW_MUTED);
    return failures ? 1 : 0;
}
