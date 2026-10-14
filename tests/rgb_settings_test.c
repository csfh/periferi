#include <stdio.h>
#include <string.h>

#include "../src/rgb_settings.h"

static int failures;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
            failures++;                                                    \
        }                                                                  \
    } while (0)

static const struct xap_rgb start = { .enable = true, .mode = 0, .hue = 48, .sat = 255, .val = 143,
                                      .speed = 48, .flags = 0xff };

static bool apply(struct xap_rgb *cfg, const char *setting, uint8_t cap, char *err)
{
    return rgb_apply_setting(cfg, setting, cap, "some-module", err, 200);
}

int main(void)
{
    char err[200] = "";
    struct xap_rgb c;

    // Brightness: up to the limit is fine, above it is refused and nothing changes.
    c = start;
    CHECK(apply(&c, "val=191", 191, err) && c.val == 191);
    CHECK(apply(&c, "val=0", 191, err) && c.val == 0);
    c = start;
    CHECK(!apply(&c, "val=192", 191, err) && strstr(err, "191") && c.val == 143);
    CHECK(!apply(&c, "val=255", 191, err) && c.val == 143);
    CHECK(!apply(&c, "val=0xff", 191, err) && c.val == 143);
    CHECK(!apply(&c, "val=256", 191, err) && !apply(&c, "val=-1", 191, err) && !apply(&c, "val=", 191, err));
    CHECK(!apply(&c, "val=x", 191, err) && c.val == 143);
    CHECK(apply(&c, "val=255", 255, err) && c.val == 255); // a module that allows it

    // No limit known (an old module file): brightness is refused, everything else still works.
    c = start;
    CHECK(!apply(&c, "val=100", 0, err) && strstr(err, "peri use some-module") && c.val == 143);
    CHECK(!apply(&c, "color=#ff0000", 0, err) && c.hue == 48 && c.val == 143);
    CHECK(apply(&c, "hue=10", 0, err) && c.hue == 10);

    // color= sets hue, saturation and brightness from the RGB, with brightness held to the limit.
    c = start;
    CHECK(apply(&c, "color=#ff0000", 191, err) && c.hue == 0 && c.sat == 255 && c.val == 191);
    CHECK(apply(&c, "color=00ff00", 191, err) && c.sat == 255 && c.val == 191 && c.hue > 60 && c.hue < 100);
    CHECK(apply(&c, "color=#ffffff", 191, err) && c.sat == 0 && c.val == 191);
    CHECK(apply(&c, "color=#303030", 191, err) && c.val == 48);
    CHECK(apply(&c, "color=#ff0000", 255, err) && c.val == 255);
    c = start;
    CHECK(!apply(&c, "color=#ff00", 191, err) && !apply(&c, "color=#gg0000", 191, err)
          && !apply(&c, "color=", 191, err) && c.hue == 48 && c.val == 143);

    // The other settings.
    c = start;
    CHECK(apply(&c, "hue=0", 191, err) && c.hue == 0 && apply(&c, "hue=255", 191, err) && c.hue == 255);
    CHECK(!apply(&c, "hue=256", 191, err) && c.hue == 255);
    CHECK(apply(&c, "sat=7", 191, err) && c.sat == 7);
    CHECK(apply(&c, "speed=0x10", 191, err) && c.speed == 16);
    CHECK(apply(&c, "speed=010", 191, err) && c.speed == 10); // decimal, not octal
    CHECK(apply(&c, "flags=0x0f", 191, err) && c.flags == 0x0f);
    CHECK(apply(&c, "enable=0", 191, err) && !c.enable && apply(&c, "enable=1", 191, err) && c.enable);
    CHECK(!apply(&c, "enable=2", 191, err) && c.enable);
    CHECK(apply(&c, "effect=5", 191, err) && c.mode == 5);
    char by_name[80];
    snprintf(by_name, sizeof by_name, "effect=%s", xap_effect_name(7));
    CHECK(apply(&c, by_name, 191, err) && c.mode == 7);
    CHECK(apply(&c, "effect=0", 191, err) && c.mode == 0);
    CHECK(apply(&c, "effect=solid_color", 191, err) && c.mode == 0);
    CHECK(!apply(&c, "effect=nope", 191, err) && !apply(&c, "effect=200", 191, err));
    CHECK(!apply(&c, "bogus=1", 191, err) && strstr(err, "bad setting"));
    CHECK(!apply(&c, "hue", 191, err) && !apply(&c, "", 191, err) && !apply(&c, "=5", 191, err));

    // Effects list from the board's mask: ids 0x00-0x2B, and nothing else.
    char out[1024];
    uint64_t enabled = (1ULL << 44) - 1; // what the CTRL reports: ff ff ff ff ff 0f
    CHECK(rgb_effects_csv(enabled, out, sizeof out));
    CHECK(strncmp(out, "solid_color,", 12) == 0);
    int commas = 0;
    for (const char *p = out; *p; p++)
        commas += *p == ',';
    CHECK(commas == 43); // 44 names
    CHECK(strstr(out, xap_effect_name(0x2B)) != nullptr);
    CHECK(xap_effect_name(0x2C) != nullptr && strstr(out, xap_effect_name(0x2C)) == nullptr);
    CHECK(rgb_effects_csv(0, out, sizeof out) && out[0] == '\0');
    CHECK(rgb_effects_csv(1ULL << 3, out, sizeof out) && strcmp(out, xap_effect_name(3)) == 0);
    CHECK(rgb_effects_csv(1ULL << 63, out, sizeof out) && out[0] == '\0'); // no such effect
    CHECK(!rgb_effects_csv(enabled, out, 20));
    CHECK(!rgb_effects_csv(enabled, out, 0));

    return failures ? 1 : 0;
}
