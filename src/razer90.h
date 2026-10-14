#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "razer.h" // razer_status, razer_error and razer_status_name are shared

// The long Razer vendor frame that Razer mice speak (the DeathAdder V3 Pro among them): one
// 90-byte HID feature report with NO report ID, on a vendor item inside the mouse interface.
// The Seiren's 64-byte frame is a different shape (razer.h). Pure byte shuffling, no I/O.
//
// Layout and checksum as in the public OpenRazer driver's `struct razer_report`
// (driver/razercommon.h) and `razer_calculate_crc` (razercommon.c); only the byte positions are
// used from it, none of its code.
//
//   byte  0      status (0 in a request)
//   byte  1      transaction id
//   bytes 2-3    remaining packets, big endian (0)
//   byte  4      protocol (0)
//   byte  5      data size: how many argument bytes follow
//   byte  6      command class
//   byte  7      command id
//   bytes 8..87  arguments (80 bytes)
//   byte  88     checksum = XOR of bytes 2..87
//   byte  89     reserved (0)
//
// The checksum leaves out the status and the transaction id, unlike the Seiren's. SEEN on the
// real mouse (2026-10-14, firmware get): the reply carried the same checksum rule in the same
// place, with the arguments under it (0x81; without them it would be 0x83).
//
// hidraw wants a leading report-ID byte (0x00 for an unnumbered report) in front of these 90
// bytes: 91 bytes for HIDIOCSFEATURE, and HIDIOCGFEATURE puts the 90 bytes of data at buf[1..90]
// (seen on the real mouse: a 91-byte read with the report-ID byte 0x00 first). The
// codec below only ever sees the 90 bytes; the transport owns the prefix, and razer90_hidraw()
// builds it so that off-by-one is tested here.

#define RAZER90_LEN 90
#define RAZER90_HIDRAW_LEN 91
#define RAZER90_ARGS_MAX 80 // bytes 8..87, for requests and replies alike

struct razer90_reply {
    uint8_t status;      // enum razer_status; other values are the device's own
    uint8_t transaction;
    size_t len;
    uint8_t args[RAZER90_ARGS_MAX];
};

// Build a request frame whose data size is `len`. A null `args` means `len` zero bytes. The
// transaction id is the caller's: no value is proven for any device here yet. Returns 0, or
// RAZER_ERR_ARGS when `len` is more than RAZER90_ARGS_MAX.
int razer90_encode(uint8_t frame[RAZER90_LEN], uint8_t transaction, uint8_t cls, uint8_t cmd,
                   const uint8_t *args, size_t len);

// Check a reply frame against the request it answers and decode it. Returns 0 when the frame is
// well formed (look at reply->status for what the device said), else a razer_error.
int razer90_decode(const uint8_t frame[RAZER90_LEN], uint8_t cls, uint8_t cmd,
                   struct razer90_reply *reply);

// The buffer to hand to HIDIOCSFEATURE: the report-ID byte 0x00, then the frame.
void razer90_hidraw(uint8_t buf[RAZER90_HIDRAW_LEN], const uint8_t frame[RAZER90_LEN]);
