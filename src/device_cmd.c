// Daemon side of "peri status" and "peri set": find the device an installed module drives, then
// read it or change its running brightness. The only frames sent are the ones in razer_dev.h.

#include "device_cmd.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <linux/videodev2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "audio.h"
#include "controls.h"
#include "module.h"
#include "camera.h"
#include "razer90_dev.h"
#include "razer_dev.h"
#include "request.h"
#include "scan.h"
#include "store.h"

// What went wrong with a frame the device was sent, in the device's own words: its status if it
// answered ("refused: not supported"), else that it did not answer or could not be written to.
static void device_said(char *buf, size_t size, int rc)
{
    char tmp[16];
    if (rc == RAZER_ERR_STATUS)
        snprintf(buf, size, "the device answered \"%s\"", razer_status_name(razer_last_status(), tmp));
    else if (rc == RAZER_ERR_NO_REPLY)
        snprintf(buf, size, "the device did not answer");
    else if (rc == RAZER_ERR_IO)
        snprintf(buf, size, "the frame could not be written to the device");
    else
        snprintf(buf, size, "the device sent a reply that did not check out (%d)", rc);
}

static void err(char *reply, size_t size, const char *what, const char *id)
{
    snprintf(reply, size, "err %s: %s\n", id, what);
}

// Resolve an installed module id to its module and the hidraw node of its connected device.
// False, with an "err ..." reply, if the id is bad, not installed, not connected or ambiguous.
static bool resolve_module(const char *id, struct peri_module *module, char *node, size_t node_size,
                           char *reply, size_t size)
{
    if (!peri_module_id_valid(id)) {
        snprintf(reply, size, "err usage: <module-id> (see `peri modules`)\n");
        return false;
    }

    char dir[PATH_MAX];
    struct peri_module installed[32];
    size_t installed_count = peri_installed_dir(dir, sizeof dir)
                                 ? peri_modules_scan(dir, installed, 32) : 0;
    struct peri_device devs[32];
    size_t dev_count = peri_scan(devs, 32);

    const struct peri_module *m;
    switch (peri_module_select(devs, dev_count, installed, installed_count, id, node, node_size,
                               &m)) {
    case PERI_SELECT_OK:
        break;
    case PERI_SELECT_NOT_INSTALLED:
        err(reply, size, "no such installed module; run `peri use <id>` (see `peri modules`)", id);
        return false;
    case PERI_SELECT_NOT_CONNECTED:
        err(reply, size, "the device is not connected", id);
        return false;
    case PERI_SELECT_AMBIGUOUS:
        err(reply, size, "more than one device matches; choosing one is not supported yet", id);
        return false;
    }
    *module = *m;
    return true;
}

// Open the Seiren-style device of a resolved module. Returns an fd, or -1 with an "err ..." reply.
static int open_razer(const struct peri_module *m, const char *node, char *reply, size_t size)
{
    if (strcmp(m->engine, "razer") != 0) {
        err(reply, size,
            strcmp(m->engine, "razer90") == 0 ? "this is not a Seiren-style module (use `peri status` or `peri set dpi=`/`poll=`)"
                                              : "this module has nothing to read or set yet",
            m->id);
        return -1;
    }
    int fd = razer_open(node);
    if (fd < 0)
        err(reply, size, "cannot open the device (see `journalctl --user -u perid`)", m->id);
    return fd;
}

// Resolve a module id to an open Seiren-style device. Returns an fd, or -1 with an "err ..." reply.
static int open_module_device(const char *id, struct peri_module *module, char *reply, size_t size)
{
    char node[32];
    if (!resolve_module(id, module, node, sizeof node, reply, size))
        return -1;
    return open_razer(module, node, reply, size);
}

static void first_word(const char *args, char *out, size_t size)
{
    snprintf(out, size, "%s", args);
    out[strcspn(out, " \n")] = '\0';
}

// The DeathAdder's status: firmware, battery, charging, DPI and poll rate, one read each. It stops
// at the first read the mouse does not answer at all (a sleeping mouse takes about 1.5 s per
// read, and the daemon serves one request at a time), and a read it answers with something other
// than ok shows as "unknown". The firmware bytes are shown raw: what they mean is not established.
static void status_razer90(const char *id, const char *node, char *reply, size_t size)
{
    int fd = razer90_open(node);
    if (fd < 0) {
        err(reply, size, "cannot open the device (see `journalctl --user -u perid`)", id);
        return;
    }
    uint8_t firmware[2], battery;
    bool charging;
    struct razer90_dpi dpi;
    unsigned hz;

    int rc = razer90_firmware(fd, firmware);
    if (rc == RAZER_ERR_NO_REPLY || rc == RAZER_ERR_IO) {
        close(fd);
        err(reply, size, "the device did not answer (a sleeping mouse may need a click first)", id);
        return;
    }
    char fw[8] = "unknown", bat[8] = "unknown", chg[8] = "unknown", dp[16] = "unknown", pr[8] = "unknown";
    if (rc == 0)
        snprintf(fw, sizeof fw, "%02x%02x", firmware[0], firmware[1]);

    // Each later read is skipped once one has gone unanswered.
    bool asleep = false;
    rc = razer90_battery(fd, &battery);
    asleep = rc == RAZER_ERR_NO_REPLY;
    if (rc == 0)
        snprintf(bat, sizeof bat, "%u", battery);
    if (!asleep) {
        rc = razer90_charging(fd, &charging);
        asleep = rc == RAZER_ERR_NO_REPLY;
        if (rc == 0)
            snprintf(chg, sizeof chg, "%s", charging ? "yes" : "no");
    }
    if (!asleep) {
        rc = razer90_dpi(fd, &dpi);
        asleep = rc == RAZER_ERR_NO_REPLY;
        if (rc == 0)
            snprintf(dp, sizeof dp, "%u:%u", dpi.x, dpi.y);
    }
    if (!asleep) {
        rc = razer90_poll_rate(fd, &hz);
        if (rc == 0)
        {
            if (hz)
                snprintf(pr, sizeof pr, "%u", hz);
            // else it stays "unknown": a code this program does not know (a faster rate)
        }
    }
    close(fd);
    snprintf(reply, size, "ok module=%s firmware=%s battery=%s charging=%s dpi=%s poll=%s\n", id, fw,
             bat, chg, dp, pr);
}

