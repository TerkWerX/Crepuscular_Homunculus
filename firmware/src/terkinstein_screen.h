/* SPDX-License-Identifier: GPL-3.0-only */
/* Main-owned capture of the actual 240 x 240 LCD writes. No ISR, DSP, flash,
 * allocation or computer-side reconstruction of page drawing is involved. */
#ifndef TERKINSTEIN_SCREEN_H
#define TERKINSTEIN_SCREEN_H
#include <stdint.h>
#include "terkinstein_protocol.h"
#define TK_SCREEN_WIDTH 240u
#define TK_SCREEN_TILE 16u
#define TK_SCREEN_COLUMNS 15u
#define TK_SCREEN_TILES 225u
#define TK_SCREEN_TILE_BYTES 512u
#define TK_SCREEN_CHUNK_BYTES 60u
#define TK_SCREEN_INTERVAL_MS 4u
#define TK_SCREEN_FAST_BURST 3u
#define TK_SCREEN_LEASE_MS 5000u
#define TK_SCREEN_COMPACT_BYTES 17u

typedef struct {
    uint16_t pixels[TK_SCREEN_WIDTH * TK_SCREEN_WIDTH];
    uint8_t raw[TK_SCREEN_TILE_BYTES], rle[TK_SCREEN_TILE_BYTES];
} tk_screen_pool_t;
static tk_screen_pool_t tk_screen_pool __attribute__((section(".pool")));
static struct {
    uint8_t dirty[TK_SCREEN_TILES];
    uint32_t generation, heartbeat, last_sent, cursor, tile, length, offset, burst_ms;
    uint16_t serial;
    uint8_t enabled, transmitting, encoding, sent, advanced, burst_count, compact, home;
} tk_screen;

static void tk_screen_capture_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t color)
{
    uint32_t row, col;
    if (x >= TK_SCREEN_WIDTH || y >= TK_SCREEN_WIDTH) return;
    if (w > TK_SCREEN_WIDTH - x) w = TK_SCREEN_WIDTH - x;
    if (h > TK_SCREEN_WIDTH - y) h = TK_SCREEN_WIDTH - y;
    for (row = y; row < y + h; row++) for (col = x; col < x + w; col++) {
        uint16_t *pixel = &tk_screen_pool.pixels[row * TK_SCREEN_WIDTH + col];
        if (*pixel != color) {
            *pixel = color;
            tk_screen.dirty[(row / TK_SCREEN_TILE) * TK_SCREEN_COLUMNS + col / TK_SCREEN_TILE] = 1;
        }
    }
}
/* lcd_blit receives byte-swapped RGB565 words: convert back to logical colors.
 * Original stride is retained while clipping. lcd_fill receives native RGB565. */
static void tk_screen_capture_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *pixels)
{
    uint32_t row, col, clipped_w = w;
    if (!pixels || x >= TK_SCREEN_WIDTH || y >= TK_SCREEN_WIDTH) return;
    if (clipped_w > TK_SCREEN_WIDTH - x) clipped_w = TK_SCREEN_WIDTH - x;
    if (h > TK_SCREEN_WIDTH - y) h = TK_SCREEN_WIDTH - y;
    for (row = 0; row < h; row++) for (col = 0; col < clipped_w; col++) {
        uint16_t c = pixels[row * w + col];
        uint16_t color = (uint16_t)(c >> 8 | c << 8);
        uint16_t *pixel = &tk_screen_pool.pixels[(y + row) * TK_SCREEN_WIDTH + x + col];
        if (*pixel != color) {
            *pixel = color;
            tk_screen.dirty[((y + row) / TK_SCREEN_TILE) * TK_SCREEN_COLUMNS + (x + col) / TK_SCREEN_TILE] = 1;
        }
    }
}
static void tk_screen_reset(uint32_t generation)
{
    tk_screen.generation = generation;
    tk_screen.enabled = tk_screen.transmitting = tk_screen.sent = tk_screen.advanced = tk_screen.burst_count = 0;
    tk_screen.compact = 0;
    tk_screen.offset = 0;
}
/* Only the PC stream uses this display context; the captured LCD remains exact.
 * Restore all tiles when entering/leaving HOME so stale coarse samples cannot
 * remain in a later full-resolution page. Serial and burst budgets survive. */
