/* SPDX-License-Identifier: GPL-3.0-only */
/* Behavioral checks for the production THORN RIFT renderer using a guarded
 * LCD/canvas substitute. These checks do not qualify physical SPI timing. */
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _MSC_VER
#define __attribute__(x)
#endif

#define RGB(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))
#define C_BLACK 0u
#define TEST_GUARD 0x7193A5C6u
static uint32_t checks, lcd_pixels, lcd_batches, cv_w, cv_h;
static struct { uint32_t before; uint16_t pixels[512]; uint32_t after; } canvas;
static struct { uint32_t before; uint16_t pixels[240 * 240]; uint32_t after; } panel;
#define cv_px canvas.pixels

static void expect(int condition, const char *description)
{
    checks++;
    if (!condition) { fprintf(stderr, "FAIL: %s\n", description); exit(1); }
}
static uint16_t swap16(uint32_t color)
{
    return (uint16_t)(((color >> 8) & 255u) | ((color & 255u) << 8));
}
static void cv_begin(uint32_t width, uint32_t height, uint16_t background);
static void cv_blit(uint32_t x, uint32_t y);

#include "../firmware/src/terkinstein_engine.h"
#include "../firmware/src/terkinstein_visual.h"
#include "../firmware/src/terkinstein_rift.h"

static uint16_t staged[TK_RIFT_W * TK_RIFT_H];

static void cv_begin(uint32_t width, uint32_t height, uint16_t background)
{
    uint32_t i;
    expect(width && width <= 64u && height == 8u, "upload staging is at most four small tiles");
    expect(width * height <= 512u, "staging stays within the guarded canvas");
    cv_w = width; cv_h = height;
    for (i = 0u; i < width * height; i++) canvas.pixels[i] = swap16(background);
}
static void cv_blit(uint32_t x, uint32_t y)
{
    uint32_t row, column;
    expect(x >= TK_RIFT_X && y >= TK_RIFT_Y, "upload starts inside the rift viewport");
    expect(x + cv_w <= TK_RIFT_X + TK_RIFT_W && y + cv_h <= TK_RIFT_Y + TK_RIFT_H,
           "upload ends inside the rift viewport");
    expect((x - TK_RIFT_X) % TK_RIFT_TILE_W == 0u &&
           (y - TK_RIFT_Y) % TK_RIFT_TILE_H == 0u, "upload follows the tile grid");
    expect(cv_w * cv_h <= 512u, "each physical transfer stays within the pixel budget");
    for (row = 0u; row < cv_h; row++) {
        for (column = 0u; column < cv_w; column++) {
            uint32_t local = (y - TK_RIFT_Y + row) * TK_RIFT_W + x - TK_RIFT_X + column;
            uint16_t pixel = canvas.pixels[row * cv_w + column];
            expect(pixel == tk_rift_back[local], "staged DMA pixels match the completed back image");
            expect(pixel == tk_rift_front[local], "front records exactly the pixels being sent");
            panel.pixels[(y + row) * 240u + x + column] = pixel;
        }
    }
    lcd_pixels += cv_w * cv_h;
    lcd_batches++;
}

static void guards_and_display(void)
{
    uint32_t x, y;
    expect(canvas.before == TEST_GUARD && canvas.after == TEST_GUARD, "canvas guards survive every upload");
    expect(panel.before == TEST_GUARD && panel.after == TEST_GUARD, "LCD guards survive every upload");
    for (y = 0u; y < 240u; y++) {
        for (x = 0u; x < 240u; x++) {
            if (x < TK_RIFT_X || x >= TK_RIFT_X + TK_RIFT_W ||
                y < TK_RIFT_Y || y >= TK_RIFT_Y + TK_RIFT_H)
                expect(panel.pixels[y * 240u + x] == 0xBEEF, "rift leaves title/footer and outside pixels alone");
            else
                expect(panel.pixels[y * 240u + x] == tk_rift_front[(y - TK_RIFT_Y) * TK_RIFT_W + x - TK_RIFT_X],
                       "the display and tracked front image agree");
        }
    }
}
static uint32_t picture_hash(void)
{
    uint32_t hash = 2166136261u, i;
    for (i = 0u; i < TK_RIFT_W * TK_RIFT_H; i++) hash = (hash ^ tk_rift_front[i]) * 16777619u;
    return hash;
}

/* Complete one requested frame. During upload, try to replace it with wildly
 * different sound and time: a pending image must remain entirely immutable. */
