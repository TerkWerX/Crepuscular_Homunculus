/* SPDX-License-Identifier: GPL-3.0-only */
/* THORN RIFT: a main-loop-only rendering of TERKINSTEIN's stereo sound.
 * The opposing fractured plates are a visual composition, not a spectrum or
 * measurement instrument. Actual waveform samples deform their edges; active
 * pitches stitch the green breach. Include after gfx, engine and visual capture.
 *
 * prepare() is deliberately cooperative: call it each main-loop pass. It builds
 * one short row/line batch, then marks tiles. upload() sends at most 512 pixels;
 * its caller must poll and check musical deadlines between uploads. No display
 * work, voice-state reads, flash, or PCM modification belongs in this header.
 */
#ifndef TERKINSTEIN_RIFT_H
#define TERKINSTEIN_RIFT_H

#include <stdint.h>

#define TK_RIFT_X 8u
#define TK_RIFT_Y 70u
#define TK_RIFT_W 224u
#define TK_RIFT_H 144u
#define TK_RIFT_TILE_W 16u
#define TK_RIFT_TILE_H 8u
#define TK_RIFT_COLS (TK_RIFT_W / TK_RIFT_TILE_W)
#define TK_RIFT_ROWS (TK_RIFT_H / TK_RIFT_TILE_H)
#define TK_RIFT_LINE_MAX 640u

#define TK_RIFT_VIOLET_DIM RGB(40, 8, 64)
#define TK_RIFT_VIOLET RGB(87, 22, 137)
#define TK_RIFT_PURPLE RGB(174, 55, 247)
#define TK_RIFT_PURPLE_HI RGB(210, 102, 255)
#define TK_RIFT_GREEN_DIM RGB(18, 78, 28)
#define TK_RIFT_GREEN RGB(96, 240, 65)
#define TK_RIFT_GREEN_HI RGB(164, 255, 125)

/* Front tracks pixels actually sent to the LCD. Back remains stable throughout
 * tile upload; the existing canvas is a small packed DMA staging buffer. */
static uint16_t tk_rift_front[TK_RIFT_W * TK_RIFT_H] __attribute__((section(".pool")));
static uint16_t tk_rift_back[TK_RIFT_W * TK_RIFT_H] __attribute__((section(".pool")));
typedef struct {
    int16_t x0, y0, x1, y1;
    uint16_t color;
} tk_rift_line_t;
static tk_rift_line_t tk_rift_line[TK_RIFT_LINE_MAX];
static uint16_t tk_rift_dirty[TK_RIFT_ROWS];
static uint32_t tk_rift_line_count, tk_rift_line_at, tk_rift_row;
static uint32_t tk_rift_phase, tk_rift_uploading, tk_rift_force = 1u;
static uint32_t tk_rift_last_ms, tk_rift_preset, tk_rift_quiet_frames, tk_rift_asleep;
static uint32_t tk_rift_clear;

static int32_t tk_rift_clamp(int32_t value, int32_t maximum)
{
    return value < 0 ? 0 : value > maximum ? maximum : value;
}

static void tk_rift_add(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t color)
{
    tk_rift_line_t *line;
    if (tk_rift_line_count >= TK_RIFT_LINE_MAX) return;
    line = &tk_rift_line[tk_rift_line_count++];
    line->x0 = (int16_t)tk_rift_clamp(x0, (int32_t)TK_RIFT_W - 1);
    line->x1 = (int16_t)tk_rift_clamp(x1, (int32_t)TK_RIFT_W - 1);
    line->y0 = (int16_t)tk_rift_clamp(y0, (int32_t)TK_RIFT_H - 1);
    line->y1 = (int16_t)tk_rift_clamp(y1, (int32_t)TK_RIFT_H - 1);
    line->color = swap16(color);
}

