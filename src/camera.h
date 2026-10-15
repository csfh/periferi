#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// The V4L2 engine: a webcam's controls (brightness, exposure, focus, zoom ...) through the kernel's
// V4L2 control ioctls. It never streams: every ioctl goes through peri_cam_ioctl(), which refuses
// anything not on a short allowlist of control queries, so the camera can never be told to start
// capturing, to set a format or to hand over a frame (a test checks the list). Nothing here reads
// or writes video data.

// Is this ioctl one the engine may issue? Control queries and reads only for now.
bool peri_cam_ioctl_allowed(unsigned long request);

// ioctl(2) on a video node, or -1 with errno EPERM when the request is not allowed.
int peri_cam_ioctl(int fd, unsigned long request, void *arg);

// "Exposure Time, Absolute" -> "exposure_time_absolute": lower case, every run of other characters
// one '_', none at either end. The names `v4l2-ctl` shows.
void peri_cam_short_name(const char *kernel_name, char *out, size_t size);

// One camera control as the camera describes it (min, max, step and default are the camera's own).
// `type` is a V4L2_CTRL_TYPE_*: only integer, boolean and menu controls are listed.
struct peri_cam_ctrl {
    uint32_t id;
    char name[40];
    uint32_t type;
    int64_t min, max, step, def, value;
    uint32_t flags; // V4L2_CTRL_FLAG_*; peri_cam_inactive() reads the one that matters
};

// True when the camera says the control cannot be set right now (exposure time while exposure is
// automatic, white-balance temperature while it is automatic, focus while autofocus is on).
bool peri_cam_inactive(const struct peri_cam_ctrl *c);

// Which of a device's video nodes is its main camera: the capture node with the lowest index. The
// BRIO's colour camera is index 0 and its infrared camera index 2, and both show the same user
// controls. A candidate that cannot capture video (the metadata nodes) is never chosen. Returns the
// position of the winner in `index`/`capture`, or -1 when none can capture.
int peri_cam_pick(const int *index, const bool *capture, size_t count);

// Open the main camera of the USB device that owns `hidraw_node` (a module's anchor interface, found
// through sysfs), for controls only. Returns an fd, or -1 after printing why.
int peri_cam_open(const char *hidraw_node);

// Every integer, boolean and menu control of the open camera with its current value, in the order
// the camera lists them. Returns how many (at most `max`).
size_t peri_cam_list(int fd, struct peri_cam_ctrl *out, size_t max);

// "brightness=128 contrast=128 ..." for `peri status`, a space between pairs. False if it does not
// fit in `size`.
bool peri_cam_status_text(const struct peri_cam_ctrl *ctrls, size_t count, char *out, size_t size);
