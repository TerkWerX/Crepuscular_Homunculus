/* SPDX-License-Identifier: GPL-3.0-only */
/* Portable 4-track, four 16-step section mask. No transport or DSP ownership. */
#ifndef TERKINSTEIN_SECTION_MUTES_H
#define TERKINSTEIN_SECTION_MUTES_H
#include <stdint.h>
#define TK_SECTION_TRACKS 4u
#define TK_SECTION_COUNT 4u
#define TK_SECTION_STEPS 16u

static uint8_t tk_section_mutes_track(uint16_t mask, uint32_t track)
{
    return track < TK_SECTION_TRACKS ? (uint8_t)((mask >> (track * TK_SECTION_COUNT)) & 15u) : 0u;
}
static int tk_section_muted(uint16_t mask, uint32_t track, uint32_t local_step)
{
    return local_step < TK_SECTION_COUNT * TK_SECTION_STEPS &&
        !!(tk_section_mutes_track(mask, track) & (1u << (local_step / TK_SECTION_STEPS)));
}
/* -1 invalid, 0 unchanged, 1 changed. Absolute setters are retry-safe. */
static int tk_section_mutes_set(uint16_t *mask, uint32_t track,
                                uint32_t section, uint32_t muted)
{
    uint16_t bit, next;
    if (!mask || track >= TK_SECTION_TRACKS || section >= TK_SECTION_COUNT || muted > 1u) return -1;
    bit = (uint16_t)(1u << (track * TK_SECTION_COUNT + section));
    next = muted ? (uint16_t)(*mask | bit) : (uint16_t)(*mask & ~bit);
    if (next == *mask) return 0;
    *mask = next;
    return 1;
}
#endif
