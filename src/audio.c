#include "audio.h"

#include <alsa/asoundlib.h>
#include <stdio.h>
#include <string.h>

int peri_audio_card(unsigned vid, unsigned pid)
{
    char want[16];
    snprintf(want, sizeof want, "%04x:%04x", vid, pid);
    for (int n = 0; n < 32; n++) {
        char path[64], id[32] = "";
        snprintf(path, sizeof path, "/proc/asound/card%d/usbid", n);
        FILE *f = fopen(path, "r");
        if (!f)
            continue;
        bool got = fgets(id, sizeof id, f) != nullptr;
        fclose(f);
        id[strcspn(id, "\n")] = '\0';
        if (got && strcmp(id, want) == 0)
            return n;
    }
    return -1;
}

long peri_audio_pct_to_raw(int pct, long min, long max)
{
    if (pct < 0)
        pct = 0;
    if (pct > 100)
        pct = 100;
    return min + ((max - min) * pct + 50) / 100;
}

int peri_audio_raw_to_pct(long raw, long min, long max)
{
    if (max <= min)
        return 0;
    if (raw < min)
        raw = min;
    if (raw > max)
        raw = max;
    return (int)(((raw - min) * 100 + (max - min) / 2) / (max - min));
}

static const char *ctl_name(enum peri_audio_ctl ctl)
{
    switch (ctl) {
    case PERI_AUDIO_GAIN: return "Mic Capture Volume";
    case PERI_AUDIO_MONITOR: return "Mic Playback Volume";
    case PERI_AUDIO_MONITOR_ON: return "Mic Playback Switch";
    case PERI_AUDIO_HEADPHONES: return "Speaker Playback Volume";
    }
    return "";
}

static bool is_switch(enum peri_audio_ctl ctl)
{
    return ctl == PERI_AUDIO_MONITOR_ON;
}

// Open the card's control device and fill in the element id for `ctl`.
static bool open_ctl(int card, enum peri_audio_ctl ctl, snd_ctl_t **handle, snd_ctl_elem_id_t *id)
{
    char name[16];
    snprintf(name, sizeof name, "hw:%d", card);
    if (card < 0 || snd_ctl_open(handle, name, 0) < 0)
        return false;
    snd_ctl_elem_id_set_interface(id, SND_CTL_ELEM_IFACE_MIXER);
    snd_ctl_elem_id_set_name(id, ctl_name(ctl));
    return true;
}

bool peri_audio_get(int card, enum peri_audio_ctl ctl, int *value)
{
    snd_ctl_t *handle;
    snd_ctl_elem_id_t *id;
    snd_ctl_elem_info_t *info;
    snd_ctl_elem_value_t *val;
    snd_ctl_elem_id_alloca(&id);
    snd_ctl_elem_info_alloca(&info);
    snd_ctl_elem_value_alloca(&val);
    if (!open_ctl(card, ctl, &handle, id))
        return false;

    bool ok = false;
    snd_ctl_elem_info_set_id(info, id);
    snd_ctl_elem_value_set_id(val, id);
    if (snd_ctl_elem_info(handle, info) >= 0 && snd_ctl_elem_read(handle, val) >= 0) {
        if (is_switch(ctl)) {
            *value = snd_ctl_elem_value_get_boolean(val, 0) ? 1 : 0;
        } else {
            *value = peri_audio_raw_to_pct(snd_ctl_elem_value_get_integer(val, 0),
                                           snd_ctl_elem_info_get_min(info),
                                           snd_ctl_elem_info_get_max(info));
        }
        ok = true;
    }
    snd_ctl_close(handle);
    return ok;
}

bool peri_audio_set(int card, enum peri_audio_ctl ctl, int value)
{
    snd_ctl_t *handle;
    snd_ctl_elem_id_t *id;
    snd_ctl_elem_info_t *info;
    snd_ctl_elem_value_t *val;
    snd_ctl_elem_id_alloca(&id);
    snd_ctl_elem_info_alloca(&info);
    snd_ctl_elem_value_alloca(&val);
    if (!open_ctl(card, ctl, &handle, id))
        return false;

    bool ok = false;
    snd_ctl_elem_info_set_id(info, id);
    snd_ctl_elem_value_set_id(val, id);
    if (snd_ctl_elem_info(handle, info) >= 0 && snd_ctl_elem_read(handle, val) >= 0) {
        unsigned count = snd_ctl_elem_info_get_count(info);
        for (unsigned ch = 0; ch < count; ch++) {
            if (is_switch(ctl))
                snd_ctl_elem_value_set_boolean(val, ch, value != 0);
            else
                snd_ctl_elem_value_set_integer(val, ch,
                    peri_audio_pct_to_raw(value, snd_ctl_elem_info_get_min(info),
                                          snd_ctl_elem_info_get_max(info)));
        }
        ok = snd_ctl_elem_write(handle, val) >= 0;
    }
    snd_ctl_close(handle);
    return ok;
}
