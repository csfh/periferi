#include <stdio.h>
#include <string.h>

#include "../src/common.h"
#include "../src/scan.h"

static int failures;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
            failures++;                                                    \
        }                                                                  \
    } while (0)

// A real descriptor, captured from /sys/class/hidraw/*/device/report_descriptor.
static bool parse_fixture(const char *name, struct peri_device *dev)
{
    char path[512], hex[1024];
    snprintf(path, sizeof path, "%s/%s.hex", FIXTURES_DIR, name);
    FILE *f = fopen(path, "r");
    if (!f || !fgets(hex, sizeof hex, f)) {
        printf("FAIL cannot read %s\n", path);
        failures++;
        if (f)
            fclose(f);
        return false;
    }
    fclose(f);
    hex[strcspn(hex, "\n")] = '\0';

    uint8_t desc[512];
    ssize_t len = peri_parse_hex(hex, desc, sizeof desc);
    CHECK(len > 0);
    *dev = (struct peri_device){};
    if (len > 0)
        peri_parse_descriptor(desc, (size_t)len, dev);
    return len > 0;
}

static void parse_bytes(const uint8_t *desc, size_t len, struct peri_device *dev)
{
    *dev = (struct peri_device){};
    peri_parse_descriptor(desc, len, dev);
}

static void test_descriptors(void)
{
    struct peri_device d;

    // The devices this tool drives keep the control interface it expects.
    if (parse_fixture("ctrl-xap", &d))
        CHECK(d.usage_page == 0xFF51 && d.usage == 0x58 && !d.fido);
    if (parse_fixture("seiren", &d))
        CHECK(d.usage_page == 0xFF53 && d.usage == 0x04 && !d.fido);

    // A security key is FIDO; a camera's HID interface is not (it is found by topology).
    if (parse_fixture("yubikey", &d))
        CHECK(d.usage_page == 0xF1D0 && d.usage == 0x01 && d.fido);
    if (parse_fixture("brio", &d))
        CHECK(d.usage_page == 0x0C && d.usage == 0x01 && !d.fido);

    // FIDO next to a vendor collection: the vendor one is picked, FIDO is still noted.
    static const uint8_t composite[] = { 0x06, 0xD0, 0xF1, 0x09, 0x01, 0xA1, 0x01, 0xC0,
                                         0x06, 0x51, 0xFF, 0x09, 0x58, 0xA1, 0x01, 0xC0 };
    parse_bytes(composite, sizeof composite, &d);
    CHECK(d.fido && d.usage_page == 0xFF51 && d.usage == 0x58);

    // FIDO only counts as a top-level collection, not nested inside another.
    static const uint8_t nested[] = { 0x05, 0x01, 0x09, 0x06, 0xA1, 0x01,
                                      0x06, 0xD0, 0xF1, 0x09, 0x01, 0xA1, 0x02, 0xC0, 0xC0 };
    parse_bytes(nested, sizeof nested, &d);
    CHECK(!d.fido && d.usage_page == 0x01 && d.usage == 0x06);

    // A plain keyboard, and an empty or truncated descriptor.
    static const uint8_t keyboard[] = { 0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0xC0 };
    parse_bytes(keyboard, sizeof keyboard, &d);
    CHECK(!d.fido && d.usage_page == 0x01 && d.usage == 0x06);
    parse_bytes(keyboard, 0, &d);
    CHECK(!d.fido && d.usage_page == 0 && d.usage == 0);
    parse_bytes(composite, 3, &d); // cut in the middle of an item
    CHECK(!d.fido);

    // Parsing again overwrites the earlier answer.
    d = (struct peri_device){ .fido = true, .usage_page = 0xF1D0, .usage = 1 };
    peri_parse_descriptor(keyboard, sizeof keyboard, &d);
    CHECK(!d.fido && d.usage_page == 0x01);
}

static struct peri_device iface(const char *group, bool fido, bool camera)
{
    struct peri_device d = { .fido = fido, .camera = camera };
    snprintf(d.group, sizeof d.group, "%s", group);
    return d;
}

static void test_classify(void)
{
    struct peri_device devs[] = {
        iface("key", false, false),    // an OTP keyboard interface of a security key...
        iface("key", true, false),     // ...whose FIDO interface is the other one
        iface("cam", false, true),     // a camera's buttons
        iface("cam", false, false),    // (its other HID interface)
        iface("plain", false, false),
        iface("both", true, true),     // FIDO wins over camera
        iface("other", false, false),  // a different group is not affected
    };
    peri_classify(devs, sizeof devs / sizeof *devs);
    CHECK(devs[0].hidden == PERI_HIDDEN_FIDO && devs[1].hidden == PERI_HIDDEN_FIDO);
    CHECK(devs[2].hidden == PERI_HIDDEN_CAMERA && devs[3].hidden == PERI_HIDDEN_CAMERA);
    CHECK(devs[4].hidden == PERI_VISIBLE);
    CHECK(devs[5].hidden == PERI_HIDDEN_FIDO);
    CHECK(devs[6].hidden == PERI_VISIBLE);

    // Classifying again from scratch clears stale reasons.
    devs[1].fido = false;
    peri_classify(devs, sizeof devs / sizeof *devs);
    CHECK(devs[0].hidden == PERI_VISIBLE && devs[1].hidden == PERI_VISIBLE);

    CHECK(strcmp(peri_hidden_name(PERI_VISIBLE), "-") == 0);
    CHECK(strcmp(peri_hidden_name(PERI_HIDDEN_FIDO), "fido") == 0);
    CHECK(strcmp(peri_hidden_name(PERI_HIDDEN_CAMERA), "camera") == 0);
}

