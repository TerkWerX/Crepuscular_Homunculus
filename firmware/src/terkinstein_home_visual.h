/* SPDX-License-Identifier: GPL-3.0-only */
/* Main-only HOME choice. Both renderers share the existing Rift buffers,
 * dirty tiles and bounded upload path. No audio state belongs here. */
#ifndef TERKINSTEIN_HOME_VISUAL_H
#define TERKINSTEIN_HOME_VISUAL_H
#include "terkinstein_homunculus_visual.h"

/* Tagged cosmetic preference in the former settings padding byte. Ordinary
 * legacy zeroes and every unrecognized byte keep the Homunculus default. */
#define TK_HOME_VISUAL_RIFT 0xA5u
static uint8_t tk_home_visual_choice;
static uint32_t tk_home_visual_is_rift(uint32_t stored)
{
    return stored == TK_HOME_VISUAL_RIFT;
}
static void tk_home_visual_reset(void)
{
    tk_homunculus_visual_reset();
}
static void tk_home_visual_select(uint32_t stored)
{
    uint32_t next = tk_home_visual_is_rift(stored);
    if (next != tk_home_visual_choice) {
        tk_home_visual_choice = (uint8_t)next;
        tk_home_visual_reset(); /* discard partial geometry and force all tiles */
    }
}
static void tk_home_visual_prepare(const tk_visual_snapshot_t *sound,
                                   uint32_t now, uint32_t preset)
{
    if (tk_home_visual_choice) tk_rift_prepare(sound, now, preset);
    else tk_homunculus_visual_prepare(sound, now, preset);
}
static int tk_home_visual_pending(void) { return tk_rift_pending(); }
static uint32_t tk_home_visual_upload(void) { return tk_rift_upload(); }
#endif
