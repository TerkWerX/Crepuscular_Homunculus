/* SPDX-License-Identifier: GPL-3.0-only */
/* Main-owned product labels. Creator artwork remains the startup bitmap. */
#ifndef TERKINSTEIN_PRODUCT_LABEL_H
#define TERKINSTEIN_PRODUCT_LABEL_H
static void tk_product_name(uint32_t y, uint16_t color)
{
    draw_text_box(8, y, 224, &FONT_S, "CREPUSCULAR", color, 1);
    draw_text_box(8, y + 20u, 224, &FONT_S, "HOMUNCULUS", color, 1);
}
static void tk_product_recovery_screen(uint32_t flash_known)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    tk_product_name(28u, C_WHITE);
    draw_text_box(0, 80, 240, &FONT_S, "USB RESCUE", C_WHITE, 1);
    draw_text_box(0, 110, 240, &FONT_S, "CONNECT USB", C_WHITE, 1);
    draw_text_box(0, 138, 240, &FONT_S,
        flash_known ? "OPEN THE INSTALLER" : "UNKNOWN FLASH", C_WHITE, 1);
    draw_text_box(0, 178, 240, &FONT_S, "AUDIO OFF", C_WHITE, 1);
}
static void tk_product_fault_screen(uint32_t vector, uint32_t pc, uint32_t returns)
{
    uint32_t i, values[3] = {vector, pc, returns};
    char text[9];
    lcd_fill(0, 0, 240, 240, RGB(160, 0, 0));
    tk_product_name(8u, C_WHITE);
    draw_text_box(0, 52, 240, &FONT_S, "CRASH", C_WHITE, 1);
    for (i = 0; i < 3u; i++) {
        uint32_t k;
        for (k = 0; k < 8u; k++)
            text[k] = "0123456789ABCDEF"[(values[i] >> (28u - 4u * k)) & 15u];
        text[8] = 0;
        draw_text_box(10, 88u + i * 24u, 220, &FONT_S, text, C_WHITE, 0);
    }
}
#endif
