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

// What the mouse answers to its read queries. The request and reply layouts are those of the
// public OpenRazer driver's DeathAdder V3 Pro block (driver/razermouse_driver.c, the case that
// holds USB id 00c3); only the byte positions are used. All four reads were seen working on the
// real mouse (2026-10-14) and are test vectors; the two writes below have not been sent.
//
//   battery   class 07 cmd 80 size 2, no arguments; the level is reply argument 1, raw 0-255
//   charging  class 07 cmd 84 size 2, no arguments; reply argument 1 (0 or 1)
//   dpi       class 04 cmd 85 size 7, argument 0 = 00 (the driver's "no store"); x is reply
//             arguments 1-2 and y is 3-4, big endian
//   poll rate class 00 cmd 85 size 1, no arguments; reply argument 0: 01 = 1000 Hz, 02 = 500,
//             08 = 125 (the dongle's faster rates answer with other codes: 0 here, not guessed)
struct razer90_dpi {
    uint16_t x, y;
};

// Each returns false when the reply is too short to hold what is asked for.
bool razer90_battery_decode(const struct razer90_reply *reply, uint8_t *raw);
bool razer90_charging_decode(const struct razer90_reply *reply, bool *charging);
bool razer90_dpi_decode(const struct razer90_reply *reply, struct razer90_dpi *dpi);
// 0 for a code that is not one of the three above; false when the reply is too short.
bool razer90_poll_decode(const struct razer90_reply *reply, unsigned *hz);

// The raw battery byte as a percentage, rounded. UNPROVEN: that the byte spans 0-255 is only the
// doc comment above the public driver's battery attribute ("needs to be scaled from 0-255 ->
// 0-100", razermouse_driver.c); the driver itself prints the raw byte. The mouse answered 0x67 (103),
// which fits 0-255 (40%) and could not be a 0-100 percentage; that is all that is established.
unsigned razer90_battery_percent(uint8_t raw);

// The two writes, from the same public driver block. Pure builders: nothing here sends.
//
//   dpi set   class 04 cmd 05 size 7: argument 0 = 01 (the driver sends 01 whatever the caller
//             asked: so a set may be kept in the mouse's own memory), x high/low, y high/low, 00 00
//   poll set  class 00 cmd 05 size 1: argument 0 = 01 for 1000 Hz, 02 for 500, 08 for 125
//
// The driver clamps DPI to 100..45000 (the whole family) and sends 500 Hz for any rate it does not
// know; these builders refuse instead. Returns the data size (7 or 1), or 0 when refused.
#define RAZER90_DPI_MIN 100   // the driver's floor
#define RAZER90_DPI_MAX 45000 // the driver's ceiling for the family: this sensor's own limit is lower
size_t razer90_dpi_set_args(unsigned x, unsigned y, uint8_t args[7]);
size_t razer90_poll_set_args(unsigned hz, uint8_t args[1]);