// The field of view as one more control, so the pane draws it from the same list as the V4L2 ones: for
// `peri controls` the full "field_of_view=menu:0:2:1:<value>:s field_of_view.options=0:Wide,1:Medium,2:Narrow",
// for `peri status` just "field_of_view=<value>". Nothing is appended when the camera's descriptor has no such
// unit or its state cannot be read. Reads only. The flag is "s" only when the camera says it can be written.
static void append_fov(int fd, const char *node, char *text, size_t size, bool detailed)
{
    struct peri_cam_fov_state st;
    // Only when the camera's range is the three positions the names and options describe.
    if (!peri_cam_fov_approve_device(node) || !peri_cam_fov_state(fd, &st) || st.max != PERI_CAM_FOV_MAX)
        return;
    size_t used = strlen(text);
    int n;
    if (detailed)
        n = snprintf(text + used, size - used, "%sfield_of_view=menu:0:%u:1:%u:%s field_of_view.options=%s", used ? " " : "",
                     st.max, st.cur, st.writable ? "s" : "r", PERI_CAM_FOV_OPTIONS);
    else
        n = snprintf(text + used, size - used, "%sfield_of_view=%u", used ? " " : "", st.cur);
    if (n < 0 || (size_t)n >= size - used)
        text[used] = '\0'; // it did not fit: leave the text as it was, never half a token
}

// A camera's status: the current value of every control it lists, by the names `v4l2-ctl` shows.
// Reads only (control queries), and the video nodes are never asked to stream.
static void status_camera(const char *id, const char *node, char *reply, size_t size)
{
    int fd = peri_cam_open(node);
    if (fd < 0) {
        err(reply, size, "cannot open the camera (see `journalctl --user -u perid`)", id);
        return;
    }
    struct peri_cam_ctrl ctrls[64];
    size_t count = peri_cam_list(fd, ctrls, 64);
    char text[768];
    bool listed = count > 0 && peri_cam_status_text(ctrls, count, text, sizeof text);
    if (listed)
        append_fov(fd, node, text, sizeof text, false);
    close(fd);
    if (!listed) {
        err(reply, size, count == 0 ? "the camera listed no controls" : "the camera's controls do not fit in one reply", id);
        return;
    }
    snprintf(reply, size, "ok module=%s %s\n", id, text);
}

void controls_handle(const char *args, char *reply, size_t size)
{
    char id[64];
    first_word(args, id, sizeof id);
    char node[32];
    struct peri_module m;
    if (!resolve_module(id, &m, node, sizeof node, reply, size))
        return;
    if (strcmp(m.engine, "v4l2") != 0) {
        err(reply, size, "only a camera module has controls to list", id);
        return;
    }
    int fd = peri_cam_open(node);
    if (fd < 0) {
        err(reply, size, "cannot open the camera (see `journalctl --user -u perid`)", id);
        return;
    }
    struct peri_cam_ctrl ctrls[64];
    size_t count = peri_cam_list(fd, ctrls, 64);
    char text[960];
    bool fits = count > 0 && peri_cam_controls_text(fd, ctrls, count, text, sizeof text);
    if (fits)
        append_fov(fd, node, text, sizeof text, true);
    close(fd);
    if (!fits) {
        err(reply, size, count == 0 ? "the camera listed no controls" : "the camera's controls do not fit in one reply", id);
        return;
    }
    snprintf(reply, size, "ok module=%s %s\n", m.id, text);
}

// `fov <module-id> set <wide|medium|narrow|0-2>`: write the field of view. Reads first, and writes nothing unless every check
// passes: the control must report itself writable and not disabled (GET_INFO bits 1 and 2), be one byte long
// (GET_LEN), allow the value (GET_MAX) and have a current value to put back (GET_CUR). A write of the value
// it already has is not sent. The reply carries `before` (the restore target) and the camera's own read-back,
// which for an extension unit is a real query of the device. Which value is which angle is not known.
static void fov_set(const char *id, const char *value_text, char *reply, size_t size)
{
    uint8_t value;
    if (!peri_cam_fov_parse(value_text, &value)) {
        err(reply, size, "the value must be wide, medium or narrow (or 0, 1 or 2)", id);
        return;
    }
    char node[32];
    struct peri_module m;
    if (!resolve_module(id, &m, node, sizeof node, reply, size))
        return;
    if (strcmp(m.engine, "v4l2") != 0) {
        err(reply, size, "only a camera module has a field of view", id);
        return;
    }
    if (!peri_cam_fov_approve_device(node)) {
        err(reply, size, "this camera's descriptor has no field-of-view unit with selector 5", id);
        return;
    }
    int fd = peri_cam_open(node);
    if (fd < 0) {
        err(reply, size, "cannot open the camera (see `journalctl --user -u perid`)", id);
        return;
    }
    struct peri_cam_fov_state st;
    if (!peri_cam_fov_state(fd, &st)) {
        close(fd);
        err(reply, size, "the control could not be read (length, capabilities, maximum and current value), so nothing was written", id);
        return;
    }
    if (!st.writable) {
        close(fd);
        err(reply, size, "the camera says this control cannot be written right now (read-only or disabled), so nothing was written", id);
        return;
    }
    if (value > st.max) {
        close(fd);
        err(reply, size, "the value is above the camera's own maximum, so nothing was written", id);
        return;
    }
    if (st.cur == value) {
        close(fd);
        snprintf(reply, size, "ok module=%s fov=set sent=none name=%s before=%u readback=%u note=already_at_that_value\n", m.id,
                 peri_cam_fov_name(value), st.cur, st.cur);
        return;
    }
    uint8_t after = 0;
    int rc = peri_cam_fov_write(fd, value, &after);
    int saved_errno = errno;
    close(fd);
    if (rc != 0) {
        char text[200];
        snprintf(text, sizeof text, "the camera did not accept the change: %s", strerror(saved_errno));
        err(reply, size, text, id);
        return;
    }
    snprintf(reply, size, "ok module=%s fov=set sent=field_of_view:%u name=%s before=%u readback=%u info=%02x stored=maybe\n", m.id,
             value, peri_cam_fov_name(value), st.cur, after, st.info);
}

