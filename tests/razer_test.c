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
    // Two getters sent to the same microphone on 2026-10-16 with zero arguments, class 0x0f commands
    // 0x92 and 0x93 (the setters 0x12 and 0x13 are named "display switch" and "tap" in a public note
    // for the V3 Pro). What the bytes mean is not known; the frames are real and decode cleanly.
    const uint8_t real_display[RAZER_REPORT_LEN] = { 0x07, 0x02, 0x52, 0x00, 0x00, 0x00, 0x03,
                                                     0x0f, 0x92, 0x00, 0x00, 0x01, [62] = 0xcd };
    check(razer_decode(real_display, 0x0f, 0x92, &real_reply) == 0 && real_reply.len == 3
              && real_reply.args[0] == 0x00 && real_reply.args[1] == 0x00 && real_reply.args[2] == 0x01,
          "decode real display-switch reply");
    const uint8_t real_tap[RAZER_REPORT_LEN] = { 0x07, 0x02, 0x52, 0x00, 0x00, 0x00, 0x04,
                                                 0x0f, 0x93, 0x00, 0x02, 0x00, 0x01, [62] = 0xc9 };
    check(razer_decode(real_tap, 0x0f, 0x93, &real_reply) == 0 && real_reply.len == 4
              && real_reply.args[1] == 0x02 && real_reply.args[3] == 0x01,
          "decode real tap reply");
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

    // Brightness set, live only (storage 00, LED 00, value 3c = 60): 52 ^ 03 ^ 0f ^ 04 ^ 3c = 66.
    // Restoring 66 (= 102): 52 ^ 03 ^ 0f ^ 04 ^ 66 = 3c.
    const uint8_t sixty[3] = { 0x00, 0x00, 0x3c }, back[3] = { 0x00, 0x00, 0x66 };
    check(razer_encode(frame, 0x52, 0x0f, 0x04, sixty, 3) == 0 && frame[63] == 0x66
              && frame[9] == 0 && frame[10] == 0 && frame[11] == 0x3c,
          "brightness set 60");
    check(razer_encode(frame, 0x52, 0x0f, 0x04, back, 3) == 0 && frame[63] == 0x3c,
          "brightness set 102");

    // Effect arguments, laid out as in the documented examples of the public reference
    // (storage 01, LED 00). Colours are red, green.
    uint8_t a[RAZER_EFFECT_ARGS_MAX];
    const uint8_t red[3] = { 0xff, 0, 0 }, green[3] = { 0, 0xff, 0 };
    const uint8_t want_static[9] = { 1, 0, 1, 0, 0, 1, 0xff, 0, 0 };
    const uint8_t want_breath_one[9] = { 1, 0, 2, 1, 0, 1, 0, 0xff, 0 };
    const uint8_t want_breath_random[6] = { 1, 0, 2, 0, 0, 0 };
    const uint8_t want_spectrum[6] = { 1, 0, 3, 0, 0, 0 };
    const uint8_t want_none[6] = { 1, 0, 0, 0, 0, 0 };
    check(razer_effect_args(1, RAZER_EFFECT_STATIC, red, a) == 9 && memcmp(a, want_static, 9) == 0,
          "effect static");
    check(razer_effect_args(1, RAZER_EFFECT_BREATHING, green, a) == 9
              && memcmp(a, want_breath_one, 9) == 0,
          "effect breathing, one colour");
    check(razer_effect_args(1, RAZER_EFFECT_BREATHING, nullptr, a) == 6
              && memcmp(a, want_breath_random, 6) == 0,
          "effect breathing, random");
    check(razer_effect_args(1, RAZER_EFFECT_SPECTRUM, nullptr, a) == 6
              && memcmp(a, want_spectrum, 6) == 0,
          "effect spectrum");
    check(razer_effect_args(1, RAZER_EFFECT_NONE, red, a) == 6 && memcmp(a, want_none, 6) == 0,
          "effect none ignores its colour");
    check(razer_effect_args(1, RAZER_EFFECT_STATIC, nullptr, a) == 0, "static needs a colour");
    check(razer_effect_args(1, 0x7f, red, a) == 0, "unknown effect");

    // The other effects, laid out as in the reference's documented examples. Their effect ids are
    // from the same place: they have not been seen to work on the microphone.
    struct razer_effect_opts o = { 0 };
    const uint8_t want_wave[6] = { 1, 0, 4, 0, 0x28, 0 }, want_wave_r[6] = { 1, 0, 4, 1, 0x28, 0 };
    check(razer_effect_build(1, RAZER_EFFECT_WAVE, &o, a) == 6 && memcmp(a, want_wave, 6) == 0, "wave");
    o.reverse = 1;
    check(razer_effect_build(1, RAZER_EFFECT_WAVE, &o, a) == 6 && memcmp(a, want_wave_r, 6) == 0,
          "wave reversed");
    o.reverse = 0;
    o.speed = RAZER_WAVE_FAST;                 // the fast wave: the same frame with the faster byte
    const uint8_t want_wave_fast[6] = { 1, 0, 4, 0, 0x10, 0 };
    check(razer_effect_build(1, RAZER_EFFECT_WAVE, &o, a) == 6 && memcmp(a, want_wave_fast, 6) == 0,
          "wave fast");
    o.speed = 0;
    check(razer_effect_build(1, RAZER_EFFECT_WAVE, &o, a) == 6 && a[4] == 0x28, "wave default speed");
    o.speed = RAZER_WAVE_FAST;                 // other effects have no speed byte, so it is ignored
    check(razer_effect_build(1, RAZER_EFFECT_SPECTRUM, &o, a) == 6 && a[4] == 0, "spectrum ignores speed");
    o.speed = 0;


    const uint8_t want_breath_two[12] = { 1, 0, 2, 2, 0, 2, 0, 0xff, 0, 0xff, 0, 0 };
    struct razer_effect_opts two = { .colors = 2 };
    memcpy(two.rgb[0], green, 3);
    memcpy(two.rgb[1], red, 3);
    check(razer_effect_build(1, RAZER_EFFECT_BREATHING, &two, a) == 12
              && memcmp(a, want_breath_two, 12) == 0, "breathing two colours");

    // Static needs a colour and ignores a second one; a third colour is not a thing.
    two.colors = 0;
    check(razer_effect_build(1, RAZER_EFFECT_STATIC, &two, a) == 0, "static needs a colour");
    two.colors = 2;
    check(razer_effect_build(1, RAZER_EFFECT_STATIC, &two, a) == 9, "static uses one colour");
    two.colors = 3;
    check(razer_effect_build(1, RAZER_EFFECT_BREATHING, &two, a) == 0, "three colours refused");
    check(razer_effect_build(1, RAZER_EFFECT_WAVE, nullptr, a) == 6, "no options is allowed");

    // Names, including the reversed ones.
    bool rev = false;
    check(razer_effect_lookup("wave", &rev) == RAZER_EFFECT_WAVE && !rev, "name wave");
    check(razer_effect_lookup("wave-reverse", &rev) == RAZER_EFFECT_WAVE && rev, "name wave-reverse");
    check(razer_effect_id("wheel") == -1 && razer_effect_id("wheel-reverse") == -1, "wheel is not an effect here");
    check(razer_effect_id("starlight") == -1 && razer_effect_id("fire") == -1
              && razer_effect_id(nullptr) == -1 && razer_effect_id("wave-") == -1, "names");

    // The device's words for a status.
    char sbuf[16];
    check(strcmp(razer_status_name(RAZER_STATUS_OK, sbuf), "ok") == 0, "status ok");
    check(strcmp(razer_status_name(RAZER_STATUS_NOT_SUPPORTED, sbuf), "not supported") == 0, "status not supported");
    check(strcmp(razer_status_name(RAZER_STATUS_BUSY, sbuf), "busy") == 0, "status busy");
    check(strcmp(razer_status_name(RAZER_STATUS_FAILURE, sbuf), "failed") == 0, "status failed");
    check(strcmp(razer_status_name(RAZER_STATUS_TIMEOUT, sbuf), "timed out") == 0, "status timed out");
    check(strcmp(razer_status_name(0x42, sbuf), "status 0x42") == 0, "unknown status");

    // Spectrum, storage 00, as a whole frame: 52 ^ 06 ^ 0f ^ 02 ^ 03 = 5a.
    check(razer_effect_args(0, RAZER_EFFECT_SPECTRUM, nullptr, a) == 6
              && razer_encode(frame, 0x52, 0x0f, 0x02, a, 6) == 0 && frame[6] == 6
              && frame[11] == 3 && frame[63] == 0x5a,
          "spectrum frame");

    // The muted ring's colour: a static effect frame aimed at zone 0x19, as sent to the real mic
    // (the device echoed these exact arguments back: 00 19 01 00 00 01 ff 00 00).
    const uint8_t mute_red[3] = { 0xff, 0x00, 0x00 };
    const uint8_t mute_expect[9] = { 0x00, RAZER_ZONE_MUTE, 0x01, 0x00, 0x00, 0x01, 0xff, 0x00, 0x00 };
    uint8_t mute_args[RAZER_EFFECT_ARGS_MAX];
    check(RAZER_ZONE_MUTE == 0x19, "the muted zone is 0x19");
    check(razer_mute_color_args(0, mute_red, mute_args) == 9 && memcmp(mute_args, mute_expect, 9) == 0,
          "muted colour frame");
    const uint8_t mute_orange[3] = { 0xff, 0x80, 0x00 };
    check(razer_mute_color_args(0, mute_orange, mute_args) == 9 && mute_args[1] == 0x19 && mute_args[7] == 0x80,
          "muted colour carries the colour");
    check(razer_encode(frame, 0x52, 0x0f, 0x02, mute_args, 9) == 0 && frame[6] == 9 && frame[9] == 0x00
              && frame[10] == 0x19,
          "muted colour as a whole frame");
    // Kept in the microphone's memory (storage 1): the arguments the real mic echoed for orange, which
    // came back after a replug.
    const uint8_t mute_kept_expect[9] = { 0x01, RAZER_ZONE_MUTE, 0x01, 0x00, 0x00, 0x01, 0xff, 0x80, 0x00 };
    check(razer_mute_color_args(1, mute_orange, mute_args) == 9 && memcmp(mute_args, mute_kept_expect, 9) == 0,
          "kept muted colour frame");

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
