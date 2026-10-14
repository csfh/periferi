#include "razer90_dev.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/hidraw.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "scan.h"

int razer90_open(const char *node)
{
    struct peri_device dev;
    if (!peri_read_device(node, &dev) || dev.vid != 0x1532 || dev.pid != 0x00C3
        || dev.usage_page != 0x0001 || dev.usage != 0x0002) {
        fprintf(stderr, "peri: %s is not a DeathAdder V3 Pro mouse interface (1532:00c3, usage 0001:0002)\n",
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

int razer90_call(int fd, uint8_t cls, uint8_t cmd, size_t data_size, const uint8_t *args,
                 struct razer90_reply *reply, uint8_t raw[RAZER90_HIDRAW_LEN], int *got)
{
    uint8_t frame[RAZER90_LEN];
    int rc = razer90_encode(frame, RAZER90_TRANSACTION_OPENRAZER, cls, cmd, args, data_size);
    if (rc < 0)
        return rc;

    uint8_t request[RAZER90_HIDRAW_LEN];
    razer90_hidraw(request, frame);

    *got = -1;
    memset(raw, 0, RAZER90_HIDRAW_LEN);
    if (ioctl(fd, HIDIOCSFEATURE(RAZER90_HIDRAW_LEN), request) < 0) {
        fprintf(stderr, "peri: set feature: %s\n", strerror(errno));
        return RAZER_ERR_IO;
    }

    // As with the Seiren: a read can still hold the answer to an earlier request (a reply that
    // names another command is not ours), and a busy status means ask again. If ours never
    // comes, say what was seen last.
    int last = RAZER_ERR_NO_REPLY;
    for (int attempt = 0; attempt < 50; attempt++) {
        nanosleep(&(struct timespec){ .tv_nsec = 31 * 1000 * 1000 }, nullptr);
        uint8_t buf[RAZER90_HIDRAW_LEN] = { 0x00 };
        int n = ioctl(fd, HIDIOCGFEATURE(RAZER90_HIDRAW_LEN), buf);
        *got = n;
        if (n > 0) // keep what came back, even when it is not a whole frame
            memcpy(raw, buf, (size_t)n < sizeof buf ? (size_t)n : sizeof buf);
        if (n != RAZER90_HIDRAW_LEN)
            continue;
        rc = razer90_decode(buf + 1, cls, cmd, reply);
        if (rc == RAZER_ERR_MISMATCH) {
            last = rc;
            continue;
        }
        // Status 0 is "new command" in the public reference: our own request echoed back before
        // the mouse has answered, so it is not an answer yet (if it never changes, the raw bytes
        // still show it).
        if (rc == 0 && (reply->status == RAZER_STATUS_BUSY || reply->status == 0x00))
            continue;
        return rc;
    }
    return last;
}

// Send one read and hand back its reply when the device says ok with at least `want` bytes.
static uint8_t last_status;

uint8_t razer90_last_status(void)
{
    return last_status;
}

static int query(int fd, uint8_t cls, uint8_t cmd, size_t size, const uint8_t *args, size_t want,
                 struct razer90_reply *reply)
{
    uint8_t raw[RAZER90_HIDRAW_LEN];
    int got;
    last_status = 0;
    int rc = razer90_call(fd, cls, cmd, size, args, reply, raw, &got);
    if (rc < 0)
        return rc;
    last_status = reply->status;
    return reply->status == RAZER_STATUS_OK && reply->len >= want ? 0 : RAZER_ERR_STATUS;
}

int razer90_firmware(int fd, uint8_t version[2])
{
    struct razer90_reply reply;
    int rc = query(fd, 0x00, 0x81, 2, nullptr, 2, &reply);
    if (rc == 0)
        memcpy(version, reply.args, 2);
    return rc;
}

int razer90_battery(int fd, uint8_t *raw)
{
    struct razer90_reply reply;
    int rc = query(fd, 0x07, 0x80, 2, nullptr, 2, &reply);
    if (rc == 0 && !razer90_battery_decode(&reply, raw))
        rc = RAZER_ERR_STATUS;
    return rc;
}

int razer90_charging(int fd, bool *charging)
{
    struct razer90_reply reply;
    int rc = query(fd, 0x07, 0x84, 2, nullptr, 2, &reply);
    if (rc == 0 && !razer90_charging_decode(&reply, charging))
        rc = RAZER_ERR_STATUS;
    return rc;
}

// Argument 0 is 00: the "no store" byte the public driver uses to read the V3 Pro's DPI.
int razer90_dpi(int fd, struct razer90_dpi *dpi)
{
    const uint8_t args[7] = { 0x00 };
    struct razer90_reply reply;
    int rc = query(fd, 0x04, 0x85, 7, args, 5, &reply);
    if (rc == 0 && !razer90_dpi_decode(&reply, dpi))
        rc = RAZER_ERR_STATUS;
    return rc;
}

int razer90_poll_rate(int fd, unsigned *hz)
{
    struct razer90_reply reply;
    int rc = query(fd, 0x00, 0x85, 1, nullptr, 1, &reply);
    if (rc == 0 && !razer90_poll_decode(&reply, hz))
        rc = RAZER_ERR_STATUS;
    return rc;
}
