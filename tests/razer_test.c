#include <stdio.h>
#include <string.h>

#include "../src/razer.h"

static int failures;

static void check(bool ok, const char *what)
{
    if (!ok) {
        printf("FAIL %s\n", what);
        failures++;
    }
}

// A reply for firmware get (class 0x00, command 0x81) with arguments 01 02. Its CRC
// is worked out by hand: 52 ^ 02 ^ 81 ^ 01 ^ 02 = d2. The status byte is not covered.
static void firmware_reply(uint8_t frame[RAZER_REPORT_LEN])
{
    memset(frame, 0, RAZER_REPORT_LEN);
    frame[0] = 0x07;
    frame[1] = 0x02;
    frame[2] = 0x52;
    frame[6] = 0x02;
    frame[8] = 0x81;
    frame[9] = 0x01;
    frame[10] = 0x02;
    frame[62] = 0xd2;
}

int main(void)
{
    uint8_t frame[RAZER_REPORT_LEN];

    // The firmware-get reply captured from the real microphone (2026-10-13). Its arguments
    // (01 01) cancel under XOR, so this proves the CRC position and that the status byte is
    // left out, not that the arguments are covered.
    const uint8_t real[RAZER_REPORT_LEN] = { 0x07, 0x02, 0x52, 0x00, 0x00, 0x00, 0x02, 0x00,
                                             0x81, 0x01, 0x01, [62] = 0xd1 };
    struct razer_reply real_reply;
    check(razer_decode(real, 0x00, 0x81, &real_reply) == 0, "decode real reply");
    check(real_reply.status == RAZER_STATUS_OK && real_reply.len == 2 && real_reply.args[0] == 1
              && real_reply.args[1] == 1,
          "real reply fields");

    // Brightness get (class 0x0f, command 0x84) from the same microphone: arguments 01 00 66.
    // Here the argument byte changes the CRC (without it the XOR would be db, not bd), so this
    // one proves that the reply CRC covers the arguments.
    const uint8_t real_bright[RAZER_REPORT_LEN] = { 0x07, 0x02, 0x52, 0x00, 0x00, 0x00, 0x03,
                                                    0x0f, 0x84, 0x01, 0x00, 0x66, [62] = 0xbd };
    check(razer_decode(real_bright, 0x0f, 0x84, &real_reply) == 0 && real_reply.len == 3
              && real_reply.args[2] == 0x66,
          "decode real brightness reply");
    uint8_t tampered[RAZER_REPORT_LEN];
    memcpy(tampered, real_bright, sizeof tampered);
    tampered[11] = 0x67;
    check(razer_decode(tampered, 0x0f, 0x84, &real_reply) == RAZER_ERR_CRC, "argument is under the crc");

    // Request for firmware get: data size 2, no arguments. 52 ^ 02 ^ 81 = d1 (bytes 1..62,
    // stored at byte 63).
    check(razer_encode(frame, 0x52, 0x00, 0x81, nullptr, 2) == 0, "encode firmware get");
    const uint8_t want_fw[9] = { 0x07, 0x00, 0x52, 0x00, 0x00, 0x00, 0x02, 0x00, 0x81 };
    check(memcmp(frame, want_fw, sizeof want_fw) == 0 && frame[9] == 0, "firmware get layout");
    check(frame[63] == 0xd1, "firmware get crc");

    // Request for brightness set (class 0x0f, command 0x04, args 01 04 b7), data size 3:
    // 52 ^ 03 ^ 0f ^ 04 ^ 01 ^ 04 ^ b7 = e8.
    const uint8_t bright[3] = { 0x01, 0x04, 0xb7 };
    check(razer_encode(frame, 0x52, 0x0f, 0x04, bright, 3) == 0, "encode brightness");
    check(frame[0] == 0x07 && frame[2] == 0x52 && frame[6] == 3 && frame[7] == 0x0f
              && frame[8] == 0x04 && memcmp(frame + 9, bright, 3) == 0 && frame[12] == 0,
          "brightness layout");
    check(frame[63] == 0xe8, "brightness crc");

    uint8_t big[RAZER_REQUEST_ARGS_MAX + 1] = { 0 };
    check(razer_encode(frame, 0x52, 0, 0, big, RAZER_REQUEST_ARGS_MAX) == 0, "54 args fit");
    check(razer_encode(frame, 0x52, 0, 0, nullptr, RAZER_REQUEST_ARGS_MAX + 1) == RAZER_ERR_ARGS,
          "55 declared bytes refused");
    check(razer_encode(frame, 0x52, 0, 0, big, RAZER_REQUEST_ARGS_MAX + 1) == RAZER_ERR_ARGS,
          "55 args refused");

    // Replies.
    struct razer_reply r;
    firmware_reply(frame);
    check(razer_decode(frame, 0x00, 0x81, &r) == 0, "decode reply");
    check(r.status == RAZER_STATUS_OK && r.transaction == 0x52 && r.len == 2 && r.args[0] == 1
              && r.args[1] == 2,
          "decoded fields");

    firmware_reply(frame);
    frame[1] = RAZER_STATUS_BUSY; // not part of the CRC
    check(razer_decode(frame, 0x00, 0x81, &r) == 0 && r.status == RAZER_STATUS_BUSY, "status outside crc");

    firmware_reply(frame);
    frame[0] = 0x05;
    check(razer_decode(frame, 0x00, 0x81, &r) == RAZER_ERR_REPORT_ID, "wrong report id");

    firmware_reply(frame);
    frame[10] ^= 0x01;
    check(razer_decode(frame, 0x00, 0x81, &r) == RAZER_ERR_CRC, "bad crc");

    firmware_reply(frame);
    check(razer_decode(frame, 0x0f, 0x84, &r) == RAZER_ERR_MISMATCH, "other command");

    // Claims 54 argument bytes, one more than a reply holds: 52 ^ 36 ^ 81 = e5.
    firmware_reply(frame);
    frame[6] = 54;
    frame[9] = frame[10] = 0;
    frame[62] = 0xe5;
    check(razer_decode(frame, 0x00, 0x81, &r) == RAZER_ERR_SIZE, "oversized reply");

    return failures ? 1 : 0;
}
