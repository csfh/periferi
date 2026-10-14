#pragma once

#include <stdbool.h>

// The microphone's standard USB audio controls, through ALSA's control interface. These are the
// ordinary UAC mixer controls every USB audio device has, not Razer's own protocol, so they need
// no reverse engineering: the capture gain, the monitor mix that feeds the mic to the headphones,
// and the headphone output. Levels are shown as 0-100, a straight mapping of each control's own
// range. They are live mixer state of the host's audio device, set through the sound card.

enum peri_audio_ctl {
    PERI_AUDIO_GAIN,        // 'Mic Capture Volume'
    PERI_AUDIO_MONITOR,     // 'Mic Playback Volume': the mic mixed into the headphones
    PERI_AUDIO_MONITOR_ON,  // 'Mic Playback Switch' (0 off, 1 on)
    PERI_AUDIO_HEADPHONES,  // 'Speaker Playback Volume'
};

// The sound card number of the USB audio device with this id, or -1. Cards are numbered in the
// order they appear, so the number changes: always look it up.
int peri_audio_card(unsigned vid, unsigned pid);

// 0-100 <-> a control's raw range. Rounded to the nearest step; out-of-range input is clamped.
long peri_audio_pct_to_raw(int pct, long min, long max);
int peri_audio_raw_to_pct(long raw, long min, long max);

// Move every channel of a control by the amount that takes channel 0 from old[0] to new0, so a
// deliberate left/right difference survives. Each channel is clamped to [min, max], so at an
// extreme the difference narrows. Writes n values to out.
void peri_audio_shift(const long *old, int n, long new0, long min, long max, long *out);

// Read a control: a level 0-100, or 0/1 for the switch. False if the card or control is missing.
bool peri_audio_get(int card, enum peri_audio_ctl ctl, int *value);

// Set a control (all its channels). A level is clamped to 0-100.
bool peri_audio_set(int card, enum peri_audio_ctl ctl, int value);
