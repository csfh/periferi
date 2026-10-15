#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// The V4L2 engine: a webcam's controls (brightness, exposure, focus, zoom ...) through the kernel's
// V4L2 control ioctls. It never streams: every ioctl goes through peri_cam_ioctl(), which refuses
// anything not on a short allowlist (control queries, control reads, one control write limited to a fixed
// list of standard controls), so the camera can never be told to start capturing, to set a format or to
// hand over a frame (a test checks the list). Nothing here reads
// or writes video data.

// Is this ioctl one the engine may issue? Control queries and reads, and VIDIOC_S_CTRL (one control
// write), which peri_cam_ioctl() further limits to the standard controls in peri_cam_settable_id().
bool peri_cam_ioctl_allowed(unsigned long request);

// Is this one of the standard V4L2 controls the engine may WRITE (brightness, exposure, focus, zoom,
// pan and tilt ...)? A fixed list, not "anything in the user or camera class": a driver can map a
// vendor extension-unit control into V4L2 under its own id, and a generic set must never reach one
// (the BRIO's field of view, firmware and factory-reset functions live there).
bool peri_cam_settable_id(uint32_t id);

// ioctl(2) on a video node, or -1 with errno EPERM when the request is not allowed, or is a control
// write to an id that is not settable.
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

// May `value` be written to this control right now? False, with a short reason in `why`, when the id
// is not settable, the control is read-only or inactive, or the value is out of range, off the step
// or not a boolean 0/1. (A menu's holes are checked against the camera by peri_cam_set().)
bool peri_cam_check_value(const struct peri_cam_ctrl *c, int64_t value, char *why, size_t size);

// The control with this short name, or null.
const struct peri_cam_ctrl *peri_cam_find(const struct peri_cam_ctrl *ctrls, size_t count, const char *name);

// Write one control of the open camera and read it back. 0 when the camera accepted it; the value it
// reports now is in `readback` (it may differ: a driver can round or ignore). -1 and errno otherwise.
// A menu index the camera does not offer is refused before anything is written.
int peri_cam_set(int fd, const struct peri_cam_ctrl *c, int64_t value, int64_t *readback);

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

// What the panel needs to draw one control: "<type>:<min>:<max>:<step>:<value>:<flags>", for example
// "int:0:255:1:128:s". type is int, bool or menu; flags are letters, in this order: s the control may be
// set by this program, i it is inactive right now, r it is read-only; "-" for none. False if it does not fit.
bool peri_cam_describe(const struct peri_cam_ctrl *c, char *out, size_t size);

// One menu option as a token without spaces: (1, "Manual Mode") -> "1:Manual_Mode". Anything that is not a
// letter, digit, '-' or '.' becomes '_'. False if it does not fit.
bool peri_cam_option_token(int index, const char *name, char *out, size_t size);

// The options a menu control offers, "1:Manual_Mode,3:Aperture_Priority_Mode", comma separated, only the
// indices the camera really has (a menu can have holes). Empty for a control that is not a menu.
bool peri_cam_options(int fd, const struct peri_cam_ctrl *c, char *out, size_t size);

// The whole reply body of `peri controls`: "name=<describe> ... name.options=<options> ...", a space
// between tokens, an `.options` token after each menu control. False if it does not fit in `size`.
bool peri_cam_controls_text(int fd, const struct peri_cam_ctrl *ctrls, size_t count, char *out, size_t size);

// "brightness=128 contrast=128 ..." for `peri status`, a space between pairs. False if it does not
// fit in `size`.
bool peri_cam_status_text(const struct peri_cam_ctrl *ctrls, size_t count, char *out, size_t size);
