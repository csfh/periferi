#include <stdio.h>
#include <string.h>

#include "../src/razer_scan.h"

static int failures;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
            failures++;                                                    \
        }                                                                  \
    } while (0)

// Append one packet to a text dump in Wireshark's layout: a four-digit offset, two spaces, 16
// bytes with an extra space after the eighth, two spaces, then the ASCII column. A blank line
// ends the packet.
static void add_packet(char *dump, size_t size, const uint8_t *bytes, size_t n)
{
    size_t len = strlen(dump);
    for (size_t off = 0; off < n; off += 16) {
        len += (size_t)snprintf(dump + len, size - len, "%04zx  ", off);
        for (int j = 0; j < 16; j++) {
            if (j == 8)
                len += (size_t)snprintf(dump + len, size - len, " ");
            if (off + (size_t)j < n)
                len += (size_t)snprintf(dump + len, size - len, "%02x ", bytes[off + (size_t)j]);
            else
                len += (size_t)snprintf(dump + len, size - len, "   ");
        }
        len += (size_t)snprintf(dump + len, size - len, " ");
        for (int j = 0; j < 16 && off + (size_t)j < n; j++) {
            uint8_t c = bytes[off + (size_t)j];
            len += (size_t)snprintf(dump + len, size - len, "%c", c >= 0x20 && c < 0x7f ? c : '.');
        }
        len += (size_t)snprintf(dump + len, size - len, "\n");
    }
    snprintf(dump + len, size - len, "\n");
}

struct seen {
    size_t count;
    struct razer_found found[8];
};

static void collect(const struct razer_found *f, void *user)
{
    struct seen *s = user;
    if (s->count < 8)
        s->found[s->count] = *f;
    s->count++;
}

