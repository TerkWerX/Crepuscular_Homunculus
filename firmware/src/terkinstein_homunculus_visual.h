/* SPDX-License-Identifier: GPL-3.0-only */
/* Crepuscular Homunculus HOME portrait and sound-reactive lightning.
 * Main-only: consumes the existing coherent visual snapshot, never live DSP
 * state. The shared Rift backend owns the same two pool buffers and <=512
 * pixel tile transfers; callers still poll/check musical slack between calls.
 * Include this instead of terkinstein_rift.h for the default instrument. */
#ifndef TERKINSTEIN_HOMUNCULUS_VISUAL_H
#define TERKINSTEIN_HOMUNCULUS_VISUAL_H

#include "terkinstein_rift.h"
#include "terkinstein_homunculus.h"

#define TK_HOMUNCULUS_HOME_W 88u
#define TK_HOMUNCULUS_HOME_H 136u
#define TK_HOMUNCULUS_HOME_X ((TK_RIFT_W - TK_HOMUNCULUS_HOME_W) / 2u)
#define TK_HOMUNCULUS_HOME_Y 4u
static uint32_t tk_homunculus_visual_frame;

/* Nearest-neighbor sampling preserves the shared 16-color palette. Coordinates
 * here are viewport-local, not the startup portrait's full-screen position. */
static uint16_t tk_homunculus_visual_pixel(uint32_t x, uint32_t y, uint32_t frame)
{
    uint32_t sx, sy, packed, index;
    if (x < TK_HOMUNCULUS_HOME_X || x >= TK_HOMUNCULUS_HOME_X + TK_HOMUNCULUS_HOME_W ||
        y < TK_HOMUNCULUS_HOME_Y || y >= TK_HOMUNCULUS_HOME_Y + TK_HOMUNCULUS_HOME_H)
        return 0u;
    sx = (x - TK_HOMUNCULUS_HOME_X) * TK_HOMUNCULUS_WIDTH / TK_HOMUNCULUS_HOME_W;
    sy = (y - TK_HOMUNCULUS_HOME_Y) * TK_HOMUNCULUS_HEIGHT / TK_HOMUNCULUS_HOME_H;
    packed = tk_homunculus_frames[frame % TK_HOMUNCULUS_FRAME_COUNT]
        [sy * TK_HOMUNCULUS_ROW_BYTES + sx / 2u];
    index = sx & 1u ? packed & 15u : packed >> 4;
    return swap16(tk_homunculus_palette[index]);
}

static int32_t tk_homunculus_visual_jitter(int32_t value)
{
    return value < -14 ? -14 : value > 14 ? 14 : value;
}

static void tk_homunculus_visual_bolt(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                                     uint16_t core)
{
    tk_rift_add(x0 - 1, y0, x1 - 1, y1, TK_RIFT_VIOLET);
    tk_rift_add(x0 + 1, y0, x1 + 1, y1, TK_RIFT_PURPLE);
    tk_rift_add(x0, y0, x1, y1, core);
}

