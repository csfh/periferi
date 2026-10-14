#include "razer_scan.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const struct { uint8_t cls, cmd; const char *name; } known[] = {
    { 0x00, 0x04, "device mode set" }, { 0x00, 0x81, "firmware get" }, { 0x00, 0x82, "serial get" },
    { 0x00, 0x84, "device mode get" }, { 0x0f, 0x02, "effect set" }, { 0x0f, 0x03, "custom frame" },
    { 0x0f, 0x04, "brightness set" },  { 0x0f, 0x84, "brightness get" },
};

bool razer_same_command(const struct razer_found *a, const struct razer_found *b)
{
    return a->frame[7] == b->frame[7] && a->frame[8] == b->frame[8]
           && a->request_crc == b->request_crc;
}

bool razer_same_variant(const struct razer_found *a, const struct razer_found *b)
{
    return razer_same_command(a, b) && a->frame[6] == b->frame[6] && a->frame[9] == b->frame[9]
           && a->frame[10] == b->frame[10];
}

const char *razer_command_name(uint8_t cls, uint8_t cmd)
{
    for (size_t i = 0; i < sizeof known / sizeof *known; i++)
        if (known[i].cls == cls && known[i].cmd == cmd)
            return known[i].name;
    return nullptr;
}

static uint8_t xor_range(const uint8_t *p, size_t from, size_t to)
{
    uint8_t x = 0;
    for (size_t i = from; i <= to; i++)
        x ^= p[i];
    return x;
}

// The hex value of one character, or -1.
static int nibble(char c)
{
    return isxdigit((unsigned char)c) ? (isdigit((unsigned char)c) ? c - '0' : tolower((unsigned char)c) - 'a' + 10) : -1;
}

// Wireshark's dump layout: a four-digit offset, two spaces, then byte j of the line at column
// 6 + 3*j, plus one more for the second eight; the ASCII column starts after those 16.
static size_t line_bytes(const char *line, size_t len, uint8_t out[16])
{
    if (len < 8)
        return 0;
    for (int i = 0; i < 4; i++)
        if (nibble(line[i]) < 0)
            return 0;
    if (line[4] != ' ' || line[5] != ' ')
        return 0;
    size_t n = 0;
    for (int j = 0; j < 16; j++) {
        size_t col = 6 + (size_t)j * 3 + (j >= 8 ? 1 : 0);
        if (col + 1 >= len)
            break;
        int hi = nibble(line[col]), lo = nibble(line[col + 1]);
        if (hi < 0 || lo < 0)
            break;
        out[n++] = (uint8_t)(hi << 4 | lo);
    }
    return n;
}

// The frames already reported for the current packet number: `tshark -V -x` prints more than one
// hex block for a packet (one per data source), and the same frame must not be reported twice.
struct reported {
    size_t packet;
    size_t count;
    uint8_t frames[8][RAZER_REPORT_LEN];
};

static bool already(const struct reported *r, const uint8_t *frame)
{
    for (size_t i = 0; i < r->count; i++)
        if (memcmp(r->frames[i], frame, RAZER_REPORT_LEN) == 0)
            return true;
    return false;
}

// Report every frame in one packet's bytes.
static size_t scan_packet(const uint8_t *p, size_t len, size_t packet, struct reported *done,
                          razer_found_cb cb, void *user)
{
    size_t found = 0;
    if (done->packet != packet) {
        done->packet = packet;
        done->count = 0;
    }
    for (size_t i = 0; i + RAZER_REPORT_LEN <= len;) {
        const uint8_t *f = p + i;
        bool req = f[0] == RAZER_REPORT_ID && f[1] == 0x00 && f[6] <= RAZER_REQUEST_ARGS_MAX
                   && xor_range(f, 1, 62) == f[63];
        bool rep = f[0] == RAZER_REPORT_ID && f[6] <= RAZER_REPLY_ARGS_MAX && f[1] != 0x00
                   && xor_range(f, 2, 61) == f[62];
        if ((req || rep) && already(done, f)) {
            i += RAZER_REPORT_LEN;
        } else if (req || rep) {
            if (done->count < 8)
                memcpy(done->frames[done->count++], f, RAZER_REPORT_LEN);
            struct razer_found out = { .packet = packet, .request_crc = req, .reply_crc = rep };
            memcpy(out.frame, f, RAZER_REPORT_LEN);
            cb(&out, user);
            found++;
            i += RAZER_REPORT_LEN; // never report the same bytes twice
        } else {
            i++;
        }
    }
    return found;
}

size_t razer_scan_dump(const char *text, razer_found_cb cb, void *user)
{
    uint8_t packet[65536];
    size_t len = 0, number = 0, found = 0, frame_no = 0;
    bool open = false;
    static struct reported done;
    done = (struct reported){ .packet = (size_t)-1 };

    const char *p = text;
    while (*p) {
        const char *end = strchr(p, '\n');
        size_t line_len = end ? (size_t)(end - p) : strlen(p);
        uint8_t bytes[16];
        size_t n = line_bytes(p, line_len, bytes);
        if (n > 0) {
            if (!open) {
                open = true;
                number = frame_no ? frame_no : number + 1;
                len = 0;
            }
            if (len + n <= sizeof packet) {
                memcpy(packet + len, bytes, n);
                len += n;
            }
        } else if (open && line_len == 0) { // a blank line ends the packet
            found += scan_packet(packet, len, number, &done, cb, user);
            open = false;
        } else if (!open && line_len > 6 && strncmp(p, "Frame ", 6) == 0) {
            // `tshark -V` starts every packet with "Frame 123: ...": that is the capture's own number.
            char *endp;
            unsigned long v = strtoul(p + 6, &endp, 10);
            if (endp != p + 6 && *endp == ':')
                frame_no = v;
        }
        p += line_len + (end ? 1 : 0);
    }
    if (open)
        found += scan_packet(packet, len, number, &done, cb, user);
    return found;
}

void razer_describe(const struct razer_found *found, char *out, size_t size)
{
    const uint8_t *f = found->frame;
    const char *name = razer_command_name(f[7], f[8]);
    if (size == 0)
        return;
    int n = snprintf(out, size, "%s txn=%02x class=%02x cmd=%02x (%s) size=%02x",
                     found->request_crc ? "request" : "reply  ", f[2], f[7], f[8],
                     name ? name : "UNKNOWN", f[6]);
    if (n > 0 && (size_t)n < size && !found->request_crc)
        n += snprintf(out + n, size - (size_t)n, " status=%02x", f[1]);
    if (n > 0 && (size_t)n < size)
        n += snprintf(out + n, size - (size_t)n, " args=");
    for (size_t i = 0; i < f[6] && i < 54 && n > 0 && (size_t)n + 4 < size; i++)
        n += snprintf(out + n, size - (size_t)n, "%s%02x", i ? " " : "", f[9 + i]);
}

ptrdiff_t razer_parse_hex(const char *text, uint8_t *out, size_t max)
{
    size_t n = 0;
    int pending = -1;
    for (const char *c = text; *c; c++) {
        if (*c == ' ' || *c == ':' || *c == '-' || *c == ',' || *c == '\n' || *c == '\t')
            continue;
        int v = nibble(*c);
        if (v < 0)
            return -1;
        if (pending < 0) {
            pending = v;
        } else {
            if (n == max)
                return -1;
            out[n++] = (uint8_t)(pending << 4 | v);
            pending = -1;
        }
    }
    return pending < 0 ? (ptrdiff_t)n : -1;
}
