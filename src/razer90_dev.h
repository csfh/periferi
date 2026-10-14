#pragma once

#include "razer90.h"

// The long-frame transport: unnumbered 90-byte feature reports on a hidraw node, sent as 91
// bytes with the report-ID byte 0x00 in front (see razer90.h).

// The transaction id the public OpenRazer driver uses for the DeathAdder V3 Pro family. Not seen
// working on this mouse yet.
#define RAZER90_TRANSACTION_OPENRAZER 0x1f

// Open a hidraw node ("hidraw0") if it is the DeathAdder V3 Pro's mouse interface (1532:00c3,
// usage page 0x0001, usage 0x0002: the vendor feature item sits inside that collection, so that
// is the usage periferi's scanner reports). Nothing else is opened. Returns an fd, or -1 after
// printing why.
int razer90_open(const char *node);

// Send one request and poll for its reply (about 1.5 s, 31 ms apart: the pause OpenRazer uses
// for this family). `raw` receives the 91 bytes of the last read, even when they do not decode,
// and `got` says how many bytes that read returned; only a 91-byte read is decoded (the 90
// bytes of data sit at raw[1..90]). Returns 0 for a valid reply (check reply->status), a
// razer_error for a malformed one, or RAZER_ERR_IO / RAZER_ERR_NO_REPLY.
int razer90_call(int fd, uint8_t cls, uint8_t cmd, size_t data_size, const uint8_t *args,
                 struct razer90_reply *reply, uint8_t raw[RAZER90_HIDRAW_LEN], int *got);

// Firmware get (class 0x00, command 0x81, size 2): the two bytes the mouse answers with. What they
// mean is not established (it answered 02 00 on 2026-10-14). 0 on an ok reply of at least two
// bytes, else a razer_error (RAZER_ERR_STATUS if the device said something other than ok).
int razer90_firmware(int fd, uint8_t version[2]);