static void tk_homunculus_visual_lightning(const tk_visual_snapshot_t *sound,
                                           uint32_t now, uint32_t preset)
{
    uint32_t side, i, peak = sound->peak, max_env = 0u, rough = sound->rough >> 10;
    uint32_t gain, energy, phase = now / 90u;
    uint16_t core;
    if (rough > 7u) rough = 7u;
    for (i = 0u; i < TK_VIS_VOICES; i++) {
        uint32_t env = (sound->voice[i] >> 8) & 255u;
        if (env > max_env) max_env = env;
    }
    energy = (peak >> 10) + (max_env >> 5);
    if (energy > 15u) energy = 15u;
    if (peak < 256u) peak = 256u;
    gain = (14u << 12) / peak;
    core = energy > 5u ? TK_RIFT_GREEN_HI : TK_RIFT_GREEN;
    /* Two lightning curtains stay in the side margins, leaving the silhouette,
     * face and its skin animation unobscured. Actual mono/side samples displace
     * different segments; roughness changes branching and peak changes spread. */
    for (side = 0u; side < 2u; side++) {
        int32_t center = side ? 184 : 39;
        int32_t previous_x = center, previous_y = 11;
        for (i = 1u; i <= 12u; i++) {
            uint32_t at = (i * 5u + side * 17u + phase) & 63u;
            int32_t wave = ((int32_t)sound->mono[at] * (int32_t)gain) >> 12;
            int32_t stereo = ((int32_t)sound->side[(at + 13u) & 63u] * (int32_t)gain) >> 13;
            int32_t flicker = (int32_t)((i * 7u + phase * 5u + preset * 3u + side * 11u) % 9u) - 4;
            int32_t x = center + tk_homunculus_visual_jitter(wave + (side ? stereo : -stereo))
                + flicker;
            int32_t y = 11 + (int32_t)i * 10;
            tk_homunculus_visual_bolt(previous_x, previous_y, x, y, core);
            if (!(i % 3u)) {
                int32_t outward = side ? 1 : -1;
                int32_t branch_x = x + outward * (9 + (int32_t)energy);
                int32_t branch_y = y - 5 - (int32_t)rough;
                tk_rift_add(x, y, branch_x, branch_y, TK_RIFT_GREEN);
                tk_rift_add(branch_x, branch_y, branch_x - outward * 4, branch_y - 5,
                    TK_RIFT_PURPLE_HI);
            }
            previous_x = x; previous_y = y;
        }
    }
    /* Discharges emerge beside the hands and meet the side curtains. The
     * portrait-aware raster below keeps every opaque skin pixel in front. */
    tk_homunculus_visual_bolt(74, 105, 57, 96 - (int32_t)rough, core);
    tk_homunculus_visual_bolt(57, 96 - (int32_t)rough, 39, 107 + (int32_t)(energy >> 2), core);
    tk_homunculus_visual_bolt(151, 116, 171, 109 - (int32_t)rough, core);
    tk_homunculus_visual_bolt(171, 109 - (int32_t)rough, 184, 120 + (int32_t)(energy >> 2), core);
    /* Each sounding envelope adds a short discharge. Gate-only kit keys do not
     * keep the field awake; all eight voices have their own pitch placement. */
    for (i = 0u; i < TK_VIS_VOICES; i++) {
        uint32_t voice = sound->voice[i], env = (voice >> 8) & 255u;
        uint32_t pitch = voice & 255u, source = (voice >> 16) & 255u;
        int32_t x, y, outward, length;
        if (!env) continue;
        outward = (i + source) & 1u ? 1 : -1;
        x = outward > 0 ? 162 : 61;
        y = 19 + (int32_t)((pitch * 3u + i * 17u) % 103u);
        length = 7 + (int32_t)(env >> 4);
        tk_homunculus_visual_bolt(x, y, x + outward * length / 2, y - 5,
            env > 96u ? TK_RIFT_GREEN_HI : TK_RIFT_GREEN);
        tk_rift_add(x + outward * length / 2, y - 5, x + outward * length, y + 3,
            TK_RIFT_GREEN);
    }
}

static void tk_homunculus_visual_raster(const tk_rift_line_t *line)
{
    int32_t x = line->x0, y = line->y0;
    int32_t dx = line->x1 > x ? line->x1 - x : x - line->x1;
    int32_t dy = line->y1 > y ? y - line->y1 : line->y1 - y;
    int32_t sx = x < line->x1 ? 1 : -1, sy = y < line->y1 ? 1 : -1;
    int32_t err = dx + dy;
    uint32_t guard = TK_RIFT_W + TK_RIFT_H;
    while (guard--) {
        int32_t twice = 2 * err;
        if (!tk_homunculus_visual_pixel((uint32_t)x, (uint32_t)y, tk_homunculus_visual_frame))
            tk_rift_back[(uint32_t)y * TK_RIFT_W + (uint32_t)x] = line->color;
        if (x == line->x1 && y == line->y1) break;
        if (twice >= dy) { err += dy; x += sx; }
        if (twice <= dx) { err += dx; y += sy; }
    }
}

static void tk_homunculus_visual_reset(void)
{
    tk_rift_reset();
    tk_homunculus_visual_frame = 0u;
}

