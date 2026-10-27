/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TERKINSTEIN_DRUMS_H
#define TERKINSTEIN_DRUMS_H
#include <stdint.h>
#define TK_DRUM_LANES 12u
typedef struct { uint8_t level, decay, tune, pan; } tk_drum_mix_t;
static const char *const TK_DRUM_NAMES[TK_DRUM_LANES] = {
    "KICK", "SUB KICK", "SNARE", "CLAP", "CLOSED HAT", "OPEN HAT",
    "METAL HAT", "LOW TOM", "HIGH TOM", "RIM", "BELL", "CLOCK"
};
static void tk_drums_defaults(tk_drum_mix_t *mix)
{
    uint32_t lane;
    for (lane = 0; lane < TK_DRUM_LANES; lane++)
        mix[lane].level = mix[lane].decay = mix[lane].tune = mix[lane].pan = 64u;
}
static int tk_drums_valid(const tk_drum_mix_t *mix)
{
    uint32_t lane;
    for (lane = 0; lane < TK_DRUM_LANES; lane++)
        if (mix[lane].level > 127u || mix[lane].decay > 127u || mix[lane].tune > 127u || mix[lane].pan > 127u) return 0;
    return 1;
}
#endif
