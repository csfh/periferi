#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// The compact Razer vendor frame as the Seiren V3 Chroma speaks it (the 90-byte frame of
// Razer mice, e.g. the DeathAdder V3 Pro, is a different shape: see razer90.h): one numbered
// 64-byte HID report (report ID 7, vendor page 0xFF53). Pure byte shuffling, no I/O, so
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
    RAZER_EFFECT_BREATHING = 0x02, // pulses; random colours, or one or two chosen colours
    RAZER_EFFECT_SPECTRUM = 0x03,  // cycles through all colours without pulsing
    RAZER_EFFECT_WAVE = 0x04,      // a wave that runs round the ring, in one of two directions
};
#define RAZER_EFFECT_ARGS_MAX 12

// The wave's speed byte. Slow is what was always sent (the public reference's value); fast is the
// "fast wave" value that reference mentions for one device. Neither number is from this microphone.
#define RAZER_WAVE_SLOW 0x28
#define RAZER_WAVE_FAST 0x10

// The parameters an effect may take. colors is how many of rgb[] are used (0, 1 or 2).
struct razer_effect_opts {
    uint8_t colors;
    uint8_t rgb[2][3];
    uint8_t reverse;  // wave: 0 or 1, which way round (which is which is not established)
    uint8_t speed;    // wave: the speed byte (lower is faster); 0 means RAZER_WAVE_SLOW
};

// The effect for a name: "none", "static", "breathing", "spectrum", "wave" or "wave-reverse".
// Returns the effect id (or -1) and, if `reverse` is not null, whether the name is the reversed one.
int razer_effect_lookup(const char *name, bool *reverse);
int razer_effect_id(const char *name);

// Build the arguments of an effect set (class 0x0f, command 0x02): variable storage, LED id 0,
// the effect, then its parameters. Returns the data size (6, 9 or 12), or 0 for an unknown effect
// or one that lacks what it needs (static needs a colour; a colour beyond `colors` is ignored).
size_t razer_effect_build(uint8_t storage, uint8_t effect, const struct razer_effect_opts *opts,
                          uint8_t args[RAZER_EFFECT_ARGS_MAX]);

// The same for the effects with at most one colour: `rgb` (3 bytes) is required for static and
// optional for breathing; the other effects ignore it.
size_t razer_effect_args(uint8_t storage, uint8_t effect, const uint8_t rgb[3],
                         uint8_t args[RAZER_EFFECT_ARGS_MAX]);

// The zone number whose static colour the microphone shows while it is MUTED. The effect frame has a
// zone byte (LED id) that the main ring ignores, but this one number is the muted ring: found on the
// owner's mic on 2026-10-16 by sending different colours to ranges of zone numbers (1 to ff) and
// asking which colour showed, narrowing 255 numbers to this one in four rounds, then confirmed by
// writing orange and red there. The unmuted ring is unaffected. Only a static colour counts there:
// spectrum and breathing sent to this zone changed nothing. Whether it survives a power cycle is not known.
#define RAZER_ZONE_MUTE 0x19

// The arguments of that write: the static-colour effect frame aimed at RAZER_ZONE_MUTE, storage 0.
// Returns the data size (9).
size_t razer_mute_color_args(const uint8_t rgb[3], uint8_t args[RAZER_EFFECT_ARGS_MAX]);

// What the mic's controls report on the control interface, as input report 5 (16 bytes with the
// id), seen in passive captures on 2026-10-14. Nothing here reads them: the daemon watches no
// device. Neither report is documented by Razer.
//   05 11 xx   tap-to-mute, flips on every tap. 01 = muted, 00 = unmuted. This is INFERRED, not
//              seen by the owner: on 2026-10-16 the mic recorded room sound for nine minutes
//              with the sound card's capture switch on, while its last report had been 00.
//              The first capture had guessed the opposite from one reading.
//   05 0f nn   dial level, absolute, steps of 2, 0x14..0x64 (20..100) in the captures

// Build a request frame whose data size is `len`. A null `args` means `len` zero bytes: queries
// such as firmware get declare a size but carry nothing. Returns 0, or RAZER_ERR_ARGS.
int razer_encode(uint8_t frame[RAZER_REPORT_LEN], uint8_t transaction, uint8_t cls, uint8_t cmd,
                 const uint8_t *args, size_t len);

// The device's own word for a reply status: "busy", "ok", "failed", "timed out", "not supported",
// or "status 0xNN" for a value this program does not know.
const char *razer_status_name(uint8_t status, char buf[16]);

// Check a reply frame against the request it answers and decode it. Returns 0 when the
// frame is well formed (look at reply->status for what the device said), else a razer_error.
int razer_decode(const uint8_t frame[RAZER_REPORT_LEN], uint8_t cls, uint8_t cmd,
                 struct razer_reply *reply);