static uint32_t frame(const tk_visual_snapshot_t *sound, uint32_t now, uint32_t preset)
{
    tk_visual_snapshot_t interruption;
    uint32_t i, before = lcd_pixels, complete = 0u;
    memset(&interruption, 0xFF, sizeof interruption);
    for (i = 0u; i < 256u; i++) {
        tk_rift_prepare(sound, now + i, preset);
        expect(tk_rift_line_count <= TK_RIFT_LINE_MAX, "geometry remains within its bounded line list");
        if (tk_rift_pending()) {
            expect(tk_rift_phase == 0u, "upload waits for all construction phases to finish");
            complete = 1u;
            break;
        }
        if (!tk_rift_phase && !tk_rift_force) { complete = 1u; break; }
    }
    expect(complete, "cooperative construction completes within a bounded number of calls");
    memcpy(staged, tk_rift_back, sizeof staged);
    for (i = 0u; tk_rift_pending() && i < 256u; i++) {
        uint32_t pixels, last_ms = tk_rift_last_ms, old_preset = tk_rift_preset;
        uint32_t sent_before = lcd_pixels;
        tk_rift_prepare(&interruption, now + 100000u, preset + 1u);
        expect(memcmp(staged, tk_rift_back, sizeof staged) == 0, "pending back image cannot be regenerated");
        expect(last_ms == tk_rift_last_ms && old_preset == tk_rift_preset,
               "pending uploads defer scene time and preset changes");
        pixels = tk_rift_upload();
        expect(pixels > 0u && pixels <= 512u, "one upload has a bounded nonzero pixel count");
        expect(lcd_pixels - sent_before == pixels, "upload return value describes the actual transfer");
        expect(memcmp(staged, tk_rift_back, sizeof staged) == 0, "upload never changes the source image");
    }
    expect(!tk_rift_pending(), "finite dirty tiles finish without a busy loop");
    expect(!tk_rift_upload(), "upload with no pending tiles is a no-op");
    guards_and_display();
    return lcd_pixels - before;
}

static void quiet_and_reentry(void)
{
    tk_visual_snapshot_t silent, sound;
    uint32_t t, hash, before;
    memset(&silent, 0, sizeof silent);
    /* A held kit key's gate survives its one-shot envelope, but its actual
     * silent state must still settle. Gate alone is deliberately ignored. */
    silent.voice[0] = 60u | (TK_SOURCE_DRUM << 16) | (1u << 24);
    tk_rift_reset();
    expect(!tk_rift_pending(), "reset cancels outstanding upload state");
    expect(frame(&silent, 0u, 0u) == TK_RIFT_W * TK_RIFT_H, "entry repaints every viewport pixel");
    for (t = 500u; t <= 5000u; t += 500u) frame(&silent, t, 0u);
    expect(tk_rift_asleep != 0u, "actual silence reaches a static sleeping image");
    hash = picture_hash(); before = lcd_pixels;
    for (t = 5001u; t < 9000u; t++) {
        tk_rift_prepare(&silent, t, 0u);
        expect(!tk_rift_pending() && !tk_rift_phase, "held silent key produces no idle work");
        expect(!tk_rift_upload(), "settled silence produces no LCD transfer");
    }
    expect(lcd_pixels == before && picture_hash() == hash, "settled scar remains static with zero LCD traffic");
    expect(frame(&silent, 9000u, 1u) > 0u, "preset change updates a sleeping scar");
    expect(picture_hash() != hash, "sleeping preset change visibly changes the composition");

    sound = silent;
    sound.peak = 6000u;
    sound.voice[0] = 64u | (220u << 8) | (TK_SOURCE_ATMOS << 16) | (1u << 24);
    for (t = 0u; t < 64u; t++) sound.mono[t] = (int16_t)(tk_sine(t * 201326592u) >> 2);
    expect(frame(&sound, 10000u, 1u) > 0u, "sound wakes a settled scene");
    expect(!tk_rift_asleep && picture_hash() != hash, "actual sound creates an animated waveform composition");
    tk_rift_reset();
    expect(frame(&sound, 11000u, 1u) == TK_RIFT_W * TK_RIFT_H,
           "re-entry forces a full repaint even when the prior field was similar");
}

static uint32_t independent_picture(tk_visual_snapshot_t *sound)
{
    tk_rift_reset();
    frame(sound, 1000u, 3u);
    return picture_hash();
}

static void sound_inputs(void)
{
    tk_visual_snapshot_t sound;
    uint32_t base, changed, i;
    memset(&sound, 0, sizeof sound);
    sound.peak = 6000u;
    base = independent_picture(&sound);
    for (i = 0u; i < 64u; i++) sound.mono[i] = (int16_t)(tk_sine(i * 134217728u) >> 3);
    changed = independent_picture(&sound);
    expect(changed != base, "mono PCM independently deforms the breach");
    base = changed;
    for (i = 0u; i < 64u; i++) sound.side[i] = (int16_t)(tk_sine(i * 335544320u) >> 3);
    changed = independent_picture(&sound);
    expect(changed != base, "stereo side PCM independently distorts opposed edges");
    base = changed;
    sound.voice[0] = 59u | (255u << 8) | (TK_SOURCE_ATMOS << 16) | (1u << 24);
    changed = independent_picture(&sound);
    expect(changed != base, "sounding envelope metadata adds a pitch stitch");
    sound.voice[0] = 71u | (255u << 8) | (TK_SOURCE_ATMOS << 16) | (1u << 24);
    base = independent_picture(&sound);
    expect(base != changed, "pitch metadata changes stitch position");
    sound.voice[0] = 71u | (TK_SOURCE_ATMOS << 16) | (1u << 24);
    base = independent_picture(&sound);
    sound.voice[0] = 0u;
    expect(independent_picture(&sound) == base, "gate without actual envelope creates no stitch");
    memset(&sound, 0, sizeof sound);
    sound.peak = 1u;
    base = independent_picture(&sound);
    sound.peak = 12000u;
    expect(independent_picture(&sound) != base, "sound peak independently opens the seam");
}

