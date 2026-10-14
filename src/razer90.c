// The long Razer frame: see razer90.h for the layout. Nothing here opens a device.

#include "razer90.h"

#include <string.h>

// Checksum over bytes 2..87, which leaves out the status and the transaction id.
static uint8_t crc(const uint8_t frame[RAZER90_LEN])
{
    uint8_t x = 0;
    for (size_t i = 2; i <= 87; i++)
        x ^= frame[i];
    return x;
}

int razer90_encode(uint8_t frame[RAZER90_LEN], uint8_t transaction, uint8_t cls, uint8_t cmd,
                   const uint8_t *args, size_t len)
{
    if (len > RAZER90_ARGS_MAX)
        return RAZER_ERR_ARGS;

    memset(frame, 0, RAZER90_LEN);
    frame[1] = transaction;
    frame[5] = (uint8_t)len;
    frame[6] = cls;
    frame[7] = cmd;
    if (args)
        memcpy(frame + 8, args, len);
    frame[88] = crc(frame);
    return 0;
}

int razer90_decode(const uint8_t frame[RAZER90_LEN], uint8_t cls, uint8_t cmd,
                   struct razer90_reply *reply)
{
    if (frame[88] != crc(frame))
        return RAZER_ERR_CRC;
    if (frame[6] != cls || frame[7] != cmd)
        return RAZER_ERR_MISMATCH;
    if (frame[5] > RAZER90_ARGS_MAX)
        return RAZER_ERR_SIZE;

    reply->status = frame[0];
    reply->transaction = frame[1];
    reply->len = frame[5];
    memcpy(reply->args, frame + 8, reply->len);
    return 0;
}

void razer90_hidraw(uint8_t buf[RAZER90_HIDRAW_LEN], const uint8_t frame[RAZER90_LEN])
{
    buf[0] = 0x00;
    memcpy(buf + 1, frame, RAZER90_LEN);
}
