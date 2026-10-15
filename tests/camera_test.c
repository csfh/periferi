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

    // The engine may query and read controls and nothing else: no streaming, no formats, no buffers,
    // and no control writes yet.
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
    check(!peri_cam_ioctl_allowed(VIDIOC_S_CTRL), "control write not enabled yet");
    check(!peri_cam_ioctl_allowed(VIDIOC_S_EXT_CTRLS), "extended control write not enabled yet");
    check(peri_cam_ioctl(-1, VIDIOC_STREAMON, nullptr) == -1, "a refused ioctl returns -1");

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
