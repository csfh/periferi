#include <stdio.h>

#include "../src/audio.h"

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
    // The real ranges of the mic's controls (read with amixer on 2026-10-14).
    CHECK(peri_audio_pct_to_raw(0, 0, 522) == 0);
    CHECK(peri_audio_pct_to_raw(100, 0, 522) == 522);
    CHECK(peri_audio_pct_to_raw(50, 0, 522) == 261);
    CHECK(peri_audio_raw_to_pct(340, 0, 522) == 65);   // what the mic held
    CHECK(peri_audio_raw_to_pct(25, 0, 29) == 86);     // the monitor volume it held
    CHECK(peri_audio_raw_to_pct(809, 0, 1050) == 77);  // the headphone volume it held

    // A level survives the trip to the control and back, at every step of a small range.
    for (int pct = 0; pct <= 100; pct++) {
        long raw = peri_audio_pct_to_raw(pct, 0, 29);
        int back = peri_audio_raw_to_pct(raw, 0, 29);
        CHECK(back >= pct - 2 && back <= pct + 2);
        CHECK(peri_audio_pct_to_raw(back, 0, 29) == raw); // stable: no drift on a second trip
    }

    // Out-of-range input is clamped, and a degenerate range is harmless.
    CHECK(peri_audio_pct_to_raw(-5, 10, 20) == 10 && peri_audio_pct_to_raw(500, 10, 20) == 20);
    CHECK(peri_audio_raw_to_pct(-3, 0, 100) == 0 && peri_audio_raw_to_pct(900, 0, 100) == 100);
    CHECK(peri_audio_raw_to_pct(5, 7, 7) == 0);

    // A balance the user set on purpose (the headphones were 809 left, 849 right) survives a change.
    long moved[2];
    long old_lr[2] = { 809, 849 };
    peri_audio_shift(old_lr, 2, peri_audio_pct_to_raw(90, 0, 1050), 0, 1050, moved);
    CHECK(moved[0] == 945 && moved[1] == 985 && moved[1] - moved[0] == 40);
    peri_audio_shift(old_lr, 2, 100, 0, 1050, moved);          // down: still 40 apart
    CHECK(moved[0] == 100 && moved[1] == 140);
    peri_audio_shift(old_lr, 2, 1050, 0, 1050, moved);         // at the top the difference narrows
    CHECK(moved[0] == 1050 && moved[1] == 1050);
    peri_audio_shift(old_lr, 2, 0, 0, 1050, moved);            // at the bottom too
    CHECK(moved[0] == 0 && moved[1] == 40);
    long unchanged[2];
    peri_audio_shift(old_lr, 2, 809, 0, 1050, unchanged);       // the same value changes nothing
    CHECK(unchanged[0] == 809 && unchanged[1] == 849);
    long one;
    long single = 25;
    peri_audio_shift(&single, 1, 10, 0, 29, &one);              // a mono control
    CHECK(one == 10);

    // A card that does not exist is not found, and reading it fails cleanly.
    int v;
    CHECK(peri_audio_card(0xdead, 0xbeef) == -1);
    CHECK(!peri_audio_get(-1, PERI_AUDIO_GAIN, &v));
    CHECK(!peri_audio_set(-1, PERI_AUDIO_GAIN, 50));
    return failures ? 1 : 0;
}