static void tk_screen_set_home(uint32_t home)
{
    uint32_t i;
    home = !!home;
    if (tk_screen.home == home) return;
    tk_screen.home = (uint8_t)home;
    if (!tk_screen.enabled || !tk_screen.compact) return;
    tk_screen.transmitting = tk_screen.sent = 0;
    tk_screen.offset = tk_screen.cursor = 0;
    for (i = 0; i < TK_SCREEN_TILES; i++) tk_screen.dirty[i] = 1;
}
static uint8_t tk_screen_subscribe_profile(uint8_t enabled, uint8_t compact,
                                         uint32_t now, uint32_t generation)
{
    uint32_t i, advanced = enabled >= 3u;
    if (generation != tk_screen.generation) tk_screen_reset(generation);
    if (!enabled) {
        tk_screen.enabled = tk_screen.transmitting = tk_screen.advanced = tk_screen.compact = 0;
        return 0;
    }
    if (enabled == 2u || enabled == 3u || !tk_screen.enabled ||
        tk_screen.advanced != advanced || tk_screen.compact != compact ||
        now - tk_screen.heartbeat >= TK_SCREEN_LEASE_MS) {
        tk_screen.transmitting = tk_screen.sent = 0;
        tk_screen.cursor = 0;
        for (i = 0; i < TK_SCREEN_TILES; i++) tk_screen.dirty[i] = 1;
    }
    tk_screen.enabled = 1;
    tk_screen.advanced = (uint8_t)advanced;
    tk_screen.compact = compact;
    tk_screen.heartbeat = now;
    return 1;
}
static uint8_t tk_screen_subscribe(uint8_t enabled, uint32_t now, uint32_t generation)
{
    return tk_screen_subscribe_profile(enabled, 0u, now, generation);
}

/* Encoding 3: version 1 followed by 64 fixed-palette two-bit indices, LSB first.
 * Each 2x2 source block retains its strongest classified color: green beats
 * bright purple, then middle purple, then black. RGB565 is expanded exactly as
 * the host LCD snapshots, preserving the offline quiet180 model's thresholds.
 * Palette: 0000,7936,D33F,A7EF. Only main's immutable raw tile is sampled. */
static void tk_screen_compact(void)
{
    uint32_t sample, dx, dy;
    tk_screen_pool.rle[0] = 1u;
    for (sample = 1u; sample < TK_SCREEN_COMPACT_BYTES; sample++) tk_screen_pool.rle[sample] = 0u;
    for (sample = 0; sample < 64u; sample++) {
        uint32_t best = 0u;
        for (dy = 0; dy < 2u; dy++) for (dx = 0; dx < 2u; dx++) {
            uint32_t pixel = ((sample / 8u) * 2u + dy) * 16u + (sample % 8u) * 2u + dx;
            uint32_t color = tk_screen_pool.raw[pixel * 2u] |
                (uint32_t)tk_screen_pool.raw[pixel * 2u + 1u] << 8;
            uint32_t r = (color >> 11) * 255u / 31u;
            uint32_t g = ((color >> 5) & 63u) * 255u / 63u;
            uint32_t b = (color & 31u) * 255u / 31u;
            uint32_t purple = r > b ? r : b, index = 0u;
            if (g > r && g > b && g >= 160u) index = 3u;
            else if (purple >= 180u && purple > g) index = purple >= 240u ? 2u : 1u;
            if (index > best) best = index;
        }
        tk_screen_pool.rle[1u + (sample >> 2)] |= (uint8_t)(best << ((sample & 3u) * 2u));
    }
    tk_screen.encoding = 3u; tk_screen.length = TK_SCREEN_COMPACT_BYTES;
}
/* Encoding 2 is lossless and opt-in: count1..16, RGB565LE palette, then
 * row-major indices packed LSB-first,1bit for1..2,2bits for3..4,4bits for5..16.
 * Existing raw/RLE remains preferable when a tile is uniform or has many runs. */
