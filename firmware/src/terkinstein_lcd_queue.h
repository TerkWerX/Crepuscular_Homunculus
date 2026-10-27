/* SPDX-License-Identifier: GPL-3.0-only */
/* Main-owned physical LCD queue. Reuses the existing exact mirror framebuffer;
 * no extra framebuffer, audio work, allocation, USB or persistent state. */
#ifndef TERKINSTEIN_LCD_QUEUE_H
#define TERKINSTEIN_LCD_QUEUE_H
#define TK_LCD_BATCH_TILES 2u
#define TK_LCD_BATCH_PIXELS (TK_LCD_BATCH_TILES * TK_SCREEN_TILE * TK_SCREEN_TILE)
static struct {
    uint8_t dirty[TK_SCREEN_TILES], active;
    uint16_t pixels[TK_SCREEN_TILE * TK_SCREEN_TILE]; /* immutable DMA staging */
    uint32_t cursor;
} tk_lcd_queue;

static void tk_lcd_queue_reset(void)
{
    uint32_t i;
    tk_lcd_queue.active = 0u;
    tk_lcd_queue.cursor = 0u;
    for (i = 0; i < TK_SCREEN_TILES; ++i) tk_lcd_queue.dirty[i] = 0u;
}
/* Call only after the previous DMA has completed. Drawing and uploading both
 * belong to main. Later paints coalesce to the newest desired tile; the cursor
 * advances fairly even when another region is continually animated. */
static uint32_t tk_lcd_queue_take(const uint16_t *frame)
{
    uint32_t i, tile, row, col, x, y;
    if (!tk_lcd_queue.active) return TK_SCREEN_TILES;
    for (i = 0; i < TK_SCREEN_TILES; ++i) {
        tile = (tk_lcd_queue.cursor + i) % TK_SCREEN_TILES;
        if (!tk_lcd_queue.dirty[tile]) continue;
        x = tile % TK_SCREEN_COLUMNS * TK_SCREEN_TILE;
        y = tile / TK_SCREEN_COLUMNS * TK_SCREEN_TILE;
        for (row = 0; row < TK_SCREEN_TILE; ++row)
            for (col = 0; col < TK_SCREEN_TILE; ++col) {
                uint16_t c = frame[(y + row) * TK_SCREEN_WIDTH + x + col];
                tk_lcd_queue.pixels[row * TK_SCREEN_TILE + col] = (uint16_t)(c >> 8 | c << 8);
            }
        tk_lcd_queue.dirty[tile] = 0u;
        tk_lcd_queue.cursor = (tile + 1u) % TK_SCREEN_TILES;
        return tile;
    }
    return TK_SCREEN_TILES;
}
#endif
