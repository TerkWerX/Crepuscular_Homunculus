/* SPDX-License-Identifier: GPL-3.0-only */
/* Main-loop-only Crepuscular Homunculus intro. Artwork stays in flash;
 * four-row uploads reuse the existing DMA-safe canvas. */
#ifndef TERKINSTEIN_INTRO_H
#define TERKINSTEIN_INTRO_H

#include "terkinstein_homunculus.h"

#define TK_INTRO_LOGO_MS 1200u
#define TK_INTRO_FRAME_MS 200u

typedef struct {
    uint32_t logo_ms, animation_ms, frame, row, phase;
} tk_intro_t;

static void tk_intro_strip(tk_intro_t *intro)
{
    uint32_t first = intro->phase == 1u;
    uint32_t x = first ? 0u : TK_HOMUNCULUS_X;
    uint32_t y = intro->row + (first ? 0u : TK_HOMUNCULUS_Y);
    uint32_t w = first ? 240u : TK_HOMUNCULUS_WIDTH;
    uint32_t height = first ? 240u : TK_HOMUNCULUS_HEIGHT;
    uint32_t h = height - intro->row, dx, dy;
    if (h > 4u) h = 4u;
    cv_begin(w, h, C_BLACK);
    for (dy = 0; dy < h; dy++) {
        uint32_t sy = y + dy;
        if (sy < TK_HOMUNCULUS_Y || sy >= TK_HOMUNCULUS_Y + TK_HOMUNCULUS_HEIGHT)
            continue;
        for (dx = 0; dx < TK_HOMUNCULUS_WIDTH; dx++) {
            uint32_t at = (sy - TK_HOMUNCULUS_Y) * TK_HOMUNCULUS_WIDTH + dx;
            uint32_t packed = tk_homunculus_frames[intro->frame][at / 2u];
            uint32_t index = (at & 1u) ? packed & 15u : packed >> 4;
            cv_px[dy * w + TK_HOMUNCULUS_X - x + dx] = swap16(tk_homunculus_palette[index]);
        }
    }
    /* Text is clipped into these same strips: no separate blocking upload. */
    if (first && y < 210u && y + h > 194u)
        cv_text((240 - text_w(&FONT_S, "CREPUSCULAR")) / 2,
            194 - (int32_t)y, &FONT_S, "CREPUSCULAR", RGB(168, 190, 124));
    if (first && y < 230u && y + h > 214u)
        cv_text((240 - text_w(&FONT_S, "HOMUNCULUS")) / 2,
            214 - (int32_t)y, &FONT_S, "HOMUNCULUS", RGB(140, 112, 159));
    cv_blit(x, y);
    intro->row += h;
    if (first && intro->row == height) {
        lcd_sync();
        intro->phase = 2u;
        intro->row = TK_HOMUNCULUS_HEIGHT;
        intro->animation_ms = fm1_ms;
    }
}

/* Loop until the caller handles a key/button press. Time alone never dismisses
 * startup; the gesture still performs its ordinary musical/control action. */
static uint32_t tk_intro_tick(tk_intro_t *intro)
{
    static const uint8_t sequence[4] = {0, 1, 2, 1};
    if (!intro->phase) {
        if (fm1_ms - intro->logo_ms < TK_INTRO_LOGO_MS) return 1u;
        intro->phase = 1u;
    }
    if (intro->phase == 2u) {
        uint32_t elapsed = fm1_ms - intro->animation_ms;
        uint32_t frame;
        frame = sequence[(elapsed / TK_INTRO_FRAME_MS) & 3u];
        if (frame != intro->frame) {
            intro->frame = frame;
            intro->row = 0u;
        }
    }
    if (intro->row < (intro->phase == 1u ? 240u : TK_HOMUNCULUS_HEIGHT))
        tk_intro_strip(intro);
    return 1u;
}
#endif