void fov_handle(const char *args, char *reply, size_t size)
{
    static const struct { const char *name; uint8_t code; uint16_t size; } queries[] = {
        { "cur", PERI_UVC_GET_CUR, 1 }, { "min", PERI_UVC_GET_MIN, 1 }, { "max", PERI_UVC_GET_MAX, 1 },
        { "res", PERI_UVC_GET_RES, 1 }, { "len", PERI_UVC_GET_LEN, 2 }, { "info", PERI_UVC_GET_INFO, 1 },
        { "def", PERI_UVC_GET_DEF, 1 },
    };
    char line[PERI_REQUEST_MAX + 1], *rest;
    snprintf(line, sizeof line, "%s", args);
    const char *id = strtok_r(line, " \n", &rest);
    const char *query = strtok_r(nullptr, " \n", &rest);
    const char *want = strtok_r(nullptr, " \n", &rest);
    if (!id || !query || strtok_r(nullptr, " \n", &rest)) {
        snprintf(reply, size, "err usage: fov <module-id> <cur|min|max|res|len|info|def> [size 1-4] | set <wide|medium|narrow|0-2>\n");
        return;
    }
    if (strcmp(query, "set") == 0) {
        fov_set(id, want, reply, size);
        return;
    }
    size_t q = 0;
    while (q < sizeof queries / sizeof *queries && strcmp(queries[q].name, query))
        q++;
    if (q == sizeof queries / sizeof *queries) {
        err(reply, size, "query must be cur, min, max, res, len, info or def", id);
        return;
    }
    uint16_t bytes = queries[q].size;
    if (want) {
        char *end;
        unsigned long v = strtoul(want, &end, 10);
        if (*end != '\0' || v < 1 || v > 4) {
            err(reply, size, "size must be a whole number from 1 to 4", id);
            return;
        }
        bytes = (uint16_t)v;
    }

    char node[32];
    struct peri_module m;
    if (!resolve_module(id, &m, node, sizeof node, reply, size))
        return;
    if (strcmp(m.engine, "v4l2") != 0) {
        err(reply, size, "only a camera module has a field of view", id);
        return;
    }
    if (!peri_cam_fov_approve_device(node)) {
        err(reply, size, "this camera's descriptor has no field-of-view unit with selector 5", id);
        return;
    }
    int fd = peri_cam_open(node);
    if (fd < 0) {
        err(reply, size, "cannot open the camera (see `journalctl --user -u perid`)", id);
        return;
    }
    uint8_t data[4] = { 0 };
    int rc = peri_cam_fov_read(fd, queries[q].code, data, bytes);
    int saved_errno = errno;
    close(fd);
    if (rc != 0) {
        // What the kernel's refusals mean, so a report of what went out is right (uvc_xu_ctrl_query): ENOENT is an
        // unknown unit or selector on this video node's chain, or the kernel's own first reads failing; ENOBUFS is
        // a size that is not the control's length (its first reads went out, the query did not); EBADRQC is a query
        // the control does not support.
        char text[240];
        if (saved_errno == ENOENT)
            snprintf(text, sizeof text, "this video node has no such control (or the kernel's first reads of it failed), and the %s query was not sent", query);
        else if (saved_errno == ENOBUFS)
            snprintf(text, sizeof text, "the size is not the control's length, so the %s query was not sent (read `len` first)", query);
        else if (saved_errno == EBADRQC)
            snprintf(text, sizeof text, "the control does not support the %s query", query);
        else
            snprintf(text, sizeof text, "the camera did not answer the %s query: %s", query, strerror(saved_errno));
        err(reply, size, text, id);
        return;
    }
    char hex[16] = "";
    for (uint16_t i = 0; i < bytes; i++)
        snprintf(hex + 2 * i, sizeof hex - 2 * i, "%02x", data[i]);
    snprintf(reply, size, "ok module=%s query=%s size=%u bytes=%s\n", m.id, query, bytes, hex);
}

