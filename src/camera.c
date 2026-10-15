#include "camera.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/uvcvideo.h>
#include <linux/videodev2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

// Queries and reads, the one control write, and (argument-checked) reads and the one narrow write of the
// field of view. Never the extended write (VIDIOC_S_EXT_CTRLS: it takes
// several controls and compound payloads), and nothing that streams, sets a format or moves buffers.
static const unsigned long allowed[] = {
    VIDIOC_QUERYCAP,
    VIDIOC_QUERY_EXT_CTRL,
    VIDIOC_QUERYMENU,
    VIDIOC_G_CTRL,
    VIDIOC_S_CTRL,
    UVCIOC_CTRL_QUERY, // peri_cam_ioctl() lets only GET queries and a one-byte SET_CUR of the approved unit's selector 5 through
};

// The standard controls that may be written: each one the BRIO lists that a person would change.
static const uint32_t settable[] = {
    V4L2_CID_BRIGHTNESS,
    V4L2_CID_CONTRAST,
    V4L2_CID_SATURATION,
    V4L2_CID_AUTO_WHITE_BALANCE,
    V4L2_CID_GAIN,
    V4L2_CID_POWER_LINE_FREQUENCY,
    V4L2_CID_WHITE_BALANCE_TEMPERATURE,
    V4L2_CID_SHARPNESS,
    V4L2_CID_BACKLIGHT_COMPENSATION,
    V4L2_CID_EXPOSURE_AUTO,
    V4L2_CID_EXPOSURE_ABSOLUTE,
    V4L2_CID_EXPOSURE_AUTO_PRIORITY,
    V4L2_CID_PAN_ABSOLUTE,
    V4L2_CID_TILT_ABSOLUTE,
    V4L2_CID_FOCUS_ABSOLUTE,
    V4L2_CID_FOCUS_AUTO,
    V4L2_CID_ZOOM_ABSOLUTE,
};

// The descriptor bytes of the field of view's extension unit GUID, as stored (little-endian fields).
const uint8_t PERI_CAM_FOV_GUID[16] = { 0x15, 0x02, 0xe4, 0x49, 0x34, 0xf4, 0xfe, 0x47,
                                        0xb1, 0x58, 0x0e, 0x88, 0x50, 0x23, 0xe5, 0x1b };

// The unit peri_cam_fov_approve() approved, 0 when none. The only thing that lets a query through.
static uint8_t approved_unit;

static bool fov_query_ok(const struct uvc_xu_control_query *q)
{
    if (approved_unit == 0 || q->unit != approved_unit || q->selector != PERI_CAM_FOV_SELECTOR || q->data == nullptr)
        return false;
    if (q->query >= PERI_UVC_GET_CUR && q->query <= PERI_UVC_GET_DEF)
        return q->size >= 1 && q->size <= 4; // a read
    // The one write: a single byte, within the camera's own range.
    return q->query == PERI_UVC_SET_CUR && q->size == 1 && *q->data <= PERI_CAM_FOV_MAX;
}

bool peri_cam_settable_id(uint32_t id)
{
    for (size_t i = 0; i < sizeof settable / sizeof *settable; i++)
        if (settable[i] == id)
            return true;
    return false;
}

bool peri_cam_ioctl_allowed(unsigned long request)
{
    for (size_t i = 0; i < sizeof allowed / sizeof *allowed; i++)
        if (allowed[i] == request)
            return true;
    return false;
}