static void extremes_and_reset_midframe(void)
{
    tk_visual_snapshot_t sound;
    uint32_t i, iteration, phase, times[] = {0u, UINT32_MAX - 100u, UINT32_MAX, 50u};
    memset(&sound, 0, sizeof sound);
    sound.rough = UINT16_MAX;
    for (i = 0u; i < 64u; i++) {
        sound.mono[i] = (int16_t)(i & 1u ? INT16_MIN : INT16_MAX);
        sound.side[i] = (int16_t)(i & 1u ? INT16_MAX : INT16_MIN);
    }
    for (i = 0u; i < TK_VIS_VOICES; i++) sound.voice[i] = UINT32_MAX;
    for (iteration = 0u; iteration < 4u; iteration++) {
        sound.peak = (uint16_t)(iteration & 1u ? UINT16_MAX : 0u);
        tk_rift_reset();
        frame(&sound, times[iteration], UINT32_MAX);
        for (i = 0u; i < tk_rift_line_count; i++) {
            expect(tk_rift_line[i].x0 >= 0 && tk_rift_line[i].x0 < (int32_t)TK_RIFT_W &&
                   tk_rift_line[i].x1 >= 0 && tk_rift_line[i].x1 < (int32_t)TK_RIFT_W,
                   "extreme inputs leave all line endpoints inside horizontal bounds");
            expect(tk_rift_line[i].y0 >= 0 && tk_rift_line[i].y0 < (int32_t)TK_RIFT_H &&
                   tk_rift_line[i].y1 >= 0 && tk_rift_line[i].y1 < (int32_t)TK_RIFT_H,
                   "extreme inputs leave all line endpoints inside vertical bounds");
        }
    }
    tk_rift_reset();
    tk_rift_prepare(&sound, 1000u, 3u);
    phase = tk_rift_phase;
    expect(phase != 0u && !tk_rift_pending(), "partially built frame is not published");
    tk_rift_prepare(NULL, 2000u, 4u);
    expect(tk_rift_phase == phase, "null snapshot cannot disturb a partial frame");
    tk_rift_reset();
    expect(!tk_rift_phase && !tk_rift_pending(), "reset discards cooperative construction");
    expect(frame(&sound, 3000u, 5u) == TK_RIFT_W * TK_RIFT_H, "reset during construction still repaints the viewport");
    tk_rift_reset();
    for (i = 0u; i < 256u && !tk_rift_pending(); i++) tk_rift_prepare(&sound, 4000u + i, 5u);
    expect(tk_rift_pending(), "test reaches a pending upload frame");
    expect(tk_rift_upload() > 0u, "test sends part of the pending frame");
    tk_rift_reset();
    expect(!tk_rift_pending(), "reset cancels unsent dirty tiles");
    expect(frame(&sound, 5000u, 5u) == TK_RIFT_W * TK_RIFT_H, "reset during upload repairs the complete viewport");
}

static void final_voice_stitches_and_wakes(void)
{
    tk_visual_snapshot_t silent, sound;
    uint32_t t, base, lines;
    memset(&silent, 0, sizeof silent);
    expect(TK_VIS_VOICES == TK_VOICES && TK_VOICES == 8u,
           "rift metadata capacity matches all eight engine voices");
    tk_rift_reset();
    frame(&silent, 0u, 0u);
    for (t = 500u; t <= 5000u; t += 500u) frame(&silent, t, 0u);
    expect(tk_rift_asleep, "fixture settles before testing the last voice alone");
    sound = silent;
    sound.voice[7] = 67u | (220u << 8) | (TK_SOURCE_ATMOS << 16) | (1u << 24);
    expect(frame(&sound, 6000u, 0u) > 0u && !tk_rift_asleep,
           "voice eight's envelope wakes the rift even when sampled PCM is zero");
    sound.peak = 6000u;
    base = independent_picture(&sound);
    lines = tk_rift_line_count;
    sound.voice[7] = 0u;
    expect(independent_picture(&sound) != base, "voice eight contributes a visible pitch stitch");
    expect(tk_rift_line_count + 5u == lines,
           "the final voice adds its complete five-line stitch without overflowing geometry");
    base = independent_picture(&sound);
    sound.voice[6] = 71u | (220u << 8) | (TK_SOURCE_BASS << 16) | (1u << 24);
    expect(independent_picture(&sound) != base, "voice seven independently contributes its own stitch");
}

int main(void)
{
    uint32_t i;
    canvas.before = canvas.after = panel.before = panel.after = TEST_GUARD;
    for (i = 0u; i < 240u * 240u; i++) panel.pixels[i] = 0xBEEF;
    quiet_and_reentry();
    sound_inputs();
    extremes_and_reset_midframe();
    final_voice_stitches_and_wakes();
    printf("THORN RIFT checks passed: %u checks; %u bounded tile batches\n", checks, lcd_batches);
    return 0;
}