void status_handle(const char *args, char *reply, size_t size)
{
    char id[64];
    first_word(args, id, sizeof id);

    char node[32];
    struct peri_module m;
    if (!resolve_module(id, &m, node, sizeof node, reply, size))
        return;
    if (strcmp(m.engine, "razer90") == 0) {
        status_razer90(m.id, node, reply, size);
        return;
    }
    if (strcmp(m.engine, "v4l2") == 0) {
        status_camera(m.id, node, reply, size);
        return;
    }
    int fd = open_razer(&m, node, reply, size);
    if (fd < 0)
        return;
    uint8_t firmware[2], brightness;
    bool ok = razer_firmware(fd, firmware) == 0
              && razer_brightness(fd, RAZER_STORE_FLASH, &brightness) == 0;
    close(fd);
    if (!ok) {
        err(reply, size, "the device did not answer a read query", id);
        return;
    }
    // The firmware bytes are shown raw: what they mean is not established. Brightness is the
    // value the device keeps (storage 1), not necessarily the one it is showing right now. Mute
    // and dial are what the mic last reported to the daemon, "unknown" until it does.
    struct razer_state controls = controls_state(m.id);
    char dial[8] = "unknown";
    if (controls.dial >= 0)
        snprintf(dial, sizeof dial, "%d", controls.dial);
    snprintf(reply, size, "ok module=%s firmware=%02x%02x brightness=%u mute=%s dial=%s\n", m.id,
             firmware[0], firmware[1], brightness,
             controls.mute < 0 ? "unknown" : controls.mute ? "unmuted" : "muted", dial);
}

// "0000ff" -> {0x00, 0x00, 0xff}. Exactly six hex digits.
static bool parse_color(const char *s, uint8_t rgb[3])
{
    if (strlen(s) != 6)
        return false;
    for (int i = 0; i < 6; i++)
        if (!isxdigit((unsigned char)s[i]))
            return false;
    for (int i = 0; i < 3; i++) {
        char pair[3] = { s[2 * i], s[2 * i + 1], '\0' };
        rgb[i] = (uint8_t)strtoul(pair, nullptr, 16);
    }
    return true;
}

// An effect name and up to two colours (hex text, or null) as the options the frame builder takes.
// False if the name is unknown, a colour is not six hex digits, a second colour has no first, or
// the effect lacks what it needs (static needs a colour). `speed` is "slow", "fast" or null, and
// only the wave effects use it. *effect is the effect id.
static bool effect_opts(const char *name, const char *color, const char *color2, const char *speed,
                        struct razer_effect_opts *opts, int *effect)
{
    bool reverse = false;
    *effect = razer_effect_lookup(name, &reverse);
    *opts = (struct razer_effect_opts){ .reverse = reverse };
    if (*effect < 0 || (color2 && !color))
        return false;
    if (color && !parse_color(color, opts->rgb[0]))
        return false;
    if (color2 && !parse_color(color2, opts->rgb[1]))
        return false;
    opts->colors = (uint8_t)(color ? (color2 ? 2 : 1) : 0);
    opts->speed = speed && strcmp(speed, "fast") == 0 ? RAZER_WAVE_FAST : RAZER_WAVE_SLOW; // wave only
    uint8_t args[RAZER_EFFECT_ARGS_MAX];
    return razer_effect_build(0, (uint8_t)*effect, opts, args) != 0;
}

// set <module-id> brightness=<0-255>
// set <module-id> effect=<name> [color=<rrggbb>] [color2=<rrggbb>]
// One kind of change per request. The device cannot be asked what its effect or colour is, so
// the reply only says what was sent.
// Is this installed module one for `engine`? Decided from the installed list alone, so the
// request can be routed before any device is looked at.
static bool installed_engine_is(const char *id, const char *engine)
{
    struct peri_module m;
    return peri_installed_get(id, &m) == PERI_INSTALLED_OK && strcmp(m.engine, engine) == 0;
}

// `set <module-id> <control>=<value>` for a camera: one standard control per request, named as `peri
// status` shows it (`field_of_view` is the exception: it is not a V4L2 control and goes to fov_set(), the
// checked extension-unit write, with a name or number as its value). The control must be on the settable
// list, not read-only or inactive, and the value in
// range and on the control's step; the value now is read first and reported as "before", the value to put
// back. The camera's ok proves nothing about the effect, so the value is read back and reported as it says.
// Camera settings may be kept by the camera (the BRIO keeps its pan), so every set may be a stored write.
static void set_camera(const char *id, char *rest, char *reply, size_t size)
{
    static const char usage[] = "err usage: set <module-id> <control>=<value> (names as in `peri status <module-id>`)\n";
    const char *tok = strtok_r(nullptr, " \n", &rest);
    if (!tok || strtok_r(nullptr, " \n", &rest)) {
        snprintf(reply, size, "%s", usage);
        return;
    }
    const char *eq = strchr(tok, '=');
    char name[40];
    if (!eq || eq == tok || (size_t)(eq - tok) >= sizeof name || eq[1] == '\0') {
        snprintf(reply, size, "%s", usage);
        return;
    }
    snprintf(name, sizeof name, "%.*s", (int)(eq - tok), tok);
    if (strcmp(name, "field_of_view") == 0) { // not a V4L2 control: its own checked write
        fov_set(id, eq + 1, reply, size);
        return;
    }
    char *end;
    long long value = strtoll(eq + 1, &end, 10);
    if (*end != '\0') {
        err(reply, size, "the value must be a whole number", id);
        return;
    }

    char node[32];
    struct peri_module m;
    if (!resolve_module(id, &m, node, sizeof node, reply, size))
        return;
    int fd = peri_cam_open(node);
    if (fd < 0) {
        err(reply, size, "cannot open the camera (see `journalctl --user -u perid`)", id);
        return;
    }
    struct peri_cam_ctrl ctrls[64];
    size_t count = peri_cam_list(fd, ctrls, 64);
    const struct peri_cam_ctrl *c = peri_cam_find(ctrls, count, name);
    char why[160];
    if (!c) {
        close(fd);
        err(reply, size, "no such control (names are in `peri status`)", id);
        return;
    }
    if (!peri_cam_check_value(c, value, why, sizeof why)) {
        close(fd);
        err(reply, size, why, id);
        return;
    }
    long long before = c->value;
    int64_t after = 0;
    int rc = peri_cam_set(fd, c, value, &after);
    int saved_errno = errno;
    close(fd);
    if (rc != 0) {
        char text[200];
        if (c->type == V4L2_CTRL_TYPE_MENU && saved_errno == EINVAL)
            snprintf(text, sizeof text, "the camera does not offer that option for %s, and nothing was sent", name);
        else
            snprintf(text, sizeof text, "the camera did not accept the change: %s", strerror(saved_errno));
        err(reply, size, text, id);
        return;
    }
    snprintf(reply, size, "ok module=%s sent=%s:%lld before=%lld device=ok readback=%lld stored=maybe\n", m.id,
             name, value, before, (long long)after);
}

