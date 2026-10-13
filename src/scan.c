// Device inventory: walk /sys/class/hidraw and describe every HID device.
// Sysfs only; nothing here opens a device node.

#include "scan.h"

#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Walk the HID report descriptor and pick a top-level collection: the first
// one, unless a vendor-defined one (usage page >= 0xFF00) exists, since that
// is where lighting and device settings live. The Seiren, for example, lists
// its consumer-control collection first and the vendor one second.
void peri_parse_descriptor(const uint8_t *d, size_t len, struct peri_device *dev)
{
    dev->usage_page = dev->usage = 0;
    dev->fido = false;
    unsigned page = 0, usage = 0;
    int depth = 0;
    bool found = false;

    for (size_t i = 0; i < len;) {
        uint8_t prefix = d[i];
        if (prefix == 0xFE) { // long item, nothing we need
            i += (i + 1 < len ? d[i + 1] : 0) + 3;
            continue;
        }

        size_t size = (prefix & 3) == 3 ? 4 : (prefix & 3); // size code 3 means 4 bytes
        if (i + 1 + size > len)
            break;
        uint32_t val = 0;
        for (size_t k = 0; k < size; k++)
            val |= (uint32_t)d[i + 1 + k] << (8 * k);

        int type = (prefix >> 2) & 3;
        int tag = prefix >> 4;
        if (type == 1 && tag == 0) { // global: usage page
            page = val & 0xFFFF;
        } else if (type == 2 && tag == 0) { // local: usage
            usage = val & 0xFFFF;
        } else if (type == 0) { // main item
            if (tag == 0xA) { // collection
                if (depth == 0 && page == 0xF1D0) // FIDO, whichever collection we pick
                    dev->fido = true;
                if (depth == 0 && (!found || (page >= 0xFF00 && dev->usage_page < 0xFF00))) {
                    dev->usage_page = (uint16_t)page;
                    dev->usage = (uint16_t)usage;
                    found = true;
                }
                depth++;
            } else if (tag == 0xC && depth > 0) { // end collection
                depth--;
            }
            usage = 0; // local state resets after every main item
        }
        i += 1 + size;
    }
}

// A webcam's buttons show up as a small HID interface next to its video
// interfaces. Walk up from the HID device to its USB device and look for a
// video-class (0x0e) interface. Anything that is not USB has no such files,
// which just means "not a camera".
static bool usb_has_video_interface(const char *node)
{
    char path[PATH_MAX], real[PATH_MAX];
    snprintf(path, sizeof path, "/sys/class/hidraw/%s/device", node);
    if (!realpath(path, real)) // <usb device>/<interface>/<hid device>
        return false;

    char *slash = strrchr(real, '/');
    if (!slash)
        return false;
    *slash = '\0'; // now the USB interface
    snprintf(path, sizeof path, "%s/bInterfaceClass", real);
    if (access(path, F_OK) != 0)
        return false;
    slash = strrchr(real, '/');
    if (!slash)
        return false;
    *slash = '\0'; // now the USB device

    DIR *dir = opendir(real);
    if (!dir)
        return false;
    bool video = false;
    for (struct dirent *e; !video && (e = readdir(dir));) {
        if (!strchr(e->d_name, ':')) // interfaces are named "<device>:<config>.<n>"
            continue;
        snprintf(path, sizeof path, "%s/%s/bInterfaceClass", real, e->d_name);
        FILE *f = fopen(path, "r");
        unsigned cls = 0;
        if (f && fscanf(f, "%x", &cls) == 1 && cls == 0x0e)
            video = true;
        if (f)
            fclose(f);
    }
    closedir(dir);
    return video;
}

enum peri_node_verdict peri_node_check(const struct peri_device *devs, size_t count,
                                       const char *node, bool needs_xap)
{
    for (size_t i = 0; i < count; i++) {
        const struct peri_device *d = &devs[i];
        if (strcmp(d->node, node) != 0)
            continue;
        if (d->hidden == PERI_HIDDEN_FIDO)
            return PERI_NODE_SECURITY_KEY;
        if (needs_xap && !(d->usage_page == 0xFF51 && d->usage == 0x58))
            return PERI_NODE_NOT_XAP;
        return PERI_NODE_OK;
    }
    return PERI_NODE_MISSING;
}

