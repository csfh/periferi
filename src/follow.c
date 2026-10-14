#include "follow.h"

void follow_event(struct follow *f, int was, int is, long now_ms)
{
    if (!f->armed) {
        f->armed = true;
        f->before = was;
    }
    f->now = is;
    f->deadline_ms = now_ms + FOLLOW_QUIET_MS; // every event restarts the quiet period
}

int follow_due(struct follow *f, long now_ms)
{
    if (!f->armed || now_ms < f->deadline_ms)
        return FOLLOW_WAIT;
    f->armed = false;
    return f->now == f->before ? FOLLOW_NOTHING : f->now;
}

long follow_timeout_ms(const struct follow *f, long now_ms)
{
    if (!f->armed)
        return -1;
    return now_ms >= f->deadline_ms ? 0 : f->deadline_ms - now_ms;
}