// `set <module-id> dpi=<n>` or `poll=<125|500|1000>` for the DeathAdder: one setting per request.
// The current value is read first and reported as "before", the value to put back, and no write
// is made without it. The DPI set may be kept in the mouse's own memory (razer90.h). The device's
// ok proves nothing about the effect, so the value is read back and reported as the device says.
static void set_razer90(const char *id, char *rest, char *reply, size_t size)
{
    static const char usage[] = "err usage: set <module-id> dpi=<n> | poll=<125|500|1000>\n";
    long dpi = -1, poll = -1;
    for (const char *t; (t = strtok_r(nullptr, " \n", &rest));) {
        char *end;
        if (strncmp(t, "dpi=", 4) == 0 && dpi < 0 && poll < 0) {
            unsigned long v = strtoul(t + 4, &end, 10);
            if (t[4] == '\0' || *end != '\0' || v > 65535) {
                err(reply, size, "dpi must be a whole number", id);
                return;
            }
            dpi = (long)v;
        } else if (strncmp(t, "poll=", 5) == 0 && poll < 0 && dpi < 0) {
            unsigned long v = strtoul(t + 5, &end, 10);
            if (t[5] == '\0' || *end != '\0' || (v != 125 && v != 500 && v != 1000)) {
                err(reply, size, "poll must be 125, 500 or 1000", id);
                return;
            }
            poll = (long)v;
        } else {
            snprintf(reply, size, "%s", usage);
            return;
        }
    }
    if (dpi < 0 && poll < 0) {
        snprintf(reply, size, "%s", usage);
        return;
    }

    char node[32];
    struct peri_module m;
    if (!resolve_module(id, &m, node, sizeof node, reply, size))
        return;
    // The module must state a range, as it must state a brightness limit: no limit, no writes.
    if (dpi >= 0 && (m.max_dpi == 0 || dpi < m.min_dpi || dpi > m.max_dpi)) {
        err(reply, size, m.max_dpi ? "dpi is outside the module's range" : "the module sets no dpi range", id);
        return;
    }

    int fd = razer90_open(node);
    if (fd < 0) {
        err(reply, size, "cannot open the device (see `journalctl --user -u perid`)", id);
        return;
    }
    struct razer90_dpi before_dpi, after_dpi;
    unsigned before_hz, after_hz;
    bool have_before = dpi >= 0 ? razer90_dpi(fd, &before_dpi) == 0
                                : razer90_poll_rate(fd, &before_hz) == 0;
    if (!have_before) {
        close(fd);
        err(reply, size, "the current value could not be read, so nothing was written (no way to put it back)", id);
        return;
    }
    // The value read must be one this request can put back: one DPI for both axes, and one of the
    // three poll rates (a faster rate, such as the dongle's, is set with another command).
    if ((dpi >= 0 && before_dpi.x != before_dpi.y) || (dpi < 0 && before_hz == 0)) {
        close(fd);
        err(reply, size, dpi >= 0 ? "the mouse has different x and y DPI, which this request cannot put back, so nothing was written"
                                  : "the mouse's poll rate is not 125, 500 or 1000 Hz, which this request cannot put back, so nothing was written",
            id);
        return;
    }
    int rc = dpi >= 0 ? razer90_set_dpi(fd, (unsigned)dpi, (unsigned)dpi)
                      : razer90_set_poll_rate(fd, (unsigned)poll);
    bool read_back = rc == 0 && (dpi >= 0 ? razer90_dpi(fd, &after_dpi) == 0
                                          : razer90_poll_rate(fd, &after_hz) == 0);
    close(fd);
    if (rc != 0) {
        char why[96], text[160];
        device_said(why, sizeof why, rc);
        snprintf(text, sizeof text, "the change was not accepted: %s", why);
        err(reply, size, text, id);
        return;
    }
    if (dpi >= 0)
        snprintf(reply, size, "ok module=%s sent=dpi:%ld before=%u:%u device=ok", m.id, dpi,
                 before_dpi.x, before_dpi.y);
    else
        snprintf(reply, size, "ok module=%s sent=poll:%ld before=%u device=ok", m.id, poll, before_hz);
    size_t used = strlen(reply);
    if (!read_back)
        snprintf(reply + used, size - used, " readback=unavailable stored=maybe\n");
    else if (dpi >= 0)
        snprintf(reply + used, size - used, " readback=%u:%u stored=maybe\n", after_dpi.x, after_dpi.y);
    else
        snprintf(reply + used, size - used, " readback=%u stored=maybe\n", after_hz);
}

