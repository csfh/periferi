#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// The V4L2 engine: a webcam's controls (brightness, exposure, focus, zoom ...) through the kernel's
// V4L2 control ioctls. It never streams: every ioctl goes through peri_cam_ioctl(), which refuses
// anything not on a short allowlist (control queries, control reads, one control write limited to a fixed
// list of standard controls, and reads and one-byte writes of one Logitech extension-unit selector), so the camera can
// never be told to start capturing, to set a format or to hand over a frame (a test checks the list).
// Nothing here reads or writes video data.

// Is this ioctl one the engine may issue? Control queries and reads, VIDIOC_S_CTRL (one control write,
// which peri_cam_ioctl() further limits to the standard controls in peri_cam_settable_id()) and
// UVCIOC_CTRL_QUERY (which peri_cam_ioctl() limits to GET reads and one-byte SET_CUR writes of the approved
// extension unit's selector 5).
bool peri_cam_ioctl_allowed(unsigned long request);

// Is this one of the standard V4L2 controls the engine may WRITE (brightness, exposure, focus, zoom,
// pan and tilt ...)? A fixed list, not "anything in the user or camera class": a driver can map a
// vendor extension-unit control into V4L2 under its own id, and a generic V4L2 write must never reach one
// (the BRIO's firmware and factory-reset functions live there). The one extension-unit control this program
// writes, the field of view, does not go through this list: it has its own narrow path (see below), routed
// by name, with its own checks.
bool peri_cam_settable_id(uint32_t id);

// ioctl(2) on a video node, or -1 with errno EPERM when the request is not allowed, is a control write
// to an id that is not settable, or is an extension-unit query other than a GET, or a one-byte SET_CUR within
// range, of the approved unit's selector 5 (see below).
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

// ---- the field-of-view extension unit (reads, and one narrow write) ------------------------------------
//
// The BRIO's field of view is not a V4L2 control: it is selector 5 of a Logitech extension unit (UVC
// vendor extension), found by the unit's GUID in the camera's own USB descriptor. The engine may read
// and write that one selector (UVCIOC_CTRL_QUERY) and nothing else: the unit must be the one a
// descriptor approved by GUID (never a hard-coded unit id), the selector must be 5, a read is any GET
// request, and a write is a one-byte SET_CUR of a value from 0 to PERI_CAM_FOV_MAX (the camera's own
// GET_MAX, read 2026-10-15). Every write needs the owner's OK on exact bytes.
// Logitech cameras carry other vendor units for firmware and test functions: they are never queried.

// The extension unit GUID of the field of view, in the byte order the descriptor stores it
// (49e40215-f434-47fe-b158-0e885023e51b as lsusb prints it).
extern const uint8_t PERI_CAM_FOV_GUID[16];
#define PERI_CAM_FOV_SELECTOR 5

// UVC class request codes the engine may send to the selector (all reads).
#define PERI_UVC_GET_CUR 0x81
#define PERI_UVC_GET_MIN 0x82
#define PERI_UVC_GET_MAX 0x83
#define PERI_UVC_GET_RES 0x84
#define PERI_UVC_GET_LEN 0x85
#define PERI_UVC_GET_INFO 0x86
#define PERI_UVC_GET_DEF 0x87
// The one write: set the current value.
#define PERI_UVC_SET_CUR 0x01
// The highest value the field-of-view control takes: this camera's own GET_MAX (three positions, 0-2).
#define PERI_CAM_FOV_MAX 2

// Find the extension unit with this GUID in a USB configuration descriptor blob (the sysfs
// `descriptors` file), inside a video-control interface only (an audio feature unit has the same
// descriptor subtype). On success `unit` is its unit id and `controls` its bmControls bitmap (bit n set
// means selector n+1 exists; up to 32 bits). False when there is no such unit or the blob is malformed.
bool peri_uvc_find_unit(const uint8_t *desc, size_t len, const uint8_t guid[16], uint8_t *unit, uint32_t *controls);

// Approve the field-of-view unit from a descriptor blob: the one place that sets which unit
// peri_cam_ioctl() lets a query reach. False (and nothing approved) when the GUID is not in the blob or
// the unit does not list selector 5.
bool peri_cam_fov_approve(const uint8_t *desc, size_t len);

// The same for the USB device that owns `hidraw_node`, reading its descriptors from sysfs.
bool peri_cam_fov_approve_device(const char *hidraw_node);

// One read of the approved unit's selector 5: `query` is one of the PERI_UVC_GET_* codes. `size` bytes
// are read into `data`: the kernel refuses (ENOBUFS) a size that is not the control's own length (read
// PERI_UVC_GET_LEN first: 2 bytes, little endian), and the first time it touches the control it sends GET_LEN
// and GET_INFO itself (both reads) before the query. 0 on success, -1 and errno otherwise (EPERM if the
// query is not allowed or no unit is approved; ENOENT, ENOBUFS and EBADRQC are the kernel's refusals).
int peri_cam_fov_read(int fd, uint8_t query, uint8_t *data, uint16_t size);

// The three positions as seen on a real BRIO (2026-10-15, the owner watching a preview): 0 is the widest view,
// 1 is more zoomed in, 2 is the most zoomed in (presumably 90, 78 and 65 degrees; the angles were not measured).
// Other cameras with this unit may order them differently: a name is only as good as the camera it was seen on.
const char *peri_cam_fov_name(int value);           // "wide", "medium", "narrow", or null
bool peri_cam_fov_parse(const char *text, uint8_t *value); // those names, or a whole number 0 to PERI_CAM_FOV_MAX
#define PERI_CAM_FOV_OPTIONS "0:Wide,1:Medium,2:Narrow" // as `peri controls` lists them for the pane

// Everything a write needs to know, read from the camera: the control's length (must be 1), its capabilities
// (GET_INFO), its own maximum and its current value. False if any read fails or the length is not 1.
// `writable` is true when GET_INFO says the control accepts a write and is not disabled right now.
struct peri_cam_fov_state {
    uint8_t info, max, cur;
    bool writable;
};
bool peri_cam_fov_state(int fd, struct peri_cam_fov_state *state);

// Write the field of view (0 to PERI_CAM_FOV_MAX) and read it back from the camera, which for an extension
// unit is a real query of the device, not a driver cache. 0 when the camera accepted the write and the value
// it reports now is in `readback`; -1 and errno otherwise (EPERM if no unit is approved or the value is out
// of range). Which value is which angle on a given camera is for the caller to find out.
int peri_cam_fov_write(int fd, uint8_t value, uint8_t *readback);

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
