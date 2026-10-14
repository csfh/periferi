// Razer vendor frames: see razer.h for the layout. Nothing here opens a device.

#include "razer.h"

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
    memset(args, 0, RAZER_EFFECT_ARGS_MAX);
    args[0] = storage;
    args[2] = effect;

    switch (effect) {
    case RAZER_EFFECT_NONE:
    case RAZER_EFFECT_SPECTRUM:
        return 6;
    case RAZER_EFFECT_BREATHING:
        if (!rgb)
            return 6; // random colours
        args[3] = 0x01; // one colour
        break;
    case RAZER_EFFECT_STATIC:
        if (!rgb)
            return 0;
        break;
    default:
        return 0;
    }
    args[5] = 0x01;
    memcpy(args + 6, rgb, 3);
    return 9;
}

bool razer_event_decode(const uint8_t *report, size_t len, struct razer_event *ev)
{
    *ev = (struct razer_event){ RAZER_EVENT_NONE, 0 };
    if (len < 3 || report[0] != 0x05)
        return false;
    if (report[1] == 0x11 && report[2] <= 1) {
        *ev = (struct razer_event){ RAZER_EVENT_MUTE, report[2] };
        return true;
    }
    if (report[1] == 0x0f) {
        *ev = (struct razer_event){ RAZER_EVENT_DIAL, report[2] };
        return true;
    }
    return false;
}

void razer_state_apply(struct razer_state *state, const struct razer_event *ev)
{
    if (ev->kind == RAZER_EVENT_MUTE)
        state->mute = ev->value;
    else if (ev->kind == RAZER_EVENT_DIAL)
        state->dial = ev->value;
}
