#pragma once

#include <stdint.h>

// RGB (0-255 each) to the keyboard's HSV scale, where hue 0-255 spans the
// whole colour circle (QMK convention: red 0, green 85, blue 170).
static inline void peri_rgb_to_hsv(uint8_t r, uint8_t g, uint8_t b,
                                   uint8_t *h, uint8_t *s, uint8_t *v)
{
    int max = r > g ? (r > b ? r : b) : (g > b ? g : b);
    int min = r < g ? (r < b ? r : b) : (g < b ? g : b);
    int delta = max - min;

    *v = (uint8_t)max;
    if (delta == 0) { // black, white or grey: no hue to speak of
        *h = 0;
        *s = 0;
        return;
    }
    *s = (uint8_t)((255 * delta + max / 2) / max);

    // Hue in 1530 steps: six sectors of 255 around the circle.
    int hue;
    if (max == r)
        hue = 255 * (g - b) / delta;
    else if (max == g)
        hue = 510 + 255 * (b - r) / delta;
    else
        hue = 1020 + 255 * (r - g) / delta;
    if (hue < 0)
        hue += 1530;
    *h = (uint8_t)((hue * 256 + 765) / 1530); // rounds; 256 wraps to 0, same colour
}
