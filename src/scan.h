#pragma once

#include <stddef.h>
#include <stdint.h>

// Why a physical device is left out of the default listing. Security keys and
// cameras are not peripherals this tool controls; keeping them out of sight
// also keeps them out of reach of `monitor` and `xap`.
enum peri_hidden : uint8_t {
    PERI_VISIBLE,
    PERI_HIDDEN_FIDO,   // some interface has a top-level collection on usage page 0xF1D0
    PERI_HIDDEN_CAMERA, // the USB device also has a video-class (0x0e) interface
};

struct peri_device {
    char node[32];     // "hidraw7"
    char group[64];    // physical device: interfaces of one USB device share it
    uint16_t vid, pid;
    uint16_t usage_page, usage; // the interface's control collection (see scan.c)
    char name[96];
    bool fido;         // this interface has a FIDO collection
    bool camera;       // this interface's USB device has a video-class interface
    enum peri_hidden hidden; // set on every interface of the group by peri_classify()
};

// Pick the control collection out of a HID report descriptor, and note FIDO.
// Sets usage_page, usage and fido; the rest of dev is left alone.
void peri_parse_descriptor(const uint8_t *desc, size_t len, struct peri_device *dev);

// Mark every interface of a physical device hidden if any one of its interfaces
// is FIDO (wins) or sits on a camera. Reasons are per group, never per node.
void peri_classify(struct peri_device *devs, size_t count);

// The key that makes interfaces one physical device. USB (bus 3): the port path, without the
// trailing "/inputN" (HID_UNIQ is a serial number there and is ignored). Anything else, such as
// Bluetooth, where phys is the local adapter and uniq the device: "<phys>+<uniq>". Spaces
// become '_'. If the key does not fit it is cut short, which can only merge devices (hide
// more), never split one. An empty phys falls back to the node name.
void peri_group_key(unsigned bus, const char *phys, const char *uniq, const char *node,
                    char *out, size_t size);

// "-", "fido" or "camera": one token, as used in the daemon's list protocol.
const char *peri_hidden_name(enum peri_hidden hidden);

enum peri_node_verdict {
    PERI_NODE_OK,
    PERI_NODE_MISSING,      // not in the list
    PERI_NODE_SECURITY_KEY, // its USB device has a FIDO interface (any interface, not just this one)
    PERI_NODE_NOT_XAP,      // needs_xap, but this is not an ff51:0058 control interface
};

// May a debugging command open this node? Decided from the scanned list alone, so it is
// testable without hardware. `monitor` passes needs_xap = false; `xap` writes, so true.
enum peri_node_verdict peri_node_check(const struct peri_device *devs, size_t count,
                                       const char *node, bool needs_xap);

// Describe one hidraw node ("hidraw7") from sysfs. False if it is gone.
// Per-interface only: use peri_scan() for the group-level `hidden` field.
bool peri_read_device(const char *node, struct peri_device *dev);

// List every HID device by reading sysfs, classified. Never opens a device node.
// Fills up to max entries and returns how many were found.
size_t peri_scan(struct peri_device *out, size_t max);
