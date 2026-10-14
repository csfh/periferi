#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "razer.h"

// Finding Razer frames in a capture of the vendor software's USB traffic. Wireshark (or tshark -x)
// writes every packet as a hex dump; this walks such a dump, picks out 64-byte frames that start
// with report id 7 and whose checksum fits the request rule or the reply rule, and describes each
// in one line. The point is to learn the commands the Windows software sends that nobody has
// published: every frame is named if it is a command we know, and "unknown" if not.

struct razer_found {
    size_t packet;            // which dump block (packet) it was in, from 1
    uint8_t frame[RAZER_REPORT_LEN];
    bool request_crc;         // the checksum fits the request rule (byte 63)
    bool reply_crc;           // the checksum fits the reply rule (byte 62)
};

typedef void (*razer_found_cb)(const struct razer_found *found, void *user);

// Walk a Wireshark text hex dump (packets separated by blank lines, 16 bytes per line after a
// four-digit offset). Calls cb for each frame found; returns how many. A packet may hold more than
// one frame; a frame is never reported twice.
size_t razer_scan_dump(const char *text, razer_found_cb cb, void *user);

// "firmware get", "effect set", ... or nullptr if (class, command) is not one we know.
const char *razer_command_name(uint8_t cls, uint8_t cmd);

// One line: "request txn=52 class=0f cmd=02 (effect set) size=09 args=01 00 01 ...".
void razer_describe(const struct razer_found *found, char *out, size_t size);

// Hex text ("07 00 52", "07:00:52", "070052") to bytes. Returns the count, or -1 if it is not hex.
ptrdiff_t razer_parse_hex(const char *text, uint8_t *out, size_t max);
