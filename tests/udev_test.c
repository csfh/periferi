#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/udev.h"

static int failures;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
            failures++;                                                    \
        }                                                                  \
    } while (0)

static struct peri_module mod(uint16_t vid, uint16_t pid)
{
    struct peri_module m = { .vid = vid, .pid = pid, .has_usage = true };
    snprintf(m.id, sizeof m.id, "m-%04x-%04x", vid, pid);
    snprintf(m.name, sizeof m.name, "Evil \"name\"\n# injected");
    return m;
}

int main(void)
{
    char out[4096];
    const char *ctrl_line = "SUBSYSTEM==\"hidraw\", ATTRS{idVendor}==\"359b\", "
                            "ATTRS{idProduct}==\"0009\", TAG+=\"uaccess\"\n";

    // One module: header, then exactly its line, and nothing from the module's text.
    struct peri_module ctrl = mod(0x359b, 9);
    size_t len = peri_udev_rules(&ctrl, 1, out, sizeof out);
    CHECK(len > 0 && len == strlen(out));
    CHECK(strstr(out, ctrl_line) != nullptr);
    CHECK(strstr(out, "Evil") == nullptr && strstr(out, "injected") == nullptr
          && strstr(out, "m-359b") == nullptr);

    // A camera (the v4l2 engine) needs no hidraw rule: its video node's access comes from the system,
    // and the HID interface its module is anchored on is only there to identify the device.
    struct peri_module cam = mod(0x046d, 0x085e);
    snprintf(cam.engine, sizeof cam.engine, "v4l2");
    CHECK(!peri_udev_needs_rule(&cam) && peri_udev_needs_rule(&ctrl));
    struct peri_module mixed[] = { cam, ctrl };
    len = peri_udev_rules(mixed, 2, out, sizeof out);
    CHECK(len > 0 && strstr(out, ctrl_line) != nullptr && strstr(out, "046d") == nullptr);

    // Lowercase hex, sorted by vid:pid, each pair once, regardless of input order.
    struct peri_module many[] = { mod(0xABCD, 2), mod(0x1532, 0x56F), mod(0x359b, 9), mod(0x1532, 0x56F),
                                  mod(0x1532, 0x00C3) };
    len = peri_udev_rules(many, 5, out, sizeof out);
    CHECK(len > 0);
    CHECK(strstr(out, "ABCD") == nullptr && strstr(out, "abcd") != nullptr);
    char *c3 = strstr(out, "00c3"), *s56 = strstr(out, "056f"), *ctl = strstr(out, "359b"),
         *ab = strstr(out, "abcd");
    CHECK(c3 && s56 && ctl && ab && c3 < s56 && s56 < ctl && ctl < ab);
    CHECK(strstr(s56 + 1, "056f") == nullptr); // the duplicate pair appears once
    size_t lines = 0;
    for (const char *p = out; (p = strstr(p, "SUBSYSTEM==")); p++)
        lines++;
    CHECK(lines == 4);

    // Any order gives the same file.
    char out2[4096];
    struct peri_module rev[] = { many[4], many[3], many[2], many[1], many[0] };
    peri_udev_rules(rev, 5, out2, sizeof out2);
    CHECK(strcmp(out, out2) == 0);

    // No modules: just the header, no rules at all.
    len = peri_udev_rules(nullptr, 0, out, sizeof out);
    CHECK(len > 0 && strstr(out, "SUBSYSTEM") == nullptr && strstr(out, "Bluetooth"));

    // Too small a buffer: refuse, never truncate into a half rule.
    CHECK(peri_udev_rules(&ctrl, 1, out, 10) == 0);
    peri_udev_rules(&ctrl, 1, out, sizeof out);
    CHECK(peri_udev_rules(&ctrl, 1, out2, strlen(out)) == 0);      // no room for the NUL
    CHECK(peri_udev_rules(&ctrl, 1, out2, strlen(out) + 1) == strlen(out));

    // Coverage.
    peri_udev_rules(&ctrl, 1, out, sizeof out);
    CHECK(peri_udev_covers(out, &ctrl));
    CHECK(!peri_udev_covers(out, &many[0]));
    // Today's vendor-wide rule does not cover the module: setup must update it.
    const char *old_file = "# Installed by `peri setup`\n"
                           "SUBSYSTEM==\"hidraw\", ATTRS{idVendor}==\"1532\", TAG+=\"uaccess\"\n"
                           "SUBSYSTEM==\"hidraw\", ATTRS{idVendor}==\"359b\", TAG+=\"uaccess\"\n";
    CHECK(!peri_udev_covers(old_file, &ctrl));
    // Empty text, an unrelated line that mentions 359b, and a commented-out copy.
    CHECK(!peri_udev_covers("", &ctrl));
    CHECK(!peri_udev_covers("# 359b:0009 is the CTRL\n", &ctrl));
    char commented[512];
    snprintf(commented, sizeof commented, "# %s", ctrl_line);
    CHECK(!peri_udev_covers(commented, &ctrl));
    // The line must be whole: a longer line that merely contains it does not count.
    char longer[512];
    snprintf(longer, sizeof longer, "%.*s, OWNER=\"x\"\n", (int)strlen(ctrl_line) - 1, ctrl_line);
    CHECK(!peri_udev_covers(longer, &ctrl));
    // ...and the last line may lack its newline.
    char bare[512];
    snprintf(bare, sizeof bare, "%.*s", (int)strlen(ctrl_line) - 1, ctrl_line);
    CHECK(peri_udev_covers(bare, &ctrl));

    return failures ? 1 : 0;
}