void set_handle(const char *args, char *reply, size_t size)
{
    static const char usage[] = "err usage: set <module-id> brightness=<0-255> | effect=<name> "
                                "[color=<rrggbb>] [color2=<rrggbb>] [speed=<slow|fast>]\n";
    char line[PERI_REQUEST_MAX + 1], *rest; // what perid reads: never cut a request short
    snprintf(line, sizeof line, "%s", args);
    const char *id = strtok_r(line, " \n", &rest);
    if (!id) {
        snprintf(reply, size, "%s", usage);
        return;
    }

    if (installed_engine_is(id, "razer90")) {
        set_razer90(id, rest, reply, size);
        return;
    }
    if (installed_engine_is(id, "v4l2")) {
        set_camera(id, rest, reply, size);
        return;
    }

    // A colour, a speed, an effect name and a brightness are the look's rules.
    // This request only decides that the change is one thing: a brightness, or
    // an effect. The values are judged before any device is opened.
    struct peri_look_field fields[5];
    size_t nfields = 0;
    struct peri_look_seen seen = { 0 };
    for (const char *t; (t = strtok_r(nullptr, " \n", &rest));) {
        if (nfields >= 5 || !peri_look_take(t, &seen, &fields[nfields])) {
            snprintf(reply, size, "%s", usage);
            return;
        }
        nfields++;
    }
    struct peri_look raw = PERI_LOOK_EMPTY;
    if (nfields > 0) {
        const char *why = peri_look_read(fields, nfields, &raw);
        if (why) {
            err(reply, size, why, id);
            return;
        }
    }
    long brightness = raw.brightness;
    const char *effect_name = raw.effect;
    const char *color = raw.color[0] ? raw.color : nullptr;
    const char *color2 = raw.color2[0] ? raw.color2 : nullptr;
    const char *speed = raw.speed[0] ? raw.speed : nullptr;
    bool bright_set = brightness >= 0, effect_set = effect_name[0] != '\0';
    if (bright_set == effect_set || (bright_set && (color || color2 || speed))
        || ((color || color2 || speed) && !effect_set)) {
        snprintf(reply, size, "%s", usage);
        return;
    }
    struct razer_effect_opts opts;
    int effect_id_checked = -1;
    if (effect_set && !effect_opts(effect_name, color, color2, speed, &opts, &effect_id_checked)) {
        err(reply, size, color2 && !color ? "a second colour needs a first"
                                          : "this effect needs a valid color=<rrggbb>", id);
        return;
    }

    struct peri_module m;
    int fd = open_module_device(id, &m, reply, size);
    if (fd < 0)
        return;

    if (effect_set) {
        int rc = razer_set_effect_opts(fd, RAZER_STORE_NONE, (uint8_t)effect_id_checked, &opts);
        close(fd);
        if (rc != 0) {
            char why[96], text[160];
            device_said(why, sizeof why, rc);
            snprintf(text, sizeof text, "the effect was not accepted: %s", why);
            err(reply, size, text, id);
            return;
        }
        snprintf(reply, size,
                 "ok module=%s sent=%s color=%s device=ok storage=none readback=unavailable\n",
                 m.id, effect_name, color ? (color2 ? "two" : "yes") : "no");
        return;
    }

    // The module must state a limit, as for the keyboard: no limit, no brightness writes.
    if (m.max_brightness == 0 || brightness > m.max_brightness) {
        close(fd);
        err(reply, size, m.max_brightness ? "brightness is above the module's limit"
                                          : "the module sets no brightness limit", id);
        return;
    }

    uint8_t now;
    bool ok = razer_set_brightness(fd, RAZER_STORE_NONE, (uint8_t)brightness) == 0
              && razer_brightness(fd, RAZER_STORE_NONE, &now) == 0;
    close(fd);
    if (!ok) {
        err(reply, size, "the device did not accept the change", id);
        return;
    }
    snprintf(reply, size, "ok module=%s brightness=%u storage=none\n", m.id, now);
}

// The installed razer module with this id (looked up in the installed directory only), without
// touching the device. Returns false with an "err ..." reply.
static bool installed_razer_module(const char *id, struct peri_module *out, char *reply, size_t size)
{
    struct peri_module m;
    // A bad id, a missing home and a missing module all read as "not installed"
    // to the user: none of them is a module that has looks.
    if (peri_installed_get(id, &m) != PERI_INSTALLED_OK) {
        err(reply, size, "no such installed module; run `peri use <id>` (see `peri modules`)", id);
        return false;
    }
    if (strcmp(m.engine, "razer") != 0) {
        err(reply, size, "this module has no looks", id);
        return false;
    }
    *out = m;
    return true;
}

static void reply_look(char *reply, size_t size, const char *id, enum peri_look_which which,
                       const struct peri_look *l)
{
    char brightness[8] = "unset";
    if (l->brightness >= 0)
        snprintf(brightness, sizeof brightness, "%d", l->brightness);
    snprintf(reply, size,
             "ok module=%s look=%s effect=%s color=%s color2=%s speed=%s brightness=%s follow=%s\n", id,
             peri_look_which_name(which), l->effect[0] ? l->effect : "unset",
             l->color[0] ? l->color : "none", l->color2[0] ? l->color2 : "none",
             l->speed[0] ? l->speed : "default", brightness, peri_look_follow(id) ? "yes" : "no");
}

