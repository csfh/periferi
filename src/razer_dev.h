#pragma once

#include "razer.h"

// The Razer engine's transport: numbered 64-byte feature reports on a hidraw node.

// Open a hidraw node ("hidraw1") if it is the Seiren V3 Chroma's control interface (1532:056f,
// usage page 0xFF53, usage 0x04). The frames are that microphone's: no other Razer device is
// ever opened. Returns an fd, or -1 after printing why.
int razer_open(const char *node);

// Send one request as a feature report and poll for its reply (about one second).
// `raw` receives the bytes of the last read, even when they do not decode (a reply that is not
// 64 bytes is never decoded), and `got` says how many bytes that read returned. Returns 0 for a valid reply (check reply->status), a
// razer_error for a malformed one, or RAZER_ERR_IO / RAZER_ERR_NO_REPLY.
int razer_call(int fd, uint8_t cls, uint8_t cmd, size_t data_size, const uint8_t *args,
               struct razer_reply *reply, uint8_t raw[RAZER_REPORT_LEN], int *got);

// Where a brightness value goes (the "variable storage" argument of the command).
#define RAZER_STORE_NONE 0x00  // asks for "now only"; the Seiren kept the value anyway (2026-10-14)
#define RAZER_STORE_FLASH 0x01 // the value the device keeps; writing it wears its memory

// Both return 0, or a razer_error. Firmware: class 0x00, command 0x81 (what the bytes mean is
// unproven). Brightness: class 0x0f, command 0x84 (get) or 0x04 (set), 0..255, LED id 0.
int razer_firmware(int fd, uint8_t version[2]);
int razer_brightness(int fd, uint8_t storage, uint8_t *brightness);
int razer_set_brightness(int fd, uint8_t storage, uint8_t brightness);
