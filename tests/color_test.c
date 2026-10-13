#include <stdio.h>

#include "../src/color.h"

static int failures;

static void check(const char *name, uint8_t r, uint8_t g, uint8_t b,
                  uint8_t want_h, uint8_t want_s, uint8_t want_v)
{
    uint8_t h, s, v;
    peri_rgb_to_hsv(r, g, b, &h, &s, &v);
    if (h != want_h || s != want_s || v != want_v) {
        printf("FAIL %-8s rgb(%u,%u,%u): got hsv(%u,%u,%u), want hsv(%u,%u,%u)\n", name, r, g, b,
               h, s, v, want_h, want_s, want_v);
        failures++;
    }
}

int main(void)
{
    check("red",     255,   0,   0,   0, 255, 255);
    check("yellow",  255, 255,   0,  43, 255, 255);
    check("green",     0, 255,   0,  85, 255, 255);
    check("cyan",      0, 255, 255, 128, 255, 255);
    check("blue",      0,   0, 255, 171, 255, 255); // QMK says 170; rounding gives 171
    check("magenta", 255,   0, 255, 213, 255, 255);
    check("white",   255, 255, 255,   0,   0, 255);
    check("black",     0,   0,   0,   0,   0,   0);
    check("grey",    128, 128, 128,   0,   0, 128);
    check("dim red", 128,   0,   0,   0, 255, 128);
    check("orange",  255, 128,   0,  21, 255, 255);
    return failures ? 1 : 0;
}