static void tk_screen_palette(void)
{
    uint16_t palette[16];
    uint32_t pixel, i, count = 0, bits, base, length;
    for (pixel = 0; pixel < 256u; pixel++) {
        uint16_t color = (uint16_t)(tk_screen_pool.raw[pixel * 2u] |
            (uint16_t)tk_screen_pool.raw[pixel * 2u + 1u] << 8);
        for (i = 0; i < count && palette[i] != color; i++) {}
        if (i == count) {
            if (count == 16u) return;
            palette[count++] = color;
        }
    }
    bits = count <= 2u ? 1u : count <= 4u ? 2u : 4u;
    base = 1u + count * 2u; length = base + 32u * bits;
    if (length >= tk_screen.length) return;
    tk_screen_pool.rle[0] = (uint8_t)count;
    for (i = 0; i < count; i++) {
        tk_screen_pool.rle[1u + i * 2u] = (uint8_t)palette[i];
        tk_screen_pool.rle[2u + i * 2u] = (uint8_t)(palette[i] >> 8);
    }
    for (i = base; i < length; i++) tk_screen_pool.rle[i] = 0;
    for (pixel = 0; pixel < 256u; pixel++) {
        uint16_t color = (uint16_t)(tk_screen_pool.raw[pixel * 2u] |
            (uint16_t)tk_screen_pool.raw[pixel * 2u + 1u] << 8);
        for (i = 0; palette[i] != color; i++) {}
        tk_screen_pool.rle[base + (pixel * bits >> 3)] |= (uint8_t)(i << (pixel * bits & 7u));
    }
    tk_screen.encoding = 2; tk_screen.length = length;
}
static void tk_screen_prepare(uint32_t tile)
{
    uint32_t row, col, at = 0, rle_n = 0, pos = 0;
    uint32_t x = tile % TK_SCREEN_COLUMNS * TK_SCREEN_TILE;
    uint32_t y = tile / TK_SCREEN_COLUMNS * TK_SCREEN_TILE;
    /* Immutable snapshot: later writes re-dirty this tile, rather than tearing
     * an in-progress transfer. Only main reads and writes these buffers. */
    tk_screen.dirty[tile] = 0;
    for (row = 0; row < TK_SCREEN_TILE; row++) for (col = 0; col < TK_SCREEN_TILE; col++) {
        uint16_t color = tk_screen_pool.pixels[(y + row) * TK_SCREEN_WIDTH + x + col];
        tk_screen_pool.raw[at++] = (uint8_t)color;
        tk_screen_pool.raw[at++] = (uint8_t)(color >> 8);
    }
    if (tk_screen.compact && tk_screen.home && x >= 16u && x <= 208u && y >= 80u && y <= 192u) {
        tk_screen_compact();
        tk_screen.tile = tile; tk_screen.offset = 0;
        tk_screen.serial = (uint16_t)((tk_screen.serial + 1u) & 16383u);
        tk_screen.transmitting = 1;
        return;
    }
    while (pos < TK_SCREEN_TILE_BYTES && rle_n + 3u < TK_SCREEN_TILE_BYTES) {
        uint32_t count = 1;
        uint8_t lo = tk_screen_pool.raw[pos], hi = tk_screen_pool.raw[pos + 1u];
        while (count < 255u && pos + count * 2u < TK_SCREEN_TILE_BYTES &&
            tk_screen_pool.raw[pos + count * 2u] == lo && tk_screen_pool.raw[pos + count * 2u + 1u] == hi) count++;
        tk_screen_pool.rle[rle_n++] = (uint8_t)count;
        tk_screen_pool.rle[rle_n++] = lo;
        tk_screen_pool.rle[rle_n++] = hi;
        pos += count * 2u;
    }
    tk_screen.encoding = pos == TK_SCREEN_TILE_BYTES && rle_n < TK_SCREEN_TILE_BYTES ? 1u : 0u;
    tk_screen.length = tk_screen.encoding ? rle_n : TK_SCREEN_TILE_BYTES;
    if (tk_screen.advanced) tk_screen_palette();
    tk_screen.tile = tile; tk_screen.offset = 0;
    tk_screen.serial = (uint16_t)((tk_screen.serial + 1u) & 16383u);
    tk_screen.transmitting = 1;
}
/* Each independent chunk uses [high-bit mask, up to seven low seven bits]. */
static uint32_t tk_screen_pack7(uint8_t *out, const uint8_t *data, uint32_t n)
{
    uint32_t at = 0, count = 0;
    while (at < n) {
        uint32_t i, amount = n - at < 7u ? n - at : 7u, mask_at = count++;
        out[mask_at] = 0;
        for (i = 0; i < amount; i++) {
            out[mask_at] |= (uint8_t)((data[at] >> 7) << i);
            out[count++] = data[at++] & 127u;
        }
    }
    return count;
}
/* One complete, all-or-nothing USB frame per call. Legacy emits at most one
 * every four ms; opted-in advanced clients allow at most three successful
 * admissions per ms. The supplied stream sender admits only with both SysEx
 * and ordinary MIDI output queues empty, so notes/replies cannot be starved.
 * Caller services input/transport and ordinary command replies first. A busy
 * sender preserves both the snapshot and offset for an exact retry. */