static void tk_rift_raster(const tk_rift_line_t *line)
{
    int32_t x = line->x0, y = line->y0;
    int32_t dx = line->x1 > x ? line->x1 - x : x - line->x1;
    int32_t dy = line->y1 > y ? y - line->y1 : line->y1 - y;
    int32_t sx = x < line->x1 ? 1 : -1, sy = y < line->y1 ? 1 : -1;
    int32_t err = dx + dy;
    uint32_t guard = TK_RIFT_W + TK_RIFT_H;
    while (guard--) {
        int32_t twice = 2 * err;
        tk_rift_back[(uint32_t)y * TK_RIFT_W + (uint32_t)x] = line->color;
        if (x == line->x1 && y == line->y1) break;
        if (twice >= dy) { err += dy; x += sx; }
        if (twice <= dx) { err += dx; y += sy; }
    }
}

/* A lost green impulse becomes a purple echo. Integer RGB565 decay reaches
 * black in finitely many frames; there is no permanent low-level sparkle. */
static uint16_t tk_rift_echo(uint16_t swapped)
{
    uint32_t color = swap16(swapped), r = color >> 11;
    uint32_t g = (color >> 5) & 63u, b = color & 31u, value;
    if (!color) return 0;
    if (g > r + b) {
        value = (g * 3u) >> 3;
        if (value > 23u) value = 23u;
        r = value * 3u >> 2;
        b = value;
        g = value >> 2;
    } else {
        r = r * 3u >> 2;
        g = g * 3u >> 2;
        b = b * 3u >> 2;
    }
    return swap16((r << 11) | (g << 5) | b);
}

static void tk_rift_scar(uint32_t preset)
{
    static const int8_t cut[] = {0, -3, 2, -1, 4, -2, 1, 0};
    int32_t i, previous_x = 13, previous_y = 64 + (int32_t)(preset % 5u);
    for (i = 1; i < 8; i++) {
        int32_t x = 13 + i * 28;
        int32_t y = 64 + i + (int32_t)(preset % 5u) + cut[i];
        tk_rift_add(previous_x, previous_y, x, y, TK_RIFT_VIOLET_DIM);
        previous_x = x; previous_y = y;
    }
    tk_rift_add(31, 53, 63, 63, TK_RIFT_VIOLET_DIM);
    tk_rift_add(162, 83, 194, 93, TK_RIFT_VIOLET_DIM);
}