// Send a saved look to the device: its effect, then its brightness. Used by the `look apply`
// request and by following the mic's mute state. The saved look is checked again, so a hand-edited
// file can never send what `set` would refuse. The reply is "ok ..." or "err ...".
static void apply_saved_look(const char *id, enum peri_look_which which, const struct peri_module *m,
                             char *reply, size_t size)
{
    struct peri_look l = peri_look_load(id, which);
    if (!l.effect[0] && l.brightness < 0) {
        err(reply, size, "nothing is saved for this look", id);
        return;
    }
    if (l.brightness >= 0 && (m->max_brightness == 0 || l.brightness > m->max_brightness)) {
        err(reply, size, "the saved brightness is above the module's limit", id);
        return;
    }
    struct razer_effect_opts opts;
    int effect_checked = -1;
    if (l.effect[0]
        && !effect_opts(l.effect, l.color[0] ? l.color : nullptr, l.color2[0] ? l.color2 : nullptr,
                        l.speed[0] ? l.speed : nullptr, &opts, &effect_checked)) {
        err(reply, size, "the saved effect is not valid", id);
        return;
    }

    struct peri_module dev;
    int fd = open_module_device(id, &dev, reply, size);
    if (fd < 0)
        return;
    int effect_rc = l.effect[0]
        ? razer_set_effect_opts(fd, RAZER_STORE_NONE, (uint8_t)effect_checked, &opts) : 0;
    if (effect_rc != 0) {
        close(fd);
        char why[96], text[200];
        device_said(why, sizeof why, effect_rc);
        snprintf(text, sizeof text, "the effect was not accepted (%s); nothing else was sent", why);
        err(reply, size, text, id);
        return;
    }
    uint8_t now = 0;
    if (l.brightness >= 0
        && (razer_set_brightness(fd, RAZER_STORE_NONE, (uint8_t)l.brightness) != 0
            || razer_brightness(fd, RAZER_STORE_NONE, &now) != 0)) {
        close(fd);
        err(reply, size, l.effect[0] ? "the effect was applied but the device did not accept the brightness"
                                     : "the device did not accept the brightness", id);
        return;
    }
    close(fd);
    char shown[8] = "-";
    if (l.brightness >= 0)
        snprintf(shown, sizeof shown, "%u", now);
    snprintf(reply, size, "ok module=%s applied=%s effect=%s brightness=%s\n", id,
             peri_look_which_name(which), l.effect[0] ? l.effect : "-", shown);
}

void look_handle(const char *args, char *reply, size_t size)
{
    static const char usage[] = "err usage: look <module-id> follow <on|off|get> | get|save|apply <unmuted|muted> "
                                "[effect=<name>] [color=<rrggbb>] [color2=<rrggbb>] [speed=<slow|fast>] "
                                "[brightness=<0-255>]\n";
    char line[PERI_REQUEST_MAX + 1], *rest;
    snprintf(line, sizeof line, "%s", args);
    const char *id = strtok_r(line, " \n", &rest);
    const char *verb = strtok_r(nullptr, " \n", &rest);
    const char *which_name = strtok_r(nullptr, " \n", &rest);
    // look <module-id> follow on|off|get: whether a mute change applies the matching look.
    if (id && verb && strcmp(verb, "follow") == 0) {
        bool on = which_name && strcmp(which_name, "on") == 0;
        bool known = on || (which_name && (strcmp(which_name, "off") == 0 || strcmp(which_name, "get") == 0));
        struct peri_module fm;
        if (!known || strtok_r(nullptr, " \n", &rest)) {
            snprintf(reply, size, "%s", usage);
            return;
        }
        if (!installed_razer_module(id, &fm, reply, size))
            return;
        if (strcmp(which_name, "get") != 0 && !peri_look_set_follow(id, on)) {
            err(reply, size, "could not save the setting", id);
            return;
        }
        snprintf(reply, size, "ok module=%s follow=%s\n", id, peri_look_follow(id) ? "yes" : "no");
        return;
    }

    enum peri_look_which which;
    if (!id || !verb || !peri_look_which_parse(which_name, &which)) {
        snprintf(reply, size, "%s", usage);
        return;
    }
    bool get = strcmp(verb, "get") == 0, save = strcmp(verb, "save") == 0,
         apply = strcmp(verb, "apply") == 0;
    if (!get && !save && !apply) {
        snprintf(reply, size, "%s", usage);
        return;
    }

    // The settings of a save: the whole look, so anything left out is "not set". get and apply
    // take none. A value is judged before the module is required, so a typo is a typo; whether
    // the settings belong together is the look module's rule, once a module is known.
    struct peri_look_field fields[5];
    size_t nfields = 0;
    struct peri_look_seen seen = { 0 };
    for (const char *t; (t = strtok_r(nullptr, " \n", &rest));) {
        if (!save || nfields >= 5 || !peri_look_take(t, &seen, &fields[nfields])) {
            snprintf(reply, size, "%s", usage);
            return;
        }
        nfields++;
    }
    struct peri_look raw = PERI_LOOK_EMPTY;
    if (nfields > 0) {
        const char *why = peri_look_read(fields, nfields, &raw);
        if (why) {
            err(reply, size, why, id);
            return;
        }
    }

    struct peri_module m;
    if (!installed_razer_module(id, &m, reply, size))
        return;

    if (get) {
        struct peri_look saved = peri_look_load(id, which);
        reply_look(reply, size, id, which, &saved);
        return;
    }

    if (save) {
        struct peri_look want;
        const char *why = peri_look_stored(&raw, &want);
        if (why) {
            err(reply, size, why, id);
            return;
        }
        if (want.brightness >= 0 && (m.max_brightness == 0 || want.brightness > m.max_brightness)) {
            err(reply, size, "brightness is above the module's limit", id);
            return;
        }
        if (!peri_look_save(id, which, &want)) {
            err(reply, size, "could not save the look", id);
            return;
        }
        struct peri_look saved = peri_look_load(id, which);
        reply_look(reply, size, id, which, &saved);
        return;
    }

    // apply: the only look request that writes to the device.
    apply_saved_look(id, which, &m, reply, size);
}

