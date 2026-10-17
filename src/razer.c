// Razer vendor frames: see razer.h for the layout. Nothing here opens a device.

#include "razer.h"

#include <stdio.h>
#include <string.h>

static uint8_t xor_bytes(const uint8_t *p, size_t from, size_t to)
{
    uint8_t x = 0;
    for (size_t i = from; i <= to; i++)
        x ^= p[i];
    return x;
}

int razer_encode(uint8_t frame[RAZER_REPORT_LEN], uint8_t transaction, uint8_t cls, uint8_t cmd,
                 const uint8_t *args, size_t len)
{
    if (len > RAZER_REQUEST_ARGS_MAX)
        return RAZER_ERR_ARGS;

    memset(frame, 0, RAZER_REPORT_LEN);
    frame[0] = RAZER_REPORT_ID;
    frame[2] = transaction;
    frame[6] = (uint8_t)len;
    frame[7] = cls;
    frame[8] = cmd;
    if (args)
        memcpy(frame + 9, args, len);
    frame[63] = xor_bytes(frame, 1, 62);
    return 0;
}

int razer_decode(const uint8_t frame[RAZER_REPORT_LEN], uint8_t cls, uint8_t cmd,
                 struct razer_reply *reply)
{
    if (frame[0] != RAZER_REPORT_ID)
        return RAZER_ERR_REPORT_ID;
    if (frame[62] != xor_bytes(frame, 2, 61))
        return RAZER_ERR_CRC;
    if (frame[7] != cls || frame[8] != cmd)
        return RAZER_ERR_MISMATCH;
    if (frame[6] > RAZER_REPLY_ARGS_MAX)
        return RAZER_ERR_SIZE;

    reply->status = frame[1];
    reply->transaction = frame[2];
    reply->len = frame[6];
    memcpy(reply->args, frame + 9, reply->len);
    return 0;
}

size_t razer_effect_args(uint8_t storage, uint8_t effect, const uint8_t rgb[3],
                         uint8_t args[RAZER_EFFECT_ARGS_MAX])
{
    struct razer_effect_opts opts = { .colors = rgb ? 1 : 0 };
    if (rgb)
        memcpy(opts.rgb[0], rgb, 3);
    return razer_effect_build(storage, effect, &opts, args);
}

size_t razer_effect_build(uint8_t storage, uint8_t effect, const struct razer_effect_opts *opts,
                          uint8_t args[RAZER_EFFECT_ARGS_MAX])
{
    memset(args, 0, RAZER_EFFECT_ARGS_MAX);
    args[0] = storage;
    args[2] = effect;
    unsigned colors = opts ? opts->colors : 0;
    if (colors > 2)
        return 0;

    switch (effect) {
    case RAZER_EFFECT_NONE:
    case RAZER_EFFECT_SPECTRUM:
        return 6;
    case RAZER_EFFECT_WAVE:
        args[3] = opts && opts->reverse ? 1 : 0;
        args[4] = opts && opts->speed ? opts->speed : RAZER_WAVE_SLOW;
        return 6;
    case RAZER_EFFECT_STATIC:
        if (colors < 1)
            return 0;
        args[5] = 0x01;
        memcpy(args + 6, opts->rgb[0], 3);
        return 9;
    case RAZER_EFFECT_BREATHING:
        if (colors == 0)
            return 6; // random colours
        args[3] = (uint8_t)colors;
        args[5] = (uint8_t)colors;
        memcpy(args + 6, opts->rgb[0], 3);
        if (colors == 2)
            memcpy(args + 9, opts->rgb[1], 3);
        return colors == 2 ? 12 : 9;
    default:
        return 0;
    }
}

size_t razer_mute_color_args(uint8_t storage, const uint8_t rgb[3], uint8_t args[RAZER_EFFECT_ARGS_MAX])
{
    size_t size = razer_effect_args(storage, RAZER_EFFECT_STATIC, rgb, args);
    if (size == 0)
        return 0;
    args[1] = RAZER_ZONE_MUTE;
    return size;
}

int razer_effect_lookup(const char *name, bool *reverse)
{
    static const struct { const char *name; int id; bool reverse; } effects[] = {
        { "none", RAZER_EFFECT_NONE, false }, { "static", RAZER_EFFECT_STATIC, false },
        { "breathing", RAZER_EFFECT_BREATHING, false }, { "spectrum", RAZER_EFFECT_SPECTRUM, false },
        { "wave", RAZER_EFFECT_WAVE, false }, { "wave-reverse", RAZER_EFFECT_WAVE, true },
    };
    for (size_t i = 0; i < sizeof effects / sizeof *effects; i++)
        if (name && strcmp(effects[i].name, name) == 0) {
            if (reverse)
                *reverse = effects[i].reverse;
            return effects[i].id;
        }
    return -1;
}

int razer_effect_id(const char *name)
{
    return razer_effect_lookup(name, nullptr);
}

const char *razer_status_name(uint8_t status, char buf[16])
{
    switch (status) {
    case RAZER_STATUS_BUSY: return "busy";
    case RAZER_STATUS_OK: return "ok";
    case RAZER_STATUS_FAILURE: return "failed";
    case RAZER_STATUS_TIMEOUT: return "timed out";
    case RAZER_STATUS_NOT_SUPPORTED: return "not supported";
    default:
        snprintf(buf, 16, "status 0x%02x", status);
        return buf;
    }
}