static void tk_rift_geometry(const tk_visual_snapshot_t *sound, uint32_t now, uint32_t preset)
{
    int16_t x[64], upper[64], lower[64], breach[64];
    uint32_t i, v, peak = sound->peak, gain, rough = sound->rough >> 9;
    int32_t opening, drift;
    if (peak < 256u) peak = 256u;
    gain = (18u << 12) / peak;
    opening = 7 + (int32_t)(sound->peak >> 8);
    if (opening > 30) opening = 30;
    if (rough > 7u) rough = 7u;
    drift = tk_sine(now * 786432u + preset * 118489089u) >> 13;
    for (i = 0; i < 64u; i++) {
        int32_t wave = (sound->mono[i] * (int32_t)gain) >> 12;
        int32_t stereo = (sound->side[i] * (int32_t)gain) >> 13;
        int32_t middle;
        if (wave > 21) wave = 21; else if (wave < -21) wave = -21;
        if (stereo > 10) stereo = 10; else if (stereo < -10) stereo = -10;
        x[i] = (int16_t)(8u + i * 207u / 63u);
        middle = 71 + ((x[i] - 112) * 11) / 112 + drift;
        upper[i] = (int16_t)(middle - opening + (wave >> 1) - stereo);
        lower[i] = (int16_t)(middle + opening + (wave * 3 >> 2) + stereo);
        breach[i] = (int16_t)(middle + (wave * 3 >> 2) + (stereo >> 1));
    }
    /* Purple plates oppose one another without forming a closed creature,
     * ring, radial spectrum or orbit. Their folds span the full viewport. */
    for (i = 0; i < 7u; i++) {
        uint32_t k = 3u + i * 8u, end = k + 7u;
        int32_t left = x[k] - 8, right = x[end] + 5;
        int32_t top = 6 + (int32_t)((i * 7u + preset * 3u) % 17u);
        int32_t bottom = 137 - (int32_t)((i * 11u + preset) % 15u);
        int32_t fold = (left + right) / 2 + (i & 1u ? 4 : -5);
        tk_rift_add(left, top + 7, fold, top, TK_RIFT_PURPLE);
        tk_rift_add(fold, top, right, upper[end] - 5, TK_RIFT_VIOLET);
        tk_rift_add(right, upper[end] - 5, left, upper[k] - 2, TK_RIFT_PURPLE);
        tk_rift_add(left, upper[k] - 2, left, top + 7, TK_RIFT_VIOLET);
        tk_rift_add(fold, top, x[k + 4u], upper[k + 4u] - 4, TK_RIFT_PURPLE_HI);
        tk_rift_add(left + 4, top + 11, x[k + 5u], upper[k + 5u] - 8, TK_RIFT_VIOLET_DIM);
        tk_rift_add(left, bottom - 4, fold + 5, bottom, TK_RIFT_PURPLE);
        tk_rift_add(fold + 5, bottom, right, lower[end] + 4, TK_RIFT_VIOLET);
        tk_rift_add(right, lower[end] + 4, left, lower[k] + 3, TK_RIFT_PURPLE);
        tk_rift_add(left, lower[k] + 3, left, bottom - 4, TK_RIFT_VIOLET);
        tk_rift_add(fold + 5, bottom, x[k + 2u], lower[k + 2u] + 4, TK_RIFT_PURPLE_HI);
        tk_rift_add(right - 3, bottom - 9, x[k + 1u], lower[k + 1u] + 8, TK_RIFT_VIOLET_DIM);
    }
    for (i = 1; i < 64u; i++) {
        tk_rift_add(x[i - 1u], upper[i - 1u], x[i], upper[i], TK_RIFT_PURPLE);
        tk_rift_add(x[i - 1u], lower[i - 1u], x[i], lower[i], TK_RIFT_PURPLE_HI);
        tk_rift_add(x[i - 1u], breach[i - 1u], x[i], breach[i], TK_RIFT_GREEN_HI);
        tk_rift_add(x[i - 1u], breach[i - 1u] + 1, x[i], breach[i] + 1, TK_RIFT_GREEN);
        tk_rift_add(x[i - 1u], (breach[i - 1u] + upper[i - 1u]) / 2,
            x[i], (breach[i] + upper[i]) / 2, TK_RIFT_GREEN_DIM);
        tk_rift_add(x[i - 1u], (breach[i - 1u] + lower[i - 1u]) / 2,
            x[i], (breach[i] + lower[i]) / 2, TK_RIFT_GREEN_DIM);
        if (!(i & 3u) && rough) {
            tk_rift_add(x[i], upper[i], x[i] - (int32_t)rough, upper[i] - (int32_t)rough - 3,
                TK_RIFT_PURPLE_HI);
            tk_rift_add(x[i], lower[i], x[i] + (int32_t)rough, lower[i] + (int32_t)rough + 2,
                TK_RIFT_PURPLE);
        }
    }
    /* Pitch positions each stitch; actual envelope, not a held-key bit,
     * determines its length and brilliance. A held one-shot therefore fades. */
    for (v = 0; v < TK_VIS_VOICES; v++) {
        uint32_t voice = sound->voice[v], env = (voice >> 8) & 255u;
        uint32_t pitch = voice & 255u, source = (voice >> 16) & 255u;
        uint32_t k = 3u + (pitch % 36u) * 57u / 35u;
        int32_t stitch, sx, sy, tilt, flicker;
        uint16_t color;
        if (!env) continue;
        stitch = 3 + (int32_t)(env >> 4);
        sx = x[k] + (int32_t)v - 2;
        sy = breach[k];
        tilt = source == TK_SOURCE_ATMOS ? -3 : source == TK_SOURCE_BASS ? 3 : (int32_t)(pitch % 5u) - 2;
        flicker = tk_sine(now * 3145728u + pitch * 127489089u) >> 14;
        color = env > 96u ? TK_RIFT_GREEN_HI : TK_RIFT_GREEN;
        tk_rift_add(sx - tilt, sy - stitch, sx + tilt, sy + stitch, color);
        tk_rift_add(sx - tilt - 3, sy - stitch + 2, sx - tilt + 3, sy - stitch - 2, TK_RIFT_GREEN);
        tk_rift_add(sx + tilt - 3, sy + stitch + 2, sx + tilt + 3, sy + stitch - 2, TK_RIFT_GREEN);
        tk_rift_add(sx - 4, sy + flicker, sx + 4, sy - flicker, color);
        tk_rift_add(sx + 7, sy - stitch - 4, sx + 10, sy - stitch - 7, TK_RIFT_GREEN_DIM);
    }
}

