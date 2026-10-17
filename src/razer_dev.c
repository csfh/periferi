#include "razer_dev.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/hidraw.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "scan.h"

int razer_open(const char *node)
{
    struct peri_device dev;
    if (!peri_read_device(node, &dev) || dev.vid != 0x1532 || dev.pid != 0x056F
        || dev.usage_page != 0xFF53 || dev.usage != 0x04) {
        fprintf(stderr, "peri: %s is not a Seiren V3 Chroma control interface (1532:056f, usage ff53:0004)\n",
                node);
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

int razer_call(int fd, uint8_t cls, uint8_t cmd, size_t data_size, const uint8_t *args,
               struct razer_reply *reply, uint8_t raw[RAZER_REPORT_LEN], int *got)
{
    uint8_t request[RAZER_REPORT_LEN];
    int rc = razer_encode(request, RAZER_TRANSACTION_DEFAULT, cls, cmd, args, data_size);
    if (rc < 0)
        return rc;

    *got = -1;
    memset(raw, 0, RAZER_REPORT_LEN);
    if (ioctl(fd, HIDIOCSFEATURE(RAZER_REPORT_LEN), request) < 0) {
        fprintf(stderr, "peri: set feature: %s\n", strerror(errno));
        return RAZER_ERR_IO;
    }

    // The device answers a moment later; keep asking until it does (or says busy no more). The first
    // read can still hold the answer to an earlier request (they all share one transaction id), which
    // names another command: that is not ours, so wait for ours. If ours never comes, say what was seen.
    int last = RAZER_ERR_NO_REPLY;
    for (int attempt = 0; attempt < 50; attempt++) {
        nanosleep(&(struct timespec){ .tv_nsec = 20 * 1000 * 1000 }, nullptr);
        uint8_t buf[RAZER_REPORT_LEN] = { RAZER_REPORT_ID };
        int n = ioctl(fd, HIDIOCGFEATURE(RAZER_REPORT_LEN), buf);
        *got = n;
        if (n > 0) // keep what came back, even when it is not a whole frame
            memcpy(raw, buf, (size_t)n < sizeof buf ? (size_t)n : sizeof buf);
        if (n != RAZER_REPORT_LEN)
            continue;
        rc = razer_decode(buf, cls, cmd, reply);
        if (rc == RAZER_ERR_MISMATCH) {
            last = rc;
            continue;
        }
        if (rc == 0 && reply->status == RAZER_STATUS_BUSY)
            continue;
        return rc;
    }
    return last;
}

static uint8_t last_status;

uint8_t razer_last_status(void)
{
    return last_status;
}

static int query(int fd, uint8_t cls, uint8_t cmd, size_t size, const uint8_t *args, size_t want,
                 struct razer_reply *reply)
{
    uint8_t raw[RAZER_REPORT_LEN];
    int got;
    last_status = 0;
    int rc = razer_call(fd, cls, cmd, size, args, reply, raw, &got);
    if (rc < 0)
        return rc;
    last_status = reply->status;
    return reply->status == RAZER_STATUS_OK && reply->len >= want ? 0 : RAZER_ERR_STATUS;
}

int razer_firmware(int fd, uint8_t version[2])
{
    struct razer_reply reply;
    int rc = query(fd, 0x00, 0x81, 2, nullptr, 2, &reply);
    if (rc == 0)
        memcpy(version, reply.args, 2);
    return rc;
}

// Arguments: variable storage, LED id 0, and a zero placeholder for the value when asking.
int razer_brightness(int fd, uint8_t storage, uint8_t *brightness)
{
    const uint8_t args[3] = { storage, 0x00, 0x00 };
    struct razer_reply reply;
    int rc = query(fd, 0x0f, 0x84, 3, args, 3, &reply);
    if (rc == 0)
        *brightness = reply.args[2];
    return rc;
}

int razer_set_brightness(int fd, uint8_t storage, uint8_t brightness)
{
    const uint8_t args[3] = { storage, 0x00, brightness };
    struct razer_reply reply;
    return query(fd, 0x0f, 0x04, 3, args, 0, &reply);
}

int razer_set_mute_color(int fd, uint8_t storage, const uint8_t rgb[3])
{
    uint8_t args[RAZER_EFFECT_ARGS_MAX];
    size_t size = razer_mute_color_args(storage, rgb, args);
    if (size == 0)
        return RAZER_ERR_ARGS;
    struct razer_reply reply;
    return query(fd, 0x0f, 0x02, size, args, 0, &reply);
}

int razer_set_effect_opts(int fd, uint8_t storage, uint8_t effect, const struct razer_effect_opts *opts)
{
    uint8_t args[RAZER_EFFECT_ARGS_MAX];
    size_t size = razer_effect_build(storage, effect, opts, args);
    if (size == 0)
        return RAZER_ERR_ARGS;
    struct razer_reply reply;
    return query(fd, 0x0f, 0x02, size, args, 0, &reply);
}