int peri_cam_ioctl(int fd, unsigned long request, void *arg)
{
    if (!peri_cam_ioctl_allowed(request)
        || (request == VIDIOC_S_CTRL && !peri_cam_settable_id(((struct v4l2_control *)arg)->id))
        || (request == UVCIOC_CTRL_QUERY && !fov_query_ok((const struct uvc_xu_control_query *)arg))) {
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

bool peri_cam_check_value(const struct peri_cam_ctrl *c, int64_t value, char *why, size_t size)
{
    if (!peri_cam_settable_id(c->id)) {
        snprintf(why, size, "%s is not a control this program sets", c->name);
        return false;
    }
    if (c->flags & V4L2_CTRL_FLAG_READ_ONLY) {
        snprintf(why, size, "%s is read-only", c->name);
        return false;
    }
    if (peri_cam_inactive(c)) {
        snprintf(why, size, "%s is inactive right now (another control, such as an automatic mode, holds it)", c->name);
        return false;
    }
    if (c->type == V4L2_CTRL_TYPE_BOOLEAN && value != 0 && value != 1) {
        snprintf(why, size, "%s takes 0 or 1", c->name);
        return false;
    }
    if (value < c->min || value > c->max) {
        snprintf(why, size, "%s must be from %lld to %lld", c->name, (long long)c->min, (long long)c->max);
        return false;
    }
    if (c->type == V4L2_CTRL_TYPE_INTEGER && c->step > 1 && (value - c->min) % c->step != 0) {
        snprintf(why, size, "%s moves in steps of %lld from %lld", c->name, (long long)c->step, (long long)c->min);
        return false;
    }
    return true;
}

const struct peri_cam_ctrl *peri_cam_find(const struct peri_cam_ctrl *ctrls, size_t count, const char *name)
{
    for (size_t i = 0; i < count; i++)
        if (strcmp(ctrls[i].name, name) == 0)
            return &ctrls[i];
    return nullptr;
}

int peri_cam_set(int fd, const struct peri_cam_ctrl *c, int64_t value, int64_t *readback)
{
    if (c->type == V4L2_CTRL_TYPE_MENU) { // a menu can have holes: the camera says which indices exist
        struct v4l2_querymenu m = { .id = c->id, .index = (uint32_t)value };
        if (peri_cam_ioctl(fd, VIDIOC_QUERYMENU, &m) != 0) {
            errno = EINVAL;
            return -1;
        }
    }
    struct v4l2_control w = { .id = c->id, .value = (int32_t)value };
    if (peri_cam_ioctl(fd, VIDIOC_S_CTRL, &w) != 0)
        return -1;
    struct v4l2_control r = { .id = c->id };
    if (peri_cam_ioctl(fd, VIDIOC_G_CTRL, &r) != 0)
        return -1;
    *readback = r.value;
    return 0;
}

bool peri_cam_describe(const struct peri_cam_ctrl *c, char *out, size_t size)
{
    const char *type = c->type == V4L2_CTRL_TYPE_BOOLEAN ? "bool" : c->type == V4L2_CTRL_TYPE_MENU ? "menu" : "int";
    char flags[4] = "";
    size_t n = 0;
    if (peri_cam_settable_id(c->id))
        flags[n++] = 's';
    if (peri_cam_inactive(c))
        flags[n++] = 'i';
    if (c->flags & V4L2_CTRL_FLAG_READ_ONLY)
        flags[n++] = 'r';
    if (n == 0)
        flags[n++] = '-';
    flags[n] = '\0';
    int len = snprintf(out, size, "%s:%lld:%lld:%lld:%lld:%s", type, (long long)c->min, (long long)c->max,
                       (long long)c->step, (long long)c->value, flags);
    return len > 0 && (size_t)len < size;
}

bool peri_cam_option_token(int index, const char *name, char *out, size_t size)
{
    int len = snprintf(out, size, "%d:", index);
    if (len < 0 || (size_t)len >= size)
        return false;
    size_t used = (size_t)len;
    for (const char *p = name; *p; p++) {
        if (used + 1 >= size)
            return false;
        out[used++] = (isalnum((unsigned char)*p) || *p == '-' || *p == '.') ? *p : '_';
    }
    out[used] = '\0';
    return true;
}

bool peri_cam_options(int fd, const struct peri_cam_ctrl *c, char *out, size_t size)
{
    if (size > 0)
        out[0] = '\0';
    if (c->type != V4L2_CTRL_TYPE_MENU)
        return true;
    size_t used = 0;
    for (int64_t i = c->min; i <= c->max; i++) {
        struct v4l2_querymenu m = { .id = c->id, .index = (uint32_t)i };
        if (peri_cam_ioctl(fd, VIDIOC_QUERYMENU, &m) != 0)
            continue; // a hole: the camera does not offer this index
        char token[64];
        if (!peri_cam_option_token((int)i, (const char *)m.name, token, sizeof token))
            return false;
        int n = snprintf(out + used, size - used, "%s%s", used ? "," : "", token);
        if (n < 0 || (size_t)n >= size - used)
            return false;
        used += (size_t)n;
    }
    return true;
}

bool peri_cam_controls_text(int fd, const struct peri_cam_ctrl *ctrls, size_t count, char *out, size_t size)
{
    size_t used = 0;
    if (size > 0)
        out[0] = '\0';
    for (size_t i = 0; i < count; i++) {
        char desc[64], opts[200];
        if (!peri_cam_describe(&ctrls[i], desc, sizeof desc) || !peri_cam_options(fd, &ctrls[i], opts, sizeof opts))
            return false;
        int n = snprintf(out + used, size - used, "%s%s=%s", used ? " " : "", ctrls[i].name, desc);
        if (n < 0 || (size_t)n >= size - used)
            return false;
        used += (size_t)n;
        if (opts[0]) {
            n = snprintf(out + used, size - used, " %s.options=%s", ctrls[i].name, opts);
            if (n < 0 || (size_t)n >= size - used)
                return false;
            used += (size_t)n;
        }
    }
    return true;
}

bool peri_uvc_find_unit(const uint8_t *desc, size_t len, const uint8_t guid[16], uint8_t *unit, uint32_t *controls)
{
    bool in_video_control = false;
    for (size_t i = 0; i + 2 <= len;) {
        size_t n = desc[i];
        if (n < 2 || i + n > len)
            return false; // a descriptor that runs off the end: the blob is malformed
        uint8_t type = desc[i + 1];
        if (type == 0x04 && n >= 9) // interface: class 0x0e, subclass 0x01 is video control
            in_video_control = desc[i + 5] == 0x0e && desc[i + 6] == 0x01;
        else if (type == 0x24 && in_video_control && n >= 24 && desc[i + 2] == 0x06) {
            // extension unit: id, 16-byte GUID, control count, input pins, their ids, bmControls size, bmControls
            size_t pins = desc[i + 21];
            if (22 + pins + 1 <= n) {
                size_t size = desc[i + 22 + pins];
                if (23 + pins + size <= n && memcmp(desc + i + 4, guid, 16) == 0) {
                    uint32_t bits = 0;
                    for (size_t b = 0; b < size && b < 4; b++)
                        bits |= (uint32_t)desc[i + 23 + pins + b] << (8 * b);
                    *unit = desc[i + 3];
                    *controls = bits;
                    return true;
                }
            }
        }
        i += n;
    }
    return false;
}

bool peri_cam_fov_approve(const uint8_t *desc, size_t len)
{
    uint8_t unit;
    uint32_t controls;
    approved_unit = 0;
    if (!peri_uvc_find_unit(desc, len, PERI_CAM_FOV_GUID, &unit, &controls) || unit == 0
        || !(controls & (1u << (PERI_CAM_FOV_SELECTOR - 1))))
        return false;
    approved_unit = unit;
    return true;
}

bool peri_cam_fov_approve_device(const char *hidraw_node)
{
    char usb[PATH_MAX], path[PATH_MAX + 16];
    if (!usb_device_of_hidraw(hidraw_node, usb, sizeof usb))
        return false;
    snprintf(path, sizeof path, "%s/descriptors", usb);
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    static uint8_t blob[256 * 1024];
    size_t got = fread(blob, 1, sizeof blob, f);
    fclose(f);
    return peri_cam_fov_approve(blob, got);
}

int peri_cam_fov_read(int fd, uint8_t query, uint8_t *data, uint16_t size)
{
    struct uvc_xu_control_query q = { .unit = approved_unit, .selector = PERI_CAM_FOV_SELECTOR,
                                      .query = query, .size = size, .data = data };
    return peri_cam_ioctl(fd, UVCIOC_CTRL_QUERY, &q);
}

int peri_cam_fov_write(int fd, uint8_t value, uint8_t *readback)
{
    struct uvc_xu_control_query w = { .unit = approved_unit, .selector = PERI_CAM_FOV_SELECTOR,
                                      .query = PERI_UVC_SET_CUR, .size = 1, .data = &value };
    if (peri_cam_ioctl(fd, UVCIOC_CTRL_QUERY, &w) != 0)
        return -1;
    return peri_cam_fov_read(fd, PERI_UVC_GET_CUR, readback, 1);
}