static void tk_homunculus_visual_prepare(const tk_visual_snapshot_t *sound,
                                         uint32_t now, uint32_t preset)
{
    uint32_t i;
    if (!sound || tk_rift_uploading) return;
    if (!tk_rift_phase) {
        static const uint8_t sequence[4] = {0u, 1u, 2u, 1u};
        uint32_t quiet = sound->peak == 0u;
        for (i = 0u; i < TK_VIS_VOICES; i++) if ((sound->voice[i] >> 8) & 255u) quiet = 0u;
        if (!tk_rift_force) {
            if (tk_rift_asleep && quiet && preset == tk_rift_preset) return;
            if (now - tk_rift_last_ms < (quiet ? 250u : 90u) && preset == tk_rift_preset) return;
        }
        if (!quiet || preset != tk_rift_preset) tk_rift_quiet_frames = 0u;
        else if (tk_rift_quiet_frames < 8u) tk_rift_quiet_frames++;
        tk_rift_asleep = quiet && tk_rift_quiet_frames >= 8u;
        tk_rift_clear = tk_rift_force || tk_rift_asleep;
        tk_rift_last_ms = now;
        tk_rift_preset = preset;
        tk_rift_line_count = tk_rift_line_at = tk_rift_row = 0u;
        tk_homunculus_visual_frame = quiet ? 0u : sequence[(now / 240u) & 3u];
        if (!quiet) tk_homunculus_visual_lightning(sound, now, preset);
        tk_rift_phase = 1u;
    }
    if (tk_rift_phase == 1u) {
        uint32_t y, x, first_y = tk_rift_row * TK_RIFT_TILE_H;
        /* At most eight rows per call; no full-frame decode or new framebuffer.
         * Repaint the portrait rectangle directly so earlier skin positions
         * cannot leave trails. Only the surrounding lightning retains echoes. */
        for (y = first_y; y < first_y + TK_RIFT_TILE_H; y++) {
            for (x = 0u; x < TK_RIFT_W; x++) {
                uint32_t at = y * TK_RIFT_W + x;
                if (x >= TK_HOMUNCULUS_HOME_X && x < TK_HOMUNCULUS_HOME_X + TK_HOMUNCULUS_HOME_W &&
                    y >= TK_HOMUNCULUS_HOME_Y && y < TK_HOMUNCULUS_HOME_Y + TK_HOMUNCULUS_HOME_H)
                    tk_rift_back[at] = tk_homunculus_visual_pixel(x, y, tk_homunculus_visual_frame);
                else tk_rift_back[at] = tk_rift_clear ? 0u : tk_rift_echo(tk_rift_front[at]);
            }
        }
        if (++tk_rift_row == TK_RIFT_ROWS) { tk_rift_row = 0u; tk_rift_phase = 2u; }
    } else if (tk_rift_phase == 2u) {
        uint32_t end = tk_rift_line_at + 24u;
        if (end > tk_rift_line_count) end = tk_rift_line_count;
        while (tk_rift_line_at < end) tk_homunculus_visual_raster(&tk_rift_line[tk_rift_line_at++]);
        if (tk_rift_line_at == tk_rift_line_count) tk_rift_phase = 3u;
    } else {
        uint32_t column, start = tk_rift_row * TK_RIFT_TILE_H * TK_RIFT_W;
        uint16_t dirty = 0u;
        for (column = 0u; column < TK_RIFT_COLS; column++) {
            uint32_t y, x, changed = tk_rift_force;
            for (y = 0u; y < TK_RIFT_TILE_H && !changed; y++) {
                uint32_t offset = start + y * TK_RIFT_W + column * TK_RIFT_TILE_W;
                for (x = 0u; x < TK_RIFT_TILE_W; x++)
                    if (tk_rift_front[offset + x] != tk_rift_back[offset + x]) { changed = 1u; break; }
            }
            if (changed) dirty |= (uint16_t)(1u << column);
        }
        tk_rift_dirty[tk_rift_row] = dirty;
        if (++tk_rift_row == TK_RIFT_ROWS) {
            tk_rift_phase = tk_rift_row = 0u;
            tk_rift_uploading = 0u;
            for (i = 0u; i < TK_RIFT_ROWS; i++) if (tk_rift_dirty[i]) tk_rift_uploading = 1u;
            tk_rift_force = 0u;
        }
    }
}

static int tk_homunculus_visual_pending(void) { return tk_rift_pending(); }
static uint32_t tk_homunculus_visual_upload(void) { return tk_rift_upload(); }

#endif
