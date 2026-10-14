#include <stdio.h>
#include <string.h>

#include "../src/razer90.h"

static int failures;

static void check(bool ok, const char *what)
{
    if (!ok) {
        printf("FAIL %s\n", what);
        failures++;
    }
}

// Every vector here is worked out by hand from the layout in razer90.h. Nothing has been read
// from a real mouse, except the first one below; they pin the codec's own rules.

// A reply for firmware get (class 0x00, command 0x81) with arguments 01 02. The checksum covers
// bytes 2..87: size 02 ^ command 81 ^ 01 ^ 02 = 80. The status and transaction are not in it.
static void firmware_reply(uint8_t frame[RAZER90_LEN], uint8_t status, uint8_t transaction)
{
    memset(frame, 0, RAZER90_LEN);
    frame[0] = status;
    frame[1] = transaction;
    frame[5] = 0x02;
    frame[7] = 0x81;
    frame[8] = 0x01;
    frame[9] = 0x02;
    frame[88] = 0x80;
}

int main(void)
{
    uint8_t frame[RAZER90_LEN];

    // The firmware-get reply read from the real DeathAdder V3 Pro (1532:00c3, 2026-10-14), as the
    // 90 data bytes of the 91-byte hidraw read: status 02 (ok), transaction 1f echoed, data size
    // 2, class 00, command 81, arguments 02 00 (what they mean is not established). The checksum
    // 81 covers the arguments: without them it would be 83.
    const uint8_t real[RAZER90_LEN] = { 0x02, 0x1f, 0x00, 0x00, 0x00, 0x02, 0x00, 0x81, 0x02, 0x00,
                                        [88] = 0x81 };
    struct razer90_reply real_reply;
    check(razer90_decode(real, 0x00, 0x81, &real_reply) == 0, "decode real reply");
    check(real_reply.status == RAZER_STATUS_OK && real_reply.transaction == 0x1f
              && real_reply.len == 2 && real_reply.args[0] == 0x02 && real_reply.args[1] == 0x00,
          "real reply fields");
    uint8_t tampered[RAZER90_LEN];
    memcpy(tampered, real, sizeof tampered);
    tampered[8] = 0x03;
    check(razer90_decode(tampered, 0x00, 0x81, &real_reply) == RAZER_ERR_CRC, "real argument is under the checksum");

    // Request for firmware get: size 2, no arguments. Checksum 02 ^ 81 = 83, stored at byte 88.
    check(razer90_encode(frame, 0x1f, 0x00, 0x81, nullptr, 2) == 0, "encode firmware get");
    check(frame[0] == 0 && frame[1] == 0x1f && frame[2] == 0 && frame[3] == 0 && frame[4] == 0
              && frame[5] == 2 && frame[6] == 0x00 && frame[7] == 0x81,
          "request header");
    check(frame[88] == 0x83 && frame[89] == 0, "request checksum and reserved byte");

    // The transaction id is not under the checksum, so another one changes only byte 1.
    uint8_t other[RAZER90_LEN];
    razer90_encode(other, 0x3f, 0x00, 0x81, nullptr, 2);
    check(other[1] == 0x3f && other[88] == frame[88], "transaction is outside the checksum");

    // 80 arguments is the most that fits: the last one sits at byte 87 and is under the checksum.
    uint8_t args[RAZER90_ARGS_MAX] = { 0 };
    args[79] = 0x40;
    check(razer90_encode(frame, 0x1f, 0x0f, 0x04, args, 80) == 0, "encode 80 arguments");
    check(frame[5] == 80 && frame[87] == 0x40, "last argument at byte 87");
    check(frame[88] == (uint8_t)(80 ^ 0x0f ^ 0x04 ^ 0x40), "checksum covers the last argument");
    check(razer90_encode(frame, 0x1f, 0x0f, 0x04, args, 81) == RAZER_ERR_ARGS, "81 arguments refused");

    // A reply decodes, and neither the status nor the transaction changes the checksum.
    struct razer90_reply reply;
    firmware_reply(frame, RAZER_STATUS_OK, 0x1f);
    check(razer90_decode(frame, 0x00, 0x81, &reply) == 0, "decode reply");
    check(reply.status == RAZER_STATUS_OK && reply.transaction == 0x1f && reply.len == 2
              && reply.args[0] == 1 && reply.args[1] == 2,
          "reply fields");
    firmware_reply(frame, RAZER_STATUS_BUSY, 0x77);
    check(razer90_decode(frame, 0x00, 0x81, &reply) == 0 && reply.status == RAZER_STATUS_BUSY
              && reply.transaction == 0x77,
          "status and transaction are outside the checksum");

    // A changed argument, or a changed checksum byte, is a checksum error.
    firmware_reply(frame, RAZER_STATUS_OK, 0x1f);
    frame[9] = 0x03;
    check(razer90_decode(frame, 0x00, 0x81, &reply) == RAZER_ERR_CRC, "argument is under the checksum");
    firmware_reply(frame, RAZER_STATUS_OK, 0x1f);
    frame[88] ^= 1;
    check(razer90_decode(frame, 0x00, 0x81, &reply) == RAZER_ERR_CRC, "bad checksum byte");

    // A well-formed reply to another command is refused, class and command each.
    firmware_reply(frame, RAZER_STATUS_OK, 0x1f);
    check(razer90_decode(frame, 0x00, 0x82, &reply) == RAZER_ERR_MISMATCH, "other command");
    check(razer90_decode(frame, 0x07, 0x81, &reply) == RAZER_ERR_MISMATCH, "other class");

    // A reply that claims 81 argument bytes (size 51 hex): checksum 51 ^ 81 = d0, valid, too big.
    memset(frame, 0, RAZER90_LEN);
    frame[5] = 0x51;
    frame[7] = 0x81;
    frame[88] = 0xd0;
    check(razer90_decode(frame, 0x00, 0x81, &reply) == RAZER_ERR_SIZE, "oversized data size");

    // hidraw's buffer: report-ID byte 0x00, then the 90 bytes unchanged.
    uint8_t buf[RAZER90_HIDRAW_LEN];
    memset(buf, 0xee, sizeof buf);
    razer90_encode(frame, 0x1f, 0x00, 0x81, nullptr, 2);
    razer90_hidraw(buf, frame);
    check(buf[0] == 0x00 && memcmp(buf + 1, frame, RAZER90_LEN) == 0 && buf[90] == frame[89],
          "hidraw prefix");

    // The four read requests the CLI sends, as the checksum byte they must carry (frame byte 88,
    // buffer index 89), worked out by hand: size ^ class ^ command, and no arguments.
    static const struct { uint8_t cls, cmd, size, crc; } reads[] = {
        { 0x07, 0x80, 2, 0x85 }, // battery:   02 ^ 07 ^ 80
        { 0x07, 0x84, 2, 0x81 }, // charging:  02 ^ 07 ^ 84
        { 0x04, 0x85, 7, 0x86 }, // dpi:       07 ^ 04 ^ 85 (argument 0 is 00)
        { 0x00, 0x85, 1, 0x84 }, // poll rate: 01 ^ 00 ^ 85
    };
    for (size_t i = 0; i < sizeof reads / sizeof *reads; i++) {
        uint8_t zeros[7] = { 0 };
        razer90_encode(frame, 0x1f, reads[i].cls, reads[i].cmd, zeros, reads[i].size);
        check(frame[88] == reads[i].crc && frame[0] == 0 && frame[1] == 0x1f, "read request checksum");
        razer90_hidraw(buf, frame);
        check(buf[89] == reads[i].crc && buf[90] == 0, "read request in the hidraw buffer");
    }

    // The decoders, on replies built by hand: argument bytes only matter by position.
    struct razer90_reply r = { .status = RAZER_STATUS_OK };
    uint8_t level;
    bool charging = true;
    struct razer90_dpi dpi;
    unsigned hz;

    r.len = 2;
    r.args[1] = 0xff;
    check(razer90_battery_decode(&r, &level) && level == 0xff, "battery is argument 1");
    check(razer90_battery_percent(0xff) == 100 && razer90_battery_percent(0) == 0
              && razer90_battery_percent(128) == 50 && razer90_battery_percent(1) == 0,
          "battery percentage rounds");
    r.len = 1;
    check(!razer90_battery_decode(&r, &level), "battery reply too short");
    r.len = 2;
    r.args[1] = 0;
    check(razer90_charging_decode(&r, &charging) && !charging, "not charging");
    r.args[1] = 1;
    check(razer90_charging_decode(&r, &charging) && charging, "charging");

    // 800 dpi is 0x0320, 1600 is 0x0640: argument 0 is skipped, x then y, high byte first.
    r.len = 7;
    memcpy(r.args, (uint8_t[]){ 0x00, 0x03, 0x20, 0x06, 0x40, 0x00, 0x00 }, 7);
    check(razer90_dpi_decode(&r, &dpi) && dpi.x == 800 && dpi.y == 1600, "dpi x and y");
    r.len = 4;
    check(!razer90_dpi_decode(&r, &dpi), "dpi reply too short");

    r.len = 1;
    r.args[0] = 0x01;
    check(razer90_poll_decode(&r, &hz) && hz == 1000, "poll 1000");
    r.args[0] = 0x02;
    check(razer90_poll_decode(&r, &hz) && hz == 500, "poll 500");
    r.args[0] = 0x08;
    check(razer90_poll_decode(&r, &hz) && hz == 125, "poll 125");
    r.args[0] = 0x04;
    check(razer90_poll_decode(&r, &hz) && hz == 0, "unknown poll code is 0, not guessed");
    r.len = 0;
    check(!razer90_poll_decode(&r, &hz), "poll reply too short");

    // The writes: 800 dpi is 0x0320, 1600 is 0x0640, and the storage byte is always 01.
    uint8_t wargs[7];
    check(razer90_dpi_set_args(800, 1600, wargs) == 7 && wargs[0] == 0x01 && wargs[1] == 0x03
              && wargs[2] == 0x20 && wargs[3] == 0x06 && wargs[4] == 0x40 && wargs[5] == 0
              && wargs[6] == 0,
          "dpi set arguments");
    check(razer90_dpi_set_args(99, 800, wargs) == 0 && razer90_dpi_set_args(800, 45001, wargs) == 0
              && razer90_dpi_set_args(100, 45000, wargs) == 7,
          "dpi range is refused, not clamped");
    // The request checksum covers the arguments: 07 ^ 04 ^ 05 ^ 01 ^ 03 ^ 20 ^ 06 ^ 40 = 6c...
    razer90_dpi_set_args(800, 1600, wargs);
    razer90_encode(frame, 0x1f, 0x04, 0x05, wargs, 7);
    check(frame[88] == (uint8_t)(0x07 ^ 0x04 ^ 0x05 ^ 0x01 ^ 0x03 ^ 0x20 ^ 0x06 ^ 0x40),
          "dpi set checksum");
    uint8_t parg[1];
    check(razer90_poll_set_args(1000, parg) == 1 && parg[0] == 0x01, "poll 1000");
    check(razer90_poll_set_args(500, parg) == 1 && parg[0] == 0x02, "poll 500");
    check(razer90_poll_set_args(125, parg) == 1 && parg[0] == 0x08, "poll 125");
    check(razer90_poll_set_args(250, parg) == 0 && razer90_poll_set_args(8000, parg) == 0
              && razer90_poll_set_args(0, parg) == 0,
          "other poll rates are refused, not turned into 500");

    if (failures == 0)
        printf("razer90_test: all passed\n");
    return failures ? 1 : 0;
}
