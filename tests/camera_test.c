#include <errno.h>
#include <linux/uvcvideo.h>
#include <linux/videodev2.h>
#include <stdio.h>
#include <string.h>

#include "../src/camera.h"

static int failures;

static void check(bool ok, const char *what)
{
    if (!ok) {
        printf("FAIL %s\n", what);
        failures++;
    }
}

static const char *short_name(const char *name)
{
    static char buf[64];
    peri_cam_short_name(name, buf, sizeof buf);
    return buf;
}

// A USB configuration descriptor blob shaped like the BRIO's: a video-control interface with two
// extension units (14 and 10, the second the field of view with bmControls ba 6f), then an audio-control
// interface whose feature unit carries the same GUID bytes as a decoy. `fov_bits` replaces unit 10's
// first bmControls byte, so a test can take selector 5 away.
static size_t build_descriptor(uint8_t *out, uint8_t fov_bits, bool with_fov_unit)
{
    size_t n = 0;
    static const uint8_t other[16] = { 0x6a, 0xd1, 0x49, 0x2c, 0xb8, 0x32, 0x85, 0x44,
                                       0x3e, 0xa8, 0x64, 0x3a, 0x15, 0x23, 0x62, 0xf2 };
    const uint8_t config[] = { 9, 0x02, 0, 0, 2, 1, 0, 0x80, 0xfa };
    const uint8_t vc_if[] = { 9, 0x04, 0, 0, 1, 0x0e, 0x01, 0, 0 };
    const uint8_t vc_hdr[] = { 13, 0x24, 0x01, 0x00, 0x01, 0, 0, 0, 0, 0, 0, 1, 1 };
    memcpy(out + n, config, sizeof config); n += sizeof config;
    memcpy(out + n, vc_if, sizeof vc_if); n += sizeof vc_if;
    memcpy(out + n, vc_hdr, sizeof vc_hdr); n += sizeof vc_hdr;
    // unit 14: 6 controls, one input pin (source 2), bmControls 3f 00
    out[n++] = 27; out[n++] = 0x24; out[n++] = 0x06; out[n++] = 14;
    memcpy(out + n, other, 16); n += 16;
    out[n++] = 6; out[n++] = 1; out[n++] = 2; out[n++] = 2; out[n++] = 0x3f; out[n++] = 0x00; out[n++] = 0;
    if (with_fov_unit) {
        // unit 10: 11 controls, one input pin (source 5), bmControls <fov_bits> 6f
        out[n++] = 27; out[n++] = 0x24; out[n++] = 0x06; out[n++] = 10;
        memcpy(out + n, PERI_CAM_FOV_GUID, 16); n += 16;
        out[n++] = 11; out[n++] = 1; out[n++] = 5; out[n++] = 2; out[n++] = fov_bits; out[n++] = 0x6f; out[n++] = 0;
    }
    const uint8_t ac_if[] = { 9, 0x04, 1, 0, 0, 0x01, 0x01, 0, 0 };
    memcpy(out + n, ac_if, sizeof ac_if); n += sizeof ac_if;
    // an audio-control "feature unit" (subtype 6) laid out like an extension unit, same GUID bytes
    out[n++] = 27; out[n++] = 0x24; out[n++] = 0x06; out[n++] = 99;
    memcpy(out + n, PERI_CAM_FOV_GUID, 16); n += 16;
    out[n++] = 11; out[n++] = 1; out[n++] = 5; out[n++] = 2; out[n++] = 0xff; out[n++] = 0xff; out[n++] = 0;
    return n;
}

