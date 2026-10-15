#include <errno.h>
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

    if (failures == 0)
        printf("camera_test: all passed\n");
    return failures ? 1 : 0;
}
