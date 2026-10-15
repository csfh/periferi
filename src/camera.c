#include "camera.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/videodev2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

// Reads only. A control SET joins this list in its own commit, with the first approved write.
static const unsigned long allowed[] = {
    VIDIOC_QUERYCAP,
    VIDIOC_QUERY_EXT_CTRL,
    VIDIOC_QUERYMENU,
    VIDIOC_G_CTRL,
};

bool peri_cam_ioctl_allowed(unsigned long request)
{
    for (size_t i = 0; i < sizeof allowed / sizeof *allowed; i++)
        if (allowed[i] == request)
            return true;
    return false;
}

int peri_cam_ioctl(int fd, unsigned long request, void *arg)
{
    if (!peri_cam_ioctl_allowed(request)) {
        errno = EPERM;
        return -1;
    }
    return ioctl(fd, request, arg);
}

void peri_cam_short_name(const char *kernel_name, char *out, size_t size)
{
    size_t n = 0;
    bool gap = false;
    for (const char *p = kernel_name; *p && n + 1 < size; p++) {
        if (isalnum((unsigned char)*p)) {
            if (gap && n > 0)
                out[n++] = '_';
            gap = false;
            if (n + 1 < size)
                out[n++] = (char)tolower((unsigned char)*p);
        } else {
            gap = true;
        }
    }
    if (size > 0)
        out[n] = '\0';
}

bool peri_cam_inactive(const struct peri_cam_ctrl *c)
{
    return (c->flags & V4L2_CTRL_FLAG_INACTIVE) != 0;
}

int peri_cam_pick(const int *index, const bool *capture, size_t count)
{
    int best = -1;
    for (size_t i = 0; i < count; i++)
        if (capture[i] && (best < 0 || index[i] < index[best]))
            best = (int)i;
    return best;
}

static bool read_int(const char *path, int *value)
{
    FILE *f = fopen(path, "r");
    bool ok = f && fscanf(f, "%d", value) == 1;
    if (f)
        fclose(f);
    return ok;
}

// The USB device directory of a hidraw node: <usb device>/<interface>/<hid device>, two levels up.
static bool usb_device_of_hidraw(const char *node, char *out, size_t size)
{
    char path[PATH_MAX], real[PATH_MAX];
    snprintf(path, sizeof path, "/sys/class/hidraw/%s/device", node);
    if (!realpath(path, real))
        return false;
    for (int i = 0; i < 2; i++) {
        char *slash = strrchr(real, '/');
        if (!slash)
            return false;
        *slash = '\0';
    }
    snprintf(out, size, "%s", real);
    return true;
}

int peri_cam_open(const char *hidraw_node)
{
    char usb[PATH_MAX];
    if (!usb_device_of_hidraw(hidraw_node, usb, sizeof usb)) {
        fprintf(stderr, "peri: %s: no such HID device\n", hidraw_node);
        return -1;
    }

    // The video nodes of that USB device: <usb device>/<interface> is their `device` link.
    struct { char path[PATH_MAX]; int index; bool capture; } cand[16];
    size_t n = 0;
    DIR *dir = opendir("/sys/class/video4linux");
    for (struct dirent *e; dir && n < 16 && (e = readdir(dir));) {
        if (strncmp(e->d_name, "video", 5) != 0)
            continue;
        char link[PATH_MAX], real[PATH_MAX];
        snprintf(link, sizeof link, "/sys/class/video4linux/%s/device", e->d_name);
        if (!realpath(link, real))
            continue;
        char *slash = strrchr(real, '/');
        if (!slash)
            continue;
        *slash = '\0';
        if (strcmp(real, usb) != 0)
            continue;
        snprintf(link, sizeof link, "/sys/class/video4linux/%s/index", e->d_name);
        if (!read_int(link, &cand[n].index))
            continue;
        snprintf(cand[n].path, sizeof cand[n].path, "/dev/%s", e->d_name);
        cand[n].capture = false;
        n++;
    }
    if (dir)
        closedir(dir);

    // Which of them can capture video: asked of the node itself (a query, nothing starts).
    for (size_t i = 0; i < n; i++) {
        int fd = open(cand[i].path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0)
            continue;
        struct v4l2_capability cap = { 0 };
        if (peri_cam_ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0) {
            uint32_t caps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS) ? cap.device_caps : cap.capabilities;
            cand[i].capture = (caps & V4L2_CAP_VIDEO_CAPTURE) != 0;
        }
        close(fd);
    }
    int index[16];
    bool capture[16];
    for (size_t i = 0; i < n; i++) {
        index[i] = cand[i].index;
        capture[i] = cand[i].capture;
    }
    int best = peri_cam_pick(index, capture, n);
    if (best < 0) {
        fprintf(stderr, "peri: %s: the device has no video capture node\n", hidraw_node);
        return -1;
    }

    int fd = open(cand[best].path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
        fprintf(stderr, "peri: %s: %s\n", cand[best].path, strerror(errno));
    return fd;
}

size_t peri_cam_list(int fd, struct peri_cam_ctrl *out, size_t max)
{
    size_t n = 0;
    struct v4l2_query_ext_ctrl q = { .id = V4L2_CTRL_FLAG_NEXT_CTRL };
    while (n < max && peri_cam_ioctl(fd, VIDIOC_QUERY_EXT_CTRL, &q) == 0) {
        uint32_t next = q.id | V4L2_CTRL_FLAG_NEXT_CTRL;
        bool plain = q.type == V4L2_CTRL_TYPE_INTEGER || q.type == V4L2_CTRL_TYPE_BOOLEAN
                     || q.type == V4L2_CTRL_TYPE_MENU;
        if (plain && !(q.flags & (V4L2_CTRL_FLAG_DISABLED | V4L2_CTRL_FLAG_HAS_PAYLOAD))) {
            struct v4l2_control g = { .id = q.id };
            if (peri_cam_ioctl(fd, VIDIOC_G_CTRL, &g) == 0) {
                struct peri_cam_ctrl *c = &out[n++];
                c->id = q.id;
                peri_cam_short_name(q.name, c->name, sizeof c->name);
                c->type = q.type;
                c->min = q.minimum;
                c->max = q.maximum;
                c->step = (int64_t)q.step;
                c->def = q.default_value;
                c->value = g.value;
                c->flags = q.flags;
            }
        }
        q = (struct v4l2_query_ext_ctrl){ .id = next };
    }
    return n;
}

bool peri_cam_status_text(const struct peri_cam_ctrl *ctrls, size_t count, char *out, size_t size)
{
    size_t used = 0;
    if (size > 0)
        out[0] = '\0';
    for (size_t i = 0; i < count; i++) {
        int n = snprintf(out + used, size - used, "%s%s=%lld", i ? " " : "", ctrls[i].name,
                         (long long)ctrls[i].value);
        if (n < 0 || (size_t)n >= size - used)
            return false;
        used += (size_t)n;
    }
    return true;
}
