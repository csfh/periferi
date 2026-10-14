#pragma once

#include <stddef.h>
#include <stdint.h>

// QMK XAP over a device's vendor HID interface (usage page 0xFF51, usage 0x58).

struct xap_reply {
    uint8_t flags;      // bit 0: success, bit 1: secure route is locked
    size_t len;
    uint8_t payload[60];
};

// Open a hidraw node ("hidraw13") if it is an XAP interface. Returns an fd, or -1 after printing why.
int xap_open(const char *node);

// Send one request (route bytes then payload) and wait for its reply.
// Returns 0 when a reply arrived (check reply->flags), -1 on timeout or error.
int xap_call(int fd, const uint8_t *route, size_t route_len,
             const uint8_t *payload, size_t payload_len, struct xap_reply *reply);

// rgbmatrix config, route 0x06 0x04 0x03 / 0x04. Setting changes the running
// config only (the firmware's "noeeprom" setters); xap_rgb_save() persists it.
struct xap_rgb {
    bool enable;
    uint8_t mode, hue, sat, val, speed, flags;
};

int xap_rgb_get(int fd, struct xap_rgb *cfg);
int xap_rgb_set(int fd, const struct xap_rgb *cfg);

// The effects the firmware has enabled, route 0x06 0x04 0x02: a mask where bit n is effect id n.
int xap_rgb_effects(int fd, uint64_t *mask);

// Persist the keyboard's current rgbmatrix config to its EEPROM (route
// 0x06 0x04 0x05). Wears flash: only ever call this on an explicit request.
int xap_rgb_save(int fd);

// Effect IDs are fixed by XAP (see data/constants/lighting/rgb_matrix_*.hjson).
const char *xap_effect_name(uint8_t id);  // nullptr if unknown
int xap_effect_id(const char *name);      // -1 if unknown