static struct peri_device node(const char *name, const char *group, uint16_t page, uint16_t usage,
                               bool fido, bool camera)
{
    struct peri_device d = iface(group, fido, camera);
    snprintf(d.node, sizeof d.node, "%s", name);
    d.usage_page = page;
    d.usage = usage;
    return d;
}

// What `monitor` (reads) and `xap` (writes) may open.
static void test_node_check(void)
{
    struct peri_device devs[] = {
        node("hidraw1", "key", 0xF1D0, 0x01, true, false),   // a security key's FIDO interface
        node("hidraw2", "key", 0x01, 0x06, false, false),    // its OTP keyboard sibling
        node("hidraw3", "cam", 0x0C, 0x01, false, true),     // a camera
        node("hidraw4", "mic", 0xFF53, 0x04, false, false),  // a vendor control interface
        node("hidraw5", "kbd", 0xFF51, 0x58, false, false),  // an XAP keyboard
        node("hidraw6", "kbd", 0x01, 0x06, false, false),    // the keyboard's typing interface
    };
    size_t n = sizeof devs / sizeof *devs;
    peri_classify(devs, n);

    CHECK(peri_node_check(devs, n, "hidraw1", false) == PERI_NODE_SECURITY_KEY);
    CHECK(peri_node_check(devs, n, "hidraw1", true) == PERI_NODE_SECURITY_KEY);
    CHECK(peri_node_check(devs, n, "hidraw2", false) == PERI_NODE_SECURITY_KEY); // same device
    CHECK(peri_node_check(devs, n, "hidraw2", true) == PERI_NODE_SECURITY_KEY);
    CHECK(peri_node_check(devs, n, "hidraw3", false) == PERI_NODE_OK);
    CHECK(peri_node_check(devs, n, "hidraw3", true) == PERI_NODE_NOT_XAP);
    CHECK(peri_node_check(devs, n, "hidraw4", false) == PERI_NODE_OK);
    CHECK(peri_node_check(devs, n, "hidraw4", true) == PERI_NODE_NOT_XAP);
    CHECK(peri_node_check(devs, n, "hidraw5", false) == PERI_NODE_OK);
    CHECK(peri_node_check(devs, n, "hidraw5", true) == PERI_NODE_OK);
    CHECK(peri_node_check(devs, n, "hidraw6", true) == PERI_NODE_NOT_XAP); // right device, wrong interface
    CHECK(peri_node_check(devs, n, "hidraw99", false) == PERI_NODE_MISSING);
    CHECK(peri_node_check(devs, n, "hidraw99", true) == PERI_NODE_MISSING);
    CHECK(peri_node_check(devs, 0, "hidraw5", false) == PERI_NODE_MISSING);
}

static void test_group_key(void)
{
    char a[64], b[64];

    // USB: the interfaces of one device share a key; HID_UNIQ (a serial number) is ignored.
    peri_group_key(3, "usb-0000:c5:00.3-1.1.1/input0", "SER1", "hidraw0", a, sizeof a);
    peri_group_key(3, "usb-0000:c5:00.3-1.1.1/input2", "", "hidraw3", b, sizeof b);
    CHECK(strcmp(a, "usb-0000:c5:00.3-1.1.1") == 0 && strcmp(a, b) == 0);
    peri_group_key(3, "usb-0000:c5:00.3-1.1.2/input0", "", "hidraw9", b, sizeof b);
    CHECK(strcmp(a, b) != 0);

    // Bluetooth: phys is the adapter, so the device's own address tells devices apart.
    peri_group_key(5, "aa:bb:cc:dd:ee:ff", "11:22:33:44:55:66", "hidraw4", a, sizeof a);
    peri_group_key(5, "aa:bb:cc:dd:ee:ff", "66:55:44:33:22:11", "hidraw5", b, sizeof b);
    CHECK(strcmp(a, b) != 0);
    CHECK(strcmp(a, "aa:bb:cc:dd:ee:ff+11:22:33:44:55:66") == 0);

    // A Bluetooth security key next to a Bluetooth mouse on one adapter: only the key is hidden.
    struct peri_device devs[2] = { iface("", true, false), iface("", false, false) };
    peri_group_key(5, "aa:bb:cc:dd:ee:ff", "11:22:33:44:55:66", "hidraw4", devs[0].group, 64);
    peri_group_key(5, "aa:bb:cc:dd:ee:ff", "66:55:44:33:22:11", "hidraw5", devs[1].group, 64);
    peri_classify(devs, 2);
    CHECK(devs[0].hidden == PERI_HIDDEN_FIDO && devs[1].hidden == PERI_VISIBLE);

    // Non-USB without a uniq: like USB, minus "/inputN". Spaces become '_'.
    peri_group_key(0x18, "i2c-ELAN 0001/input0", "", "hidraw6", a, sizeof a);
    CHECK(strcmp(a, "i2c-ELAN_0001") == 0);

    // No phys: the node name, so the key is never empty.
    peri_group_key(3, "", "", "hidraw7", a, sizeof a);
    CHECK(strcmp(a, "hidraw7") == 0);

    // Too long: cut short (merging devices at worst), not replaced by the node name.
    char small[12];
    peri_group_key(5, "aa:bb:cc:dd:ee:ff", "11:22:33:44:55:66", "hidraw4", small, sizeof small);
    CHECK(strcmp(small, "aa:bb:cc:dd") == 0);
}

int main(void)
{
    test_descriptors();
    test_classify();
    test_node_check();
    test_group_key();
    return failures ? 1 : 0;
}