int main(void)
{
    static char dump[16384];
    uint8_t usb_header[27];
    memset(usb_header, 0xaa, sizeof usb_header); // the capture's own header bytes precede the frame

    // 1: a request, firmware get (built by our own codec).
    uint8_t fw_request[RAZER_REPORT_LEN];
    razer_encode(fw_request, 0x52, 0x00, 0x81, nullptr, 2);
    uint8_t p1[27 + RAZER_REPORT_LEN];
    memcpy(p1, usb_header, 27);
    memcpy(p1 + 27, fw_request, RAZER_REPORT_LEN);
    add_packet(dump, sizeof dump, p1, sizeof p1);

    // 2: noise with no frame in it, including a 07 that is not a frame.
    uint8_t noise[100];
    memset(noise, 0x07, sizeof noise);
    add_packet(dump, sizeof dump, noise, sizeof noise);

    // 3: the real firmware-get reply captured from the mic (2026-10-13), after a different header.
    uint8_t reply[RAZER_REPORT_LEN] = { 0x07, 0x02, 0x52, 0x00, 0x00, 0x00, 0x02, 0x00, 0x81,
                                        0x01, 0x01, [62] = 0xd1 };
    uint8_t p3[13 + RAZER_REPORT_LEN];
    memset(p3, 0x11, 13);
    memcpy(p3 + 13, reply, RAZER_REPORT_LEN);
    add_packet(dump, sizeof dump, p3, sizeof p3);

    // 4: a command we do not know, class 0f command 77, with arguments.
    uint8_t args[4] = { 1, 2, 3, 4 };
    uint8_t unknown[RAZER_REPORT_LEN];
    razer_encode(unknown, 0x52, 0x0f, 0x77, args, 4);
    add_packet(dump, sizeof dump, unknown, sizeof unknown);

    // 5: two frames in one packet (a request then its reply), and a 6th that is cut short.
    uint8_t p5[2 * RAZER_REPORT_LEN];
    memcpy(p5, fw_request, RAZER_REPORT_LEN);
    memcpy(p5 + RAZER_REPORT_LEN, reply, RAZER_REPORT_LEN);
    add_packet(dump, sizeof dump, p5, sizeof p5);
    add_packet(dump, sizeof dump, fw_request, 40);

    // 7: a decoy that starts with 07 but whose checksum fits neither rule.
    uint8_t decoy[RAZER_REPORT_LEN];
    memcpy(decoy, fw_request, RAZER_REPORT_LEN);
    decoy[63] ^= 0x01;
    add_packet(dump, sizeof dump, decoy, sizeof decoy);

    struct seen s = { 0 };
    size_t n = razer_scan_dump(dump, collect, &s);
    CHECK(n == 5 && s.count == 5);

    CHECK(s.found[0].packet == 1 && s.found[0].request_crc && !s.found[0].reply_crc);
    CHECK(s.found[0].frame[7] == 0x00 && s.found[0].frame[8] == 0x81 && s.found[0].frame[6] == 2);

    CHECK(s.found[1].packet == 3 && s.found[1].reply_crc && !s.found[1].request_crc);
    CHECK(s.found[1].frame[1] == 0x02 && s.found[1].frame[9] == 0x01 && s.found[1].frame[10] == 0x01);

    CHECK(s.found[2].packet == 4 && s.found[2].request_crc);
    CHECK(s.found[2].frame[7] == 0x0f && s.found[2].frame[8] == 0x77);

    CHECK(s.found[3].packet == 5 && s.found[3].request_crc);   // both frames of packet 5
    CHECK(s.found[4].packet == 5 && s.found[4].reply_crc);

    // Naming and description: known commands are named, the new one is flagged.
    CHECK(razer_command_name(0x0f, 0x02) != nullptr && strcmp(razer_command_name(0x0f, 0x02), "effect set") == 0);
    CHECK(strcmp(razer_command_name(0x00, 0x81), "firmware get") == 0);
    CHECK(razer_command_name(0x0f, 0x77) == nullptr);
    char line[256];
    razer_describe(&s.found[0], line, sizeof line);
    CHECK(strstr(line, "request") && strstr(line, "class=00 cmd=81") && strstr(line, "(firmware get)"));
    razer_describe(&s.found[2], line, sizeof line);
    CHECK(strstr(line, "UNKNOWN") && strstr(line, "args=01 02 03 04"));
    razer_describe(&s.found[1], line, sizeof line);
    CHECK(strstr(line, "reply") && strstr(line, "status=02") && strstr(line, "args=01 01"));

    // A new variant of a known command is not the same as the known one: two effect-set frames
    // that differ only in the LED id (the second argument byte) are different variants.
    uint8_t led0[6] = { 1, 0, 2, 0, 0, 0 }, led5[6] = { 1, 5, 2, 0, 0, 0 };
    struct razer_found a = { .request_crc = true }, b = { .request_crc = true }, c = { .request_crc = false };
    razer_encode(a.frame, 0x52, 0x0f, 0x02, led0, 6);
    razer_encode(b.frame, 0x52, 0x0f, 0x02, led5, 6);
    memcpy(c.frame, a.frame, RAZER_REPORT_LEN);
    CHECK(razer_same_command(&a, &b) && !razer_same_variant(&a, &b));
    CHECK(razer_same_variant(&a, &a));
    CHECK(!razer_same_command(&a, &c));                       // the same bytes seen as a reply
    uint8_t fw2[2] = { 0, 0 };
    struct razer_found d = { .request_crc = true };
    razer_encode(d.frame, 0x52, 0x00, 0x81, fw2, 2);
    CHECK(!razer_same_command(&a, &d));

    // `tshark -V -x` numbers every packet ("Frame 12: ...") and prints a hex block per data source;
    // the capture's own number is used, and the same frame in two blocks is reported once.
    static char dump2[8192];
    uint8_t one[RAZER_REPORT_LEN];
    razer_encode(one, 0x52, 0x00, 0x81, fw2, 2);
    strcat(dump2, "Frame 12: 91 bytes on wire (728 bits), 91 bytes captured\n    Encapsulation type: USB\n\n");
    add_packet(dump2, sizeof dump2, one, sizeof one);
    add_packet(dump2, sizeof dump2, one, sizeof one);        // a second block for the same packet
    strcat(dump2, "Frame 13: 91 bytes on wire (728 bits), 91 bytes captured\n\n");
    add_packet(dump2, sizeof dump2, one, sizeof one);
    struct seen numbered = { 0 };
    CHECK(razer_scan_dump(dump2, collect, &numbered) == 2);
    CHECK(numbered.found[0].packet == 12 && numbered.found[1].packet == 13);

    // Text that is not a dump finds nothing and does no harm.
    struct seen none = { 0 };
    CHECK(razer_scan_dump("", collect, &none) == 0);
    CHECK(razer_scan_dump("hello\nworld\n\n", collect, &none) == 0);
    CHECK(razer_scan_dump("0000  07 00 52\n", collect, &none) == 0);   // too short to hold a frame

    // Hex text in the shapes people paste.
    uint8_t out[8];
    CHECK(razer_parse_hex("07 00 52", out, sizeof out) == 3 && out[0] == 7 && out[2] == 0x52);
    CHECK(razer_parse_hex("07:00:52", out, sizeof out) == 3);
    CHECK(razer_parse_hex("070052", out, sizeof out) == 3);
    CHECK(razer_parse_hex("07 0", out, sizeof out) == -1);           // an odd digit
    CHECK(razer_parse_hex("07 zz", out, sizeof out) == -1);
    CHECK(razer_parse_hex("0102030405060708090a", out, sizeof out) == -1); // too long
    return failures ? 1 : 0;
}