void peri_group_key(unsigned bus, const char *phys, const char *uniq, const char *node,
                    char *out, size_t size)
{
    char key[256];
    if (!phys[0])
        snprintf(key, sizeof key, "%s", node);
    else if (bus != 0x0003 && uniq[0])
        snprintf(key, sizeof key, "%s+%s", phys, uniq);
    else {
        snprintf(key, sizeof key, "%s", phys);
        char *slash = strrchr(key, '/'); // drop "/inputN"
        if (slash)
            *slash = '\0';
    }
    snprintf(out, size, "%s", key);
    for (char *p = out; *p; p++) // keeps the list protocol one-token-per-field
        if (*p == ' ')
            *p = '_';
}

const char *peri_hidden_name(enum peri_hidden hidden)
{
    return hidden == PERI_HIDDEN_FIDO ? "fido" : hidden == PERI_HIDDEN_CAMERA ? "camera" : "-";
}

void peri_classify(struct peri_device *devs, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        devs[i].hidden = PERI_VISIBLE;
        for (size_t j = 0; j < count; j++) {
            if (strcmp(devs[i].group, devs[j].group) != 0)
                continue;
            if (devs[j].fido)
                devs[i].hidden = PERI_HIDDEN_FIDO;
            else if (devs[j].camera && devs[i].hidden == PERI_VISIBLE)
                devs[i].hidden = PERI_HIDDEN_CAMERA;
        }
    }
}

bool peri_read_device(const char *node, struct peri_device *dev)
{
    char path[320];
    snprintf(path, sizeof path, "/sys/class/hidraw/%s/device/uevent", node);
    FILE *f = fopen(path, "r");
    if (!f)
        return false;

    *dev = (struct peri_device){};
    snprintf(dev->node, sizeof dev->node, "%s", node);

    char line[256];
    unsigned bus = 0, vid = 0, pid = 0;
    char phys[128] = "", uniq[64] = "";
    while (fgets(line, sizeof line, f)) {
        if (sscanf(line, "HID_ID=%x:%x:%x", &bus, &vid, &pid) == 3)
            continue;
        if (strncmp(line, "HID_NAME=", 9) == 0) {
            line[strcspn(line, "\n")] = '\0';
            snprintf(dev->name, sizeof dev->name, "%s", line + 9);
        }
        if (strncmp(line, "HID_PHYS=", 9) == 0) { // "usb-0000:c5:00.3-1.1.4.1/input2"
            line[strcspn(line, "\n")] = '\0';
            snprintf(phys, sizeof phys, "%s", line + 9);
        }
        if (strncmp(line, "HID_UNIQ=", 9) == 0) {
            line[strcspn(line, "\n")] = '\0';
            snprintf(uniq, sizeof uniq, "%s", line + 9);
        }
    }
    fclose(f);

    peri_group_key(bus, phys, uniq, node, dev->group, sizeof dev->group);

    dev->vid = (uint16_t)vid;
    dev->pid = (uint16_t)pid;

    snprintf(path, sizeof path, "/sys/class/hidraw/%s/device/report_descriptor", node);
    f = fopen(path, "rb");
    if (f) {
        uint8_t desc[4096];
        size_t len = fread(desc, 1, sizeof desc, f);
        fclose(f);
        peri_parse_descriptor(desc, len, dev);
    }
    dev->camera = bus == 0x0003 && usb_has_video_interface(node); // 3 = USB
    return true;
}

size_t peri_scan(struct peri_device *out, size_t max)
{
    struct dirent **entries;
    int n = scandir("/sys/class/hidraw", &entries, nullptr, versionsort);
    if (n < 0)
        return 0;

    size_t count = 0;
    for (int i = 0; i < n; i++) {
        if (count < max && strncmp(entries[i]->d_name, "hidraw", 6) == 0
            && peri_read_device(entries[i]->d_name, &out[count]))
            count++;
        free(entries[i]);
    }
    free(entries);
    peri_classify(out, count);
    return count;
}