static int tk_screen_pump(uint32_t now, uint32_t generation, int allowed,
                          int (*send)(const uint8_t *, uint32_t))
{
    uint8_t payload[TK_REMOTE_PAYLOAD_MAX], wire[TK_REMOTE_WIRE_MAX];
    const uint8_t *data;
    uint32_t i, amount, n;
    if (generation != tk_screen.generation) tk_screen_reset(generation);
    if (!tk_screen.enabled) return 0;
    if (now - tk_screen.heartbeat >= TK_SCREEN_LEASE_MS) {
        tk_screen.enabled = tk_screen.transmitting = 0;
        return 0;
    }
    if (!allowed || !send) return 0;
    if (tk_screen.advanced) {
        if (tk_screen.burst_ms == now && tk_screen.burst_count >= TK_SCREEN_FAST_BURST) return 0;
    } else if (tk_screen.sent && now - tk_screen.last_sent < TK_SCREEN_INTERVAL_MS) return 0;
    if (!tk_screen.transmitting) {
        for (i = 0; i < TK_SCREEN_TILES; i++) {
            uint32_t tile = (tk_screen.cursor + i) % TK_SCREEN_TILES;
            if (tk_screen.dirty[tile]) { tk_screen_prepare(tile); break; }
        }
        if (!tk_screen.transmitting) return 0;
    }
    data = tk_screen.encoding ? tk_screen_pool.rle : tk_screen_pool.raw;
    amount = tk_screen.length - tk_screen.offset;
    if (amount > TK_SCREEN_CHUNK_BYTES) amount = TK_SCREEN_CHUNK_BYTES;
    payload[0] = TK_REMOTE_OK; payload[1] = 1;
    payload[2] = (uint8_t)(tk_screen.tile % TK_SCREEN_COLUMNS);
    payload[3] = (uint8_t)(tk_screen.tile / TK_SCREEN_COLUMNS);
    payload[4] = (uint8_t)(tk_screen.serial & 127u); payload[5] = (uint8_t)(tk_screen.serial >> 7);
    payload[6] = tk_screen.encoding;
    payload[7] = (uint8_t)(tk_screen.length & 127u); payload[8] = (uint8_t)(tk_screen.length >> 7);
    payload[9] = (uint8_t)(tk_screen.offset & 127u); payload[10] = (uint8_t)(tk_screen.offset >> 7);
    n = 11u + tk_screen_pack7(payload + 11, data + tk_screen.offset, amount);
    n = tk_remote_encode(wire, sizeof wire, TK_REMOTE_REPLY, TK_REMOTE_SCREEN_TILE,
        (uint8_t)(tk_screen.serial & 127u), payload, n);
    if (!n || !send(wire, n)) return 0;
    if (tk_screen.burst_ms != now) { tk_screen.burst_ms = now; tk_screen.burst_count = 0; }
    tk_screen.burst_count++;
    tk_screen.last_sent = now; tk_screen.sent = 1; tk_screen.offset += amount;
    if (tk_screen.offset == tk_screen.length) {
        tk_screen.transmitting = 0;
        tk_screen.cursor = (tk_screen.tile + 1u) % TK_SCREEN_TILES;
    }
    return 1;
}
#endif
