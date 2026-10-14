#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// The compact Razer vendor frame as the Seiren V3 Chroma speaks it (the 90-byte frame of
// Razer mice, e.g. the DeathAdder V3 Pro, is a different shape and is not covered here): one numbered 64-byte
// HID report (report ID 7, vendor page 0xFF53). Pure byte shuffling, no I/O, so
// it can be tested without the microphone.
//
//   byte  0      report ID (7)
//   byte  1      status (0 in a request)
//   byte  2      transaction id
//   bytes 3-4    remaining packets (0)
//   byte  5      protocol (0)
//   byte  6      data size: how many argument bytes follow
//   byte  7      command class
//   byte  8      command id
//   bytes 9..    arguments
//   request:     byte 63 = XOR of bytes 1..62
//   reply:       byte 62 = XOR of bytes 2..61

#define RAZER_REPORT_LEN 64
#define RAZER_REPORT_ID 0x07
#define RAZER_TRANSACTION_DEFAULT 0x52 // the two public reports disagree (0x52, 0x1f): unproven here
#define RAZER_REQUEST_ARGS_MAX 54      // bytes 9..62
#define RAZER_REPLY_ARGS_MAX 53        // bytes 9..61

enum razer_status {
    RAZER_STATUS_BUSY = 0x01,
    RAZER_STATUS_OK = 0x02,
    RAZER_STATUS_FAILURE = 0x03,
    RAZER_STATUS_TIMEOUT = 0x04,
    RAZER_STATUS_NOT_SUPPORTED = 0x05,
};

enum razer_error {
    RAZER_ERR_ARGS = -1,     // too many argument bytes for one frame
    RAZER_ERR_REPORT_ID = -2,
    RAZER_ERR_CRC = -3,
    RAZER_ERR_MISMATCH = -4, // the reply answers a different class or command
    RAZER_ERR_SIZE = -5,     // the reply claims more argument bytes than fit
    RAZER_ERR_IO = -6,       // the request could not be sent (transport, see razer_dev.h)
    RAZER_ERR_NO_REPLY = -7, // sent, but no 64-byte reply came back in time
    RAZER_ERR_STATUS = -8,   // a valid reply, but the device did not say OK or sent too little
};

struct razer_reply {
    uint8_t status;      // enum razer_status; other values are the device's own
    uint8_t transaction;
    size_t len;
    uint8_t args[RAZER_REPLY_ARGS_MAX];
};

// The lighting effects of the extended matrix (class 0x0f, command 0x02). The numbers are the
// effect ids the device takes.
enum razer_effect {
    RAZER_EFFECT_NONE = 0x00,
    RAZER_EFFECT_STATIC = 0x01,
    RAZER_EFFECT_BREATHING = 0x02, // pulses; random colours unless one colour is given
    RAZER_EFFECT_SPECTRUM = 0x03,  // cycles through all colours without pulsing
};
#define RAZER_EFFECT_ARGS_MAX 9

// The arguments of an effect set: variable storage, LED id 0, the effect, then its parameters.
// `rgb` (3 bytes) is required for static and optional for breathing; the other effects ignore it.
// Returns the data size (6 or 9), or 0 if the effect is unknown or lacks its colour.
size_t razer_effect_args(uint8_t storage, uint8_t effect, const uint8_t rgb[3],
                         uint8_t args[RAZER_EFFECT_ARGS_MAX]);

// What the mic's controls report on the control interface, as input report 5 (16 bytes with the
// id), seen in passive captures on 2026-10-14. Neither is documented by Razer.
//   05 11 xx   tap-to-mute state, flips on every tap: 00 = muted, 01 = unmuted
//   05 0f nn   dial level, absolute, steps of 2, 0x14..0x64 (20..100) in the captures
enum razer_event_kind {
    RAZER_EVENT_NONE,  // not a report we know
    RAZER_EVENT_MUTE,  // value: 0 muted, 1 unmuted
    RAZER_EVENT_DIAL,  // value: the level as reported
};

struct razer_event {
    enum razer_event_kind kind;
    uint8_t value;
};

// Decode one report read from the control interface. Unknown ids, types and mute values give
// RAZER_EVENT_NONE (and false), so a firmware that reports more never breaks a reader.
bool razer_event_decode(const uint8_t *report, size_t len, struct razer_event *ev);

// What the controls last reported. -1 means nothing has been seen yet: the mic cannot be asked
// for its state, so it stays unknown until the first tap or turn.
struct razer_state {
    int mute; // -1 unknown, 0 muted, 1 unmuted
    int dial; // -1 unknown, else the last level as reported
};
#define RAZER_STATE_UNKNOWN ((struct razer_state){ -1, -1 })

void razer_state_apply(struct razer_state *state, const struct razer_event *ev);

// Build a request frame whose data size is `len`. A null `args` means `len` zero bytes: queries
// such as firmware get declare a size but carry nothing. Returns 0, or RAZER_ERR_ARGS.
int razer_encode(uint8_t frame[RAZER_REPORT_LEN], uint8_t transaction, uint8_t cls, uint8_t cmd,
                 const uint8_t *args, size_t len);

// Check a reply frame against the request it answers and decode it. Returns 0 when the
// frame is well formed (look at reply->status for what the device said), else a razer_error.
int razer_decode(const uint8_t frame[RAZER_REPORT_LEN], uint8_t cls, uint8_t cmd,
                 struct razer_reply *reply);