static void tk_rift_reset(void)
{
    uint32_t row;
    tk_rift_phase = tk_rift_uploading = tk_rift_asleep = 0u;
    tk_rift_quiet_frames = tk_rift_line_count = tk_rift_line_at = 0u;
    tk_rift_force = 1u;
    for (row = 0; row < TK_RIFT_ROWS; row++) tk_rift_dirty[row] = 0u;
}

static void tk_rift_prepare(const tk_visual_snapshot_t *sound, uint32_t now, uint32_t preset)
{
    uint32_t i;
    if (!sound || tk_rift_uploading) return;
    if (!tk_rift_phase) {
        uint32_t quiet = sound->peak == 0u;
        for (i = 0; i < TK_VIS_VOICES; i++) if ((sound->voice[i] >> 8) & 255u) quiet = 0u;
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
        if (quiet) tk_rift_scar(preset);
        else tk_rift_geometry(sound, now, preset);
        tk_rift_phase = 1u;
    }
    if (tk_rift_phase == 1u) {
        uint32_t start = tk_rift_row * TK_RIFT_TILE_H * TK_RIFT_W;
        uint32_t end = start + TK_RIFT_TILE_H * TK_RIFT_W;
        for (i = start; i < end; i++)
            tk_rift_back[i] = tk_rift_clear ? 0u : tk_rift_echo(tk_rift_front[i]);
        if (++tk_rift_row == TK_RIFT_ROWS) { tk_rift_row = 0u; tk_rift_phase = 2u; }
    } else if (tk_rift_phase == 2u) {
        uint32_t end = tk_rift_line_at + 24u;
        if (end > tk_rift_line_count) end = tk_rift_line_count;
        while (tk_rift_line_at < end) tk_rift_raster(&tk_rift_line[tk_rift_line_at++]);
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

static int tk_rift_pending(void) { return tk_rift_uploading != 0u; }

static uint32_t tk_rift_upload(void)
{
    uint32_t row, column, count, width, y, x;
    uint16_t mask;
    if (!tk_rift_uploading) return 0u;
    for (row = 0u; row < TK_RIFT_ROWS && !tk_rift_dirty[row]; row++) {}
    if (row == TK_RIFT_ROWS) { tk_rift_uploading = 0u; return 0u; }
    for (column = 0u; !(tk_rift_dirty[row] & (1u << column)); column++) {}
    count = 1u;
    while (count < 4u && column + count < TK_RIFT_COLS &&
        (tk_rift_dirty[row] & (1u << (column + count)))) count++;
    width = count * TK_RIFT_TILE_W;
    cv_begin(width, TK_RIFT_TILE_H, C_BLACK);
    for (y = 0u; y < TK_RIFT_TILE_H; y++) {
        uint32_t source = (row * TK_RIFT_TILE_H + y) * TK_RIFT_W + column * TK_RIFT_TILE_W;
        for (x = 0u; x < width; x++) {
            uint16_t pixel = tk_rift_back[source + x];
            cv_px[y * width + x] = pixel;
            tk_rift_front[source + x] = pixel;
        }
    }
    cv_blit(TK_RIFT_X + column * TK_RIFT_TILE_W, TK_RIFT_Y + row * TK_RIFT_TILE_H);
    mask = (uint16_t)(((1u << count) - 1u) << column);
    tk_rift_dirty[row] &= (uint16_t)~mask;
    tk_rift_uploading = 0u;
    for (row = 0u; row < TK_RIFT_ROWS; row++) if (tk_rift_dirty[row]) tk_rift_uploading = 1u;
    return width * TK_RIFT_TILE_H;
}

#endif
