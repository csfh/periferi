#include "xap.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/random.h>
#include <unistd.h>

#include "scan.h"

// Index = XAP effect ID, 0x00 to 0x30.
static const char *const effect_names[] = {
    "solid_color", "alphas_mods", "gradient_up_down", "gradient_left_right", "breathing",
    "band_sat", "band_val", "band_pinwheel_sat", "band_pinwheel_val", "band_spiral_sat",
    "band_spiral_val", "cycle_all", "cycle_left_right", "cycle_up_down", "cycle_out_in",
    "cycle_out_in_dual", "rainbow_moving_chevron", "cycle_pinwheel", "cycle_spiral",
    "dual_beacon", "rainbow_beacon", "rainbow_pinwheels", "raindrops", "jellybean_raindrops",
    "hue_breathing", "hue_pendulum", "hue_wave", "pixel_fractal", "pixel_flow", "pixel_rain",
    "typing_heatmap", "digital_rain", "solid_reactive_simple", "solid_reactive",
    "solid_reactive_wide", "solid_reactive_multiwide", "solid_reactive_cross",
    "solid_reactive_multicross", "solid_reactive_nexus", "solid_reactive_multinexus",
    "splash", "multisplash", "solid_splash", "solid_multisplash", "flower_blooming",
    "starlight", "starlight_dual_sat", "starlight_dual_hue", "riverflow",
};
static_assert(sizeof effect_names / sizeof *effect_names == 0x31, "XAP defines IDs 0x00-0x30");

const char *xap_effect_name(uint8_t id)
{
    return id < sizeof effect_names / sizeof *effect_names ? effect_names[id] : nullptr;
}

int xap_effect_id(const char *name)
{
    for (size_t i = 0; i < sizeof effect_names / sizeof *effect_names; i++)
        if (strcmp(effect_names[i], name) == 0)
            return (int)i;
    return -1;
}

int xap_open(const char *node)
{
    struct peri_device dev;
    if (!peri_read_device(node, &dev) || dev.usage_page != 0xFF51 || dev.usage != 0x58) {
        fprintf(stderr, "peri: %s is not an XAP interface (usage ff51:0058)\n", node);
        return -1;
    }

    char path[64];
    snprintf(path, sizeof path, "/dev/%s", node);
    int fd = open(path, O_RDWR | O_CLOEXEC);
    if (fd < 0)
        fprintf(stderr, "peri: %s: %s%s\n", path, strerror(errno),
                errno == EACCES ? " (try `peri setup`)" : "");
    return fd;
}

int xap_call(int fd, const uint8_t *route, size_t route_len,
             const uint8_t *payload, size_t payload_len, struct xap_reply *reply)
{
    if (route_len + payload_len > 61) // 64-byte report minus token and length
        return -1;

    // Valid tokens are 0x0100-0xFFFD; 0xFFFE and 0xFFFF are reserved.
    uint16_t token;
    do {
        if (getrandom(&token, sizeof token, 0) != (ssize_t)sizeof token) {
            perror("peri: getrandom");
            return -1;
        }
    } while (token < 0x0100 || token >= 0xFFFE);

    // The report is unnumbered, so hidraw wants a leading report ID 0 that it
    // strips before sending: 1 + 64 bytes. Layout: token (LE), length, route, payload.
    uint8_t request[1 + 64] = { 0 };
    request[1] = token & 0xFF;
    request[2] = token >> 8;
    request[3] = (uint8_t)(route_len + payload_len);
    memcpy(&request[4], route, route_len);
    if (payload_len > 0)
        memcpy(&request[4 + route_len], payload, payload_len);
    if (write(fd, request, sizeof request) != (ssize_t)sizeof request) {
        perror("peri: write");
        return -1;
    }

    // Reply: token (LE), flags, length, payload. Skip broadcasts and other tokens.
    struct pollfd pfd = { .fd = fd, .events = POLLIN };
    for (int attempt = 0; attempt < 10; attempt++) {
        if (poll(&pfd, 1, 500) <= 0)
            continue;
        uint8_t raw[64];
        ssize_t n = read(fd, raw, sizeof raw);
        if (n < 4 || (uint16_t)(raw[0] | raw[1] << 8) != token)
            continue;

        reply->flags = raw[2];
        reply->len = raw[3] < (size_t)n - 4 ? raw[3] : (size_t)n - 4;
        memcpy(reply->payload, &raw[4], reply->len);
        return 0;
    }
    fprintf(stderr, "peri: no reply within 5 seconds\n");
    return -1;
}

static constexpr uint8_t route_rgb_get[] = { 0x06, 0x04, 0x03 };
static constexpr uint8_t route_rgb_set[] = { 0x06, 0x04, 0x04 };

int xap_rgb_get(int fd, struct xap_rgb *cfg)
{
    struct xap_reply reply;
    if (xap_call(fd, route_rgb_get, sizeof route_rgb_get, nullptr, 0, &reply) < 0)
        return -1;
    if (!(reply.flags & 0x01) || reply.len < 7) {
        fprintf(stderr, "peri: reading the rgb config failed (flags %02x, %zu bytes)\n",
                reply.flags, reply.len);
        return -1;
    }
    *cfg = (struct xap_rgb){
        .enable = reply.payload[0] != 0,
        .mode = reply.payload[1],
        .hue = reply.payload[2],
        .sat = reply.payload[3],
        .val = reply.payload[4],
        .speed = reply.payload[5],
        .flags = reply.payload[6],
    };
    return 0;
}

static constexpr uint8_t route_rgb_effects[] = { 0x06, 0x04, 0x02 };

int xap_rgb_effects(int fd, uint64_t *mask)
{
    struct xap_reply reply;
    if (xap_call(fd, route_rgb_effects, sizeof route_rgb_effects, nullptr, 0, &reply) < 0)
        return -1;
    if (!(reply.flags & 0x01) || reply.len < 8) {
        fprintf(stderr, "peri: reading the effect list failed (flags %02x, %zu bytes)\n",
                reply.flags, reply.len);
        return -1;
    }
    *mask = 0;
    for (int i = 7; i >= 0; i--) // little-endian u64
        *mask = *mask << 8 | reply.payload[i];
    return 0;
}

static constexpr uint8_t route_rgb_save[] = { 0x06, 0x04, 0x05 };

int xap_rgb_save(int fd)
{
    struct xap_reply reply;
    if (xap_call(fd, route_rgb_save, sizeof route_rgb_save, nullptr, 0, &reply) < 0)
        return -1;
    if (!(reply.flags & 0x01)) {
        fprintf(stderr, "peri: the keyboard refused to save the rgb config (flags %02x)\n",
                reply.flags);
        return -1;
    }
    return 0;
}

int xap_rgb_set(int fd, const struct xap_rgb *cfg)
{
    const uint8_t payload[] = { cfg->enable, cfg->mode, cfg->hue, cfg->sat,
                                cfg->val, cfg->speed, cfg->flags };
    struct xap_reply reply;
    if (xap_call(fd, route_rgb_set, sizeof route_rgb_set, payload, sizeof payload, &reply) < 0)
        return -1;
    if (!(reply.flags & 0x01)) {
        fprintf(stderr, "peri: the keyboard rejected the rgb config (flags %02x)\n", reply.flags);
        return -1;
    }
    return 0;
}