int main(void)
{
    // The kernel's names become the ones v4l2-ctl shows (as read from the real BRIO).
    check(strcmp(short_name("Brightness"), "brightness") == 0, "plain name");
    check(strcmp(short_name("Exposure Time, Absolute"), "exposure_time_absolute") == 0, "comma and space");
    check(strcmp(short_name("Focus, Automatic Continuous"), "focus_automatic_continuous") == 0, "two words");
    check(strcmp(short_name("White Balance, Automatic"), "white_balance_automatic") == 0, "white balance");
    check(strcmp(short_name("Auto Exposure"), "auto_exposure") == 0, "auto exposure");
    check(strcmp(short_name("Power Line Frequency"), "power_line_frequency") == 0, "power line");
    check(strcmp(short_name("  -- Gain! "), "gain") == 0, "no leading or trailing underscore");
    char tiny[6];
    peri_cam_short_name("Brightness", tiny, sizeof tiny);
    check(strcmp(tiny, "brigh") == 0, "a short buffer is cut, never overrun");

    // The engine may query and read controls and write one control: no streaming, no formats, no
    // buffers, no extended (multi-control) writes.
    check(peri_cam_ioctl_allowed(VIDIOC_QUERYCAP), "querycap allowed");
    check(peri_cam_ioctl_allowed(VIDIOC_QUERY_EXT_CTRL), "query ext ctrl allowed");
    check(peri_cam_ioctl_allowed(VIDIOC_QUERYMENU), "querymenu allowed");
    check(peri_cam_ioctl_allowed(VIDIOC_G_CTRL), "get control allowed");
    check(!peri_cam_ioctl_allowed(VIDIOC_STREAMON), "streamon refused");
    check(!peri_cam_ioctl_allowed(VIDIOC_STREAMOFF), "streamoff refused");
    check(!peri_cam_ioctl_allowed(VIDIOC_REQBUFS), "reqbufs refused");
    check(!peri_cam_ioctl_allowed(VIDIOC_QBUF), "qbuf refused");
    check(!peri_cam_ioctl_allowed(VIDIOC_DQBUF), "dqbuf refused");
    check(!peri_cam_ioctl_allowed(VIDIOC_S_FMT), "set format refused");
    check(!peri_cam_ioctl_allowed(VIDIOC_S_PARM), "set parameters refused");
    check(peri_cam_ioctl_allowed(VIDIOC_S_CTRL), "single control write allowed");
    check(!peri_cam_ioctl_allowed(VIDIOC_S_EXT_CTRLS), "extended control write refused");
    check(!peri_cam_ioctl_allowed(VIDIOC_G_EXT_CTRLS), "extended control read is not needed");
    check(peri_cam_ioctl(-1, VIDIOC_STREAMON, nullptr) == -1 && errno == EPERM, "a refused ioctl is EPERM");

    // A control write is limited to the standard controls on the fixed list: a vendor control that a
    // driver maps into V4L2 under its own id (here a private-base id) can never be written through
    // peri_cam_ioctl(), whatever the caller passes. A listed id gets as far as the (closed) fd.
    check(peri_cam_settable_id(V4L2_CID_BRIGHTNESS) && peri_cam_settable_id(V4L2_CID_ZOOM_ABSOLUTE)
              && peri_cam_settable_id(V4L2_CID_EXPOSURE_AUTO_PRIORITY),
          "standard controls are settable");
    check(!peri_cam_settable_id(V4L2_CID_PRIVATE_BASE) && !peri_cam_settable_id(V4L2_CID_PRIVATE_BASE + 1)
              && !peri_cam_settable_id(V4L2_CID_PAN_RELATIVE) && !peri_cam_settable_id(0)
              && !peri_cam_settable_id(V4L2_CID_HUE) && !peri_cam_settable_id(V4L2_CID_GAMMA),
          "anything else is not settable");
    struct v4l2_control vendor = { .id = V4L2_CID_PRIVATE_BASE + 5, .value = 1 };
    errno = 0;
    check(peri_cam_ioctl(-1, VIDIOC_S_CTRL, &vendor) == -1 && errno == EPERM, "a non-standard control write is refused");
    struct v4l2_control standard = { .id = V4L2_CID_BRIGHTNESS, .value = 128 };
    errno = 0;
    check(peri_cam_ioctl(-1, VIDIOC_S_CTRL, &standard) == -1 && errno == EBADF, "a standard control write reaches the fd");

    // The main camera is the capture node with the lowest index (the BRIO: colour 0, infrared 2,
    // metadata nodes 1 and 3).
    const int index[] = { 3, 0, 1, 2 };
    const bool capture[] = { false, true, false, true };
    check(peri_cam_pick(index, capture, 4) == 1, "lowest capturing index wins");
    const int idx2[] = { 2, 0 };
    const bool cap2[] = { true, false };
    check(peri_cam_pick(idx2, cap2, 2) == 0, "a node that cannot capture is never chosen");
    const bool none[] = { false, false };
    check(peri_cam_pick(idx2, none, 2) == -1, "no capture node");
    check(peri_cam_pick(idx2, none, 0) == -1, "no candidates");

    // What may be written, checked against the BRIO's real ranges (read 2026-10-15).
    struct peri_cam_ctrl brightness = { .id = V4L2_CID_BRIGHTNESS, .name = "brightness", .type = V4L2_CTRL_TYPE_INTEGER,
                                        .min = 0, .max = 255, .step = 1, .def = 128, .value = 128 };
    struct peri_cam_ctrl pan = { .id = V4L2_CID_PAN_ABSOLUTE, .name = "pan_absolute", .type = V4L2_CTRL_TYPE_INTEGER,
                                 .min = -36000, .max = 36000, .step = 3600, .value = 10800 };
    struct peri_cam_ctrl autowb = { .id = V4L2_CID_AUTO_WHITE_BALANCE, .name = "white_balance_automatic",
                                    .type = V4L2_CTRL_TYPE_BOOLEAN, .min = 0, .max = 1, .step = 1, .value = 1 };
    struct peri_cam_ctrl exposure = { .id = V4L2_CID_EXPOSURE_ABSOLUTE, .name = "exposure_time_absolute",
                                      .type = V4L2_CTRL_TYPE_INTEGER, .min = 3, .max = 2047, .step = 1,
                                      .flags = V4L2_CTRL_FLAG_INACTIVE };
    struct peri_cam_ctrl odd = { .id = V4L2_CID_PRIVATE_BASE + 2, .name = "vendor_thing", .type = V4L2_CTRL_TYPE_INTEGER,
                                 .min = 0, .max = 9, .step = 1 };
    struct peri_cam_ctrl locked = brightness;
    locked.flags = V4L2_CTRL_FLAG_READ_ONLY;
    char why[160];
    check(peri_cam_check_value(&brightness, 0, why, sizeof why) && peri_cam_check_value(&brightness, 255, why, sizeof why),
          "range ends are fine");
    check(!peri_cam_check_value(&brightness, 256, why, sizeof why) && strstr(why, "0 to 255"), "above the range");
    check(!peri_cam_check_value(&brightness, -1, why, sizeof why), "below the range");
    check(peri_cam_check_value(&pan, 7200, why, sizeof why) && peri_cam_check_value(&pan, -36000, why, sizeof why),
          "a value on the step");
    check(!peri_cam_check_value(&pan, 7000, why, sizeof why) && strstr(why, "3600"), "a value off the step is refused");
    check(peri_cam_check_value(&autowb, 0, why, sizeof why) && !peri_cam_check_value(&autowb, 2, why, sizeof why),
          "a boolean takes 0 or 1");
    check(!peri_cam_check_value(&exposure, 250, why, sizeof why) && strstr(why, "inactive"), "an inactive control is refused");
    check(!peri_cam_check_value(&odd, 1, why, sizeof why) && strstr(why, "not a control this program sets"),
          "a control off the list is refused");
    check(!peri_cam_check_value(&locked, 100, why, sizeof why) && strstr(why, "read-only"), "a read-only control is refused");
    const struct peri_cam_ctrl list[] = { brightness, pan };
    check(peri_cam_find(list, 2, "pan_absolute") == &list[1] && peri_cam_find(list, 2, "zoom_absolute") == nullptr,
          "find by name");

    // What the panel is told about each control.
    char desc[64];
    check(peri_cam_describe(&brightness, desc, sizeof desc) && strcmp(desc, "int:0:255:1:128:s") == 0, "describe an integer");
    check(peri_cam_describe(&autowb, desc, sizeof desc) && strcmp(desc, "bool:0:1:1:1:s") == 0, "describe a boolean");
    check(peri_cam_describe(&exposure, desc, sizeof desc) && strcmp(desc, "int:3:2047:1:0:si") == 0, "describe an inactive control");
    check(peri_cam_describe(&locked, desc, sizeof desc) && strcmp(desc, "int:0:255:1:128:sr") == 0, "describe a read-only control");
    check(peri_cam_describe(&odd, desc, sizeof desc) && strcmp(desc, "int:0:9:1:0:-") == 0, "describe a control that may not be set");
    check(peri_cam_describe(&pan, desc, sizeof desc) && strcmp(desc, "int:-36000:36000:3600:10800:s") == 0, "describe a stepped control");
    check(!peri_cam_describe(&pan, desc, 10), "a description that does not fit is refused");
    char tok[40];
    check(peri_cam_option_token(3, "Aperture Priority Mode", tok, sizeof tok) && strcmp(tok, "3:Aperture_Priority_Mode") == 0, "option token");
    check(peri_cam_option_token(1, "60 Hz", tok, sizeof tok) && strcmp(tok, "1:60_Hz") == 0, "option with a number");
    check(peri_cam_option_token(0, "Wi-Fi: on/off!", tok, sizeof tok) && strcmp(tok, "0:Wi-Fi__on_off_") == 0, "odd characters become underscores");
    check(!peri_cam_option_token(2, "Aperture Priority Mode", tok, 8), "an option token that does not fit is refused");
    char all[200];
    struct peri_cam_ctrl two[2] = { brightness, autowb };
    check(peri_cam_controls_text(-1, two, 2, all, sizeof all)
              && strcmp(all, "brightness=int:0:255:1:128:s white_balance_automatic=bool:0:1:1:1:s") == 0,
          "the controls reply body");
    check(!peri_cam_controls_text(-1, two, 2, all, 40), "a controls reply that does not fit is refused");

    // The status line, and that it refuses to cut a pair in half.
    struct peri_cam_ctrl c[2] = { { .name = "brightness", .value = 128 }, { .name = "pan_absolute", .value = -3600 } };
    char text[64];
    check(peri_cam_status_text(c, 2, text, sizeof text) && strcmp(text, "brightness=128 pan_absolute=-3600") == 0,
          "status text");
    char small[20];
    check(!peri_cam_status_text(c, 2, small, sizeof small), "status text that does not fit is refused");
    c[0].flags = V4L2_CTRL_FLAG_INACTIVE;
    check(peri_cam_inactive(&c[0]) && !peri_cam_inactive(&c[1]), "inactive flag");

    // ---- the field-of-view extension unit: found by GUID in the descriptor, reads and one narrow write --
    uint8_t blob[512];
    size_t blen = build_descriptor(blob, 0xba, true);
    uint8_t unit = 0;
    uint32_t bits = 0;
    check(peri_uvc_find_unit(blob, blen, PERI_CAM_FOV_GUID, &unit, &bits) && unit == 10 && bits == 0x6fba,
          "the field-of-view unit is found by its GUID, not by a fixed id");
    const uint8_t stranger[16] = { 1, 2, 3 };
    check(!peri_uvc_find_unit(blob, blen, stranger, &unit, &bits), "an unknown GUID is not found");
    // Malformed blobs are refused and never read past their end (these tests run under ASan).
    // The layout: config 9 + video interface 9 + header 13, so unit 14 starts at 31 and unit 10 at 58.
    check(!peri_uvc_find_unit(blob, 58 + 10, PERI_CAM_FOV_GUID, &unit, &bits), "a blob cut inside the unit is not found");
    check(peri_uvc_find_unit(blob, blen - 3, PERI_CAM_FOV_GUID, &unit, &bits) && unit == 10,
          "a blob cut after the unit still has it");
    uint8_t cut[512];
    memcpy(cut, blob, blen);
    cut[31] = 200; // unit 14's bLength claims far more than is left
    check(!peri_uvc_find_unit(cut, blen, PERI_CAM_FOV_GUID, &unit, &bits), "an overlong descriptor length is refused");
    cut[31] = 0; // a zero length would loop forever if it were trusted
    check(!peri_uvc_find_unit(cut, blen, PERI_CAM_FOV_GUID, &unit, &bits), "a zero descriptor length is refused");
    check(!peri_uvc_find_unit(blob, 0, PERI_CAM_FOV_GUID, &unit, &bits), "an empty blob has nothing");
    uint8_t no_fov[512];
    size_t nlen = build_descriptor(no_fov, 0xba, false);
    check(!peri_uvc_find_unit(no_fov, nlen, PERI_CAM_FOV_GUID, &unit, &bits),
          "an audio unit with the same GUID bytes is not a video extension unit");

    // Before any approval nothing reaches the camera, whatever is asked.
    uint8_t buf[2] = { 0 };
    struct uvc_xu_control_query q = { .unit = 10, .selector = PERI_CAM_FOV_SELECTOR, .query = PERI_UVC_GET_CUR,
                                      .size = 1, .data = buf };
    errno = 0;
    check(peri_cam_ioctl(-1, UVCIOC_CTRL_QUERY, &q) == -1 && errno == EPERM, "no unit approved: refused");
    check(peri_cam_fov_read(-1, PERI_UVC_GET_CUR, buf, 1) == -1 && errno == EPERM, "no unit approved: fov read refused");

    // A descriptor without selector 5 approves nothing (bmControls 0xaa has bit 4 clear).
    size_t weak = build_descriptor(blob, 0xaa, true);
    check(!peri_cam_fov_approve(blob, weak), "a unit without selector 5 is not approved");
    errno = 0;
    check(peri_cam_fov_read(-1, PERI_UVC_GET_CUR, buf, 1) == -1 && errno == EPERM, "still refused");

    blen = build_descriptor(blob, 0xba, true);
    check(peri_cam_fov_approve(blob, blen), "the real layout approves unit 10");

    // Approved: only GET queries of selector 5 on that unit, one to four bytes, get as far as the fd.
    const uint8_t gets[] = { PERI_UVC_GET_CUR, PERI_UVC_GET_MIN, PERI_UVC_GET_MAX, PERI_UVC_GET_RES,
                             PERI_UVC_GET_LEN, PERI_UVC_GET_INFO, PERI_UVC_GET_DEF };
    for (size_t i = 0; i < sizeof gets; i++) {
        errno = 0;
        check(peri_cam_fov_read(-1, gets[i], buf, 1) == -1 && errno == EBADF, "an approved read reaches the fd");
    }
    // Everything else is refused before the fd is touched.
    q.unit = 10; q.selector = 5; q.size = 1; q.data = buf;
    // The one write, SET_CUR (0x01): a single byte from 0 to the camera's own max (2) reaches the fd...
    for (uint8_t v = 0; v <= PERI_CAM_FOV_MAX; v++) {
        buf[0] = v;
        q.query = PERI_UVC_SET_CUR;
        errno = 0;
        check(peri_cam_ioctl(-1, UVCIOC_CTRL_QUERY, &q) == -1 && errno == EBADF, "an in-range write reaches the fd");
    }
    // ...and nothing else does: a value past the max, two bytes, no buffer.
    buf[0] = PERI_CAM_FOV_MAX + 1;
    errno = 0;
    check(peri_cam_ioctl(-1, UVCIOC_CTRL_QUERY, &q) == -1 && errno == EPERM, "a value past the max is refused");
    buf[0] = 255;
    errno = 0;
    check(peri_cam_ioctl(-1, UVCIOC_CTRL_QUERY, &q) == -1 && errno == EPERM, "255 is refused");
    buf[0] = 1;
    q.size = 2;
    errno = 0;
    check(peri_cam_ioctl(-1, UVCIOC_CTRL_QUERY, &q) == -1 && errno == EPERM, "a two-byte write is refused");
    q.size = 1;
    q.selector = 4;
    errno = 0;
    check(peri_cam_ioctl(-1, UVCIOC_CTRL_QUERY, &q) == -1 && errno == EPERM, "a write to another selector is refused");
    q.selector = 5; q.unit = 14;
    errno = 0;
    check(peri_cam_ioctl(-1, UVCIOC_CTRL_QUERY, &q) == -1 && errno == EPERM, "a write to another unit is refused");
    q.unit = 10; q.data = nullptr;
    errno = 0;
    check(peri_cam_ioctl(-1, UVCIOC_CTRL_QUERY, &q) == -1 && errno == EPERM, "a write with no buffer is refused");
    q.data = buf;
    errno = 0;
    check(peri_cam_fov_write(-1, PERI_CAM_FOV_MAX + 1, buf) == -1 && errno == EPERM, "peri_cam_fov_write refuses a value past the max");
    errno = 0;
    check(peri_cam_fov_write(-1, 1, buf) == -1 && errno == EBADF, "peri_cam_fov_write sends an in-range value");
    q.query = 0x02; // SET_MIN does not exist, and a code between SET_CUR and the GETs is refused
    errno = 0;
    check(peri_cam_ioctl(-1, UVCIOC_CTRL_QUERY, &q) == -1 && errno == EPERM, "another request code is refused");
    q.query = PERI_UVC_GET_CUR;
    q.query = 0x88;
    errno = 0;
    check(peri_cam_ioctl(-1, UVCIOC_CTRL_QUERY, &q) == -1 && errno == EPERM, "a request code past GET_DEF is refused");
    q.query = 0x80;
    errno = 0;
    check(peri_cam_ioctl(-1, UVCIOC_CTRL_QUERY, &q) == -1 && errno == EPERM, "a request code before GET_CUR is refused");
    q.query = PERI_UVC_GET_CUR;
    q.selector = 4;
    errno = 0;
    check(peri_cam_ioctl(-1, UVCIOC_CTRL_QUERY, &q) == -1 && errno == EPERM, "another selector is refused");
    q.selector = 5; q.unit = 14;
    errno = 0;
    check(peri_cam_ioctl(-1, UVCIOC_CTRL_QUERY, &q) == -1 && errno == EPERM, "another unit is refused");
    q.unit = 10; q.size = 0;
    errno = 0;
    check(peri_cam_ioctl(-1, UVCIOC_CTRL_QUERY, &q) == -1 && errno == EPERM, "a zero-length query is refused");
    q.size = 5;
    errno = 0;
    check(peri_cam_ioctl(-1, UVCIOC_CTRL_QUERY, &q) == -1 && errno == EPERM, "an oversized query is refused");
    q.size = 1; q.data = nullptr;
    errno = 0;
    check(peri_cam_ioctl(-1, UVCIOC_CTRL_QUERY, &q) == -1 && errno == EPERM, "a query with no buffer is refused");

    if (failures == 0)
        printf("camera_test: all passed\n");
    return failures ? 1 : 0;
}
