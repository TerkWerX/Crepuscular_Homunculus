/* SPDX-License-Identifier: GPL-3.0-only */
/* Focused cooperative intro checks; no app main, device, filesystem or DSP. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t fm1_ms;
static uint16_t cv_px[240u * 4u];
static uint16_t display[240u * 240u];
static uint16_t pending_pixels[240u * 4u];
static uint32_t cv_w, cv_h, blits, max_rows, pending_count;
typedef struct { uint32_t h; } test_font_t;
static const test_font_t FONT_S = {16u};
#define C_BLACK 0u
#define RGB(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))
static uint16_t swap16(uint32_t v) { return (uint16_t)((v >> 8) | ((v & 255u) << 8)); }

static void lcd_sync(void)
{
    /* An earlier transfer must see an unchanged staging buffer until waited. */
    assert(!memcmp(pending_pixels, cv_px, pending_count * sizeof(uint16_t)));
    pending_count = 0;
}

static void cv_begin(uint32_t width, uint32_t height, uint16_t color)
{
    uint32_t i;
    assert(width && width <= 240u && height && height <= 4u);
    assert(width * height <= sizeof(cv_px) / sizeof(cv_px[0]));
    lcd_sync();
    cv_w = width;
    cv_h = height;
    for (i = 0; i < width * height; i++) cv_px[i] = swap16(color);
}

static void cv_blit(uint32_t x, uint32_t y)
{
    uint32_t dx, dy;
    assert(x + cv_w <= 240u && y + cv_h <= 240u);
    assert(!pending_count);
    for (dy = 0; dy < cv_h; dy++)
        for (dx = 0; dx < cv_w; dx++)
            display[(y + dy) * 240u + x + dx] = swap16(cv_px[dy * cv_w + dx]);
    pending_count = cv_w * cv_h;
    memcpy(pending_pixels, cv_px, pending_count * sizeof(uint16_t));
    if (max_rows < cv_h) max_rows = cv_h;
    blits++;
}

static int32_t text_w(const test_font_t *font, const char *text)
{
    (void)font;
    return (int32_t)strlen(text) * 8;
}

/* A clipped solid proxy glyph suffices here; actual font/layout is checked in
 * the full host harness. It ensures later crop uploads leave title pixels. */
static void cv_text(int32_t x, int32_t y, const test_font_t *font, const char *text, uint16_t color)
{
    int32_t dx, dy, width = text_w(font, text);
    for (dy = 0; dy < (int32_t)font->h; dy++)
        for (dx = 0; dx < width; dx++)
            if ((uint32_t)(x + dx) < cv_w && (uint32_t)(y + dy) < cv_h)
                cv_px[(uint32_t)(y + dy) * cv_w + (uint32_t)(x + dx)] = swap16(color);
}

#include "terkinstein_intro.h"

static void assert_frame(uint32_t frame)
{
    uint32_t x, y;
    for (y = 0; y < TK_HOMUNCULUS_HEIGHT; y++)
        for (x = 0; x < TK_HOMUNCULUS_WIDTH; x++) {
            uint32_t byte = tk_homunculus_frames[frame][y * TK_HOMUNCULUS_ROW_BYTES + x / 2u];
            uint32_t color = (x & 1u) ? byte % 16u : byte / 16u;
            assert(display[(TK_HOMUNCULUS_Y + y) * 240u + TK_HOMUNCULUS_X + x]
                == tk_homunculus_palette[color]);
        }
}

static void assert_surrounding(void)
{
    uint32_t x, y;
    int32_t text_left = (240 - text_w(&FONT_S, "CREPUSCULAR")) / 2;
    int32_t text_right = (240 - text_w(&FONT_S, "HOMUNCULUS")) / 2;
    for (y = 0; y < 240; y++)
        for (x = 0; x < 240; x++) {
            uint16_t expected = 0;
            if (x >= TK_HOMUNCULUS_X && x < TK_HOMUNCULUS_X + TK_HOMUNCULUS_WIDTH &&
                y >= TK_HOMUNCULUS_Y && y < TK_HOMUNCULUS_Y + TK_HOMUNCULUS_HEIGHT) continue;
            if (y >= 194u && y < 210u && x >= (uint32_t)text_left && x < (uint32_t)text_left + 88u)
                expected = RGB(168, 190, 124);
            if (y >= 214u && y < 230u && x >= (uint32_t)text_right && x < (uint32_t)text_right + 80u)
                expected = RGB(140, 112, 159);
            assert(display[y * 240u + x] == expected);
        }
}

static void exercise(uint32_t start)
{
    tk_intro_t intro = {0};
    uint32_t i, previous;
    memset(display, 0x7c, sizeof(display));
    memset(cv_px, 0, sizeof(cv_px));
    memset(pending_pixels, 0, sizeof(pending_pixels));
    blits = max_rows = pending_count = 0;
    intro.logo_ms = start;
    fm1_ms = start + TK_INTRO_LOGO_MS - 1u;
    assert(tk_intro_tick(&intro) == 1u);
    assert(!blits && !intro.phase);
    fm1_ms++;
    for (i = 0; i < 60u; i++) {
        previous = blits;
        assert(tk_intro_tick(&intro) == 1u);
        assert(blits == previous + 1u);
        fm1_ms++;
    }
    assert(intro.phase == 2u && intro.frame == 0u && blits == 60u);
    assert(!pending_count); /* first complete frame is synchronized before its clock starts */
    assert_frame(0u);
    assert_surrounding();
    fm1_ms = intro.animation_ms + 199u;
    assert(tk_intro_tick(&intro) == 1u && blits == 60u);
    for (i = 1u; i <= 3u; i++) {
        uint32_t row;
        fm1_ms = intro.animation_ms + i * TK_INTRO_FRAME_MS;
        for (row = 0; row < (TK_HOMUNCULUS_HEIGHT + 3u) / 4u; row++) {
            previous = blits;
            assert(tk_intro_tick(&intro) == 1u);
            assert(blits == previous + 1u);
            fm1_ms++;
        }
        lcd_sync();
        assert_frame(i == 2u ? 2u : 1u);
        assert_surrounding();
    }
    assert(max_rows == 4u);
    previous = blits;
    fm1_ms = intro.animation_ms + 799u;
    assert(tk_intro_tick(&intro) == 1u && blits == previous);
    fm1_ms++;
    assert(tk_intro_tick(&intro) == 1u && blits == previous + 1u);
    for (i = 1u; i < (TK_HOMUNCULUS_HEIGHT + 3u) / 4u; i++) {
        fm1_ms++;
        assert(tk_intro_tick(&intro) == 1u);
    }
    lcd_sync();
    assert_frame(0u);
    assert_surrounding();
    /* Waiting minutes, or wrapping the millisecond clock, never completes it. */
    fm1_ms = intro.animation_ms + 600000u;
    assert(tk_intro_tick(&intro) == 1u);
    fm1_ms = UINT32_MAX;
    assert(tk_intro_tick(&intro) == 1u);
    fm1_ms = 0u;
    assert(tk_intro_tick(&intro) == 1u);
}

int main(void)
{
    exercise(100u);
    exercise(UINT32_MAX - 600u);  /* logo wait crosses millisecond wrap */
    exercise(UINT32_MAX - 1600u); /* animation transitions cross wrap */
    puts("homunculus intro: crop/palette, DMA-buffer lifetime, title retention, 4-row budget, continuous A-B-C-B, no timed dismissal and timer wrap passed");
    return 0;
}