static void reply_audio(char *reply, size_t size, const char *id, int card)
{
    static const struct { const char *key; enum peri_audio_ctl ctl; bool yes_no; } keys[] = {
        { "gain", PERI_AUDIO_GAIN, false }, { "monitor", PERI_AUDIO_MONITOR, false },
        { "monitor_on", PERI_AUDIO_MONITOR_ON, true }, { "headphones", PERI_AUDIO_HEADPHONES, false },
    };
    int n = snprintf(reply, size, "ok module=%s", id);
    for (size_t i = 0; i < sizeof keys / sizeof *keys && n > 0 && (size_t)n < size; i++) {
        int v;
        char shown[8] = "unknown";
        if (peri_audio_get(card, keys[i].ctl, &v)) {
            if (keys[i].yes_no)
                snprintf(shown, sizeof shown, "%s", v ? "yes" : "no");
            else
                snprintf(shown, sizeof shown, "%d", v);
        }
        n += snprintf(reply + n, size - (size_t)n, " %s=%s", keys[i].key, shown);
    }
    if (n > 0 && (size_t)n + 2 <= size)
        memcpy(reply + n, "\n", 2);
}

void audio_handle(const char *args, char *reply, size_t size)
{
    static const char usage[] = "err usage: audio <module-id> get | set gain=<0-100> monitor=<0-100> "
                                "monitor_on=<yes|no> headphones=<0-100>\n";
    char line[PERI_REQUEST_MAX + 1], *rest;
    snprintf(line, sizeof line, "%s", args);
    const char *id = strtok_r(line, " \n", &rest);
    const char *verb = strtok_r(nullptr, " \n", &rest);
    if (!id || !verb || (strcmp(verb, "get") != 0 && strcmp(verb, "set") != 0)) {
        snprintf(reply, size, "%s", usage);
        return;
    }
    bool set = strcmp(verb, "set") == 0;

    // Parse every setting before touching the sound card, so a bad one changes nothing.
    struct { enum peri_audio_ctl ctl; int value; } changes[4];
    size_t count = 0;
    for (const char *t; (t = strtok_r(nullptr, " \n", &rest));) {
        if (!set || count == 4) {
            snprintf(reply, size, "%s", usage);
            return;
        }
        const char *eq = strchr(t, '=');
        size_t klen = eq ? (size_t)(eq - t) : 0;
        if (!eq) {
            snprintf(reply, size, "%s", usage);
            return;
        }
        enum peri_audio_ctl ctl;
        bool level = true;
        if (klen == 4 && strncmp(t, "gain", 4) == 0) ctl = PERI_AUDIO_GAIN;
        else if (klen == 7 && strncmp(t, "monitor", 7) == 0) ctl = PERI_AUDIO_MONITOR;
        else if (klen == 10 && strncmp(t, "monitor_on", 10) == 0) { ctl = PERI_AUDIO_MONITOR_ON; level = false; }
        else if (klen == 10 && strncmp(t, "headphones", 10) == 0) ctl = PERI_AUDIO_HEADPHONES;
        else {
            snprintf(reply, size, "%s", usage);
            return;
        }
        for (size_t i = 0; i < count; i++)
            if (changes[i].ctl == ctl) { // a key given twice is a typo, not a request
                snprintf(reply, size, "%s", usage);
                return;
            }
        int value;
        if (level) {
            char *end;
            unsigned long v = strtoul(eq + 1, &end, 10);
            if (eq[1] == '\0' || *end != '\0' || v > 100) {
                err(reply, size, "levels are whole numbers from 0 to 100", id);
                return;
            }
            value = (int)v;
        } else if (strcmp(eq + 1, "yes") == 0 || strcmp(eq + 1, "no") == 0) {
            value = strcmp(eq + 1, "yes") == 0;
        } else {
            err(reply, size, "monitor_on must be yes or no", id);
            return;
        }
        changes[count].ctl = ctl;
        changes[count++].value = value;
    }
    if (set && count == 0) {
        snprintf(reply, size, "%s", usage);
        return;
    }

    struct peri_module m;
    if (!installed_razer_module(id, &m, reply, size))
        return;
    int card = peri_audio_card(m.vid, m.pid);
    if (card < 0) {
        err(reply, size, "the sound card is not connected", id);
        return;
    }
    for (size_t i = 0; i < count; i++)
        if (!peri_audio_set(card, changes[i].ctl, changes[i].value)) {
            err(reply, size, "the sound card did not accept the change", id);
            return;
        }
    reply_audio(reply, size, id, card);
}

// The mic's mute state changed (after the quiet period, see follow.h): apply the saved look for the
// new state, if following is on and a look is saved for it. The result goes to the journal.
void follow_mute_apply(const char *module_id, bool unmuted)
{
    if (!peri_look_follow(module_id))
        return;
    char reply[256];
    struct peri_module m;
    if (!installed_razer_module(module_id, &m, reply, sizeof reply)) {
        fprintf(stderr, "perid: follow mute: %s", reply);
        return;
    }
    enum peri_look_which which = unmuted ? PERI_LOOK_UNMUTED : PERI_LOOK_MUTED;
    struct peri_look l = peri_look_load(module_id, which);
    if (!l.effect[0] && l.brightness < 0)
        return; // nothing is saved for this state: nothing to send
    apply_saved_look(module_id, which, &m, reply, sizeof reply);
    fprintf(stderr, "perid: follow mute (%s): %s", peri_look_which_name(which), reply);
}
