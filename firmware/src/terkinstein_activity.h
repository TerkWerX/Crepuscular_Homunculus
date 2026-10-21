/* SPDX-License-Identifier: GPL-3.0-only */
/* Read-only panel feedback, owned by the main loop. No DSP or hardware access. */
#ifndef TERKINSTEIN_ACTIVITY_H
#define TERKINSTEIN_ACTIVITY_H
#include <stdint.h>

#define TK_PANEL_ACTIVITY_VERSION 3u
#define TK_PANEL_ACTIVITY_BYTES 12u
#define TK_PANEL_ACTIVITY_PULSE_MS 160u
#define TK_PANEL_BUTTONS 14u
#define TK_PANEL_ENCODERS 8u /* seven encoders plus the analog MASTER gesture */
#define TK_PANEL_NOTES_MASK 0x07FFFFFFu

typedef struct {
    uint32_t buttons, notes;
    uint32_t button_ms[TK_PANEL_BUTTONS], encoder_ms[TK_PANEL_ENCODERS];
    uint16_t button_pulses, leds;
    uint8_t encoder_pulses;
} tk_panel_activity_t;

static tk_panel_activity_t tk_panel_activity;

static void tk_panel_activity_init(tk_panel_activity_t *a)
{
    uint32_t i;
    a->buttons = a->notes = a->button_pulses = a->encoder_pulses = a->leds = 0u;
    for (i = 0; i < TK_PANEL_BUTTONS; i++) a->button_ms[i] = 0u;
    for (i = 0; i < TK_PANEL_ENCODERS; i++) a->encoder_ms[i] = 0u;
}

/* Sample held inputs before serving remote requests. Expiry runs each main
 * pass, so old pulses cannot reappear when the millisecond counter wraps. */
static void tk_panel_activity_inputs(tk_panel_activity_t *a, uint32_t buttons,
                                     uint32_t notes, uint32_t now)
{
    uint32_t i;
    a->buttons = buttons & 16383u;
    a->notes = notes & TK_PANEL_NOTES_MASK;
    for (i = 0; i < TK_PANEL_BUTTONS; i++)
        if ((a->button_pulses & (1u << i)) && now - a->button_ms[i] > TK_PANEL_ACTIVITY_PULSE_MS)
            a->button_pulses &= (uint16_t)~(1u << i);
    for (i = 0; i < TK_PANEL_ENCODERS; i++)
        if ((a->encoder_pulses & (1u << i)) && now - a->encoder_ms[i] > TK_PANEL_ACTIVITY_PULSE_MS)
            a->encoder_pulses &= (uint8_t)~(1u << i);
}

/* Called with physical press edges before remote/MIDI buttons are merged. */
static void tk_panel_activity_press(tk_panel_activity_t *a, uint32_t pressed, uint32_t now)
{
    uint32_t i;
    pressed &= 16383u;
    for (i = 0; i < TK_PANEL_BUTTONS; i++)
        if (pressed & (1u << i)) a->button_ms[i] = now;
    a->button_pulses |= (uint16_t)pressed;
}

/* Record raw detents before aliases are consumed or acceleration is applied. */
static void tk_panel_activity_encoder(tk_panel_activity_t *a, uint32_t encoder,
                                      int32_t delta, uint32_t now)
{
    if (delta && encoder < TK_PANEL_ENCODERS) {
        a->encoder_ms[encoder] = now;
        a->encoder_pulses |= (uint8_t)(1u << encoder);
    }
}

/* Twelve MIDI-safe bytes, excluding protocol status. The original nine-byte
 * prefix retains its indices; bytes 9..10 report exact held buttons separately
 * from press pulses; byte 11 carries the analog MASTER pulse. Queries never
 * consume an event. The octave byte is signed
 * panel octave -2..+2 biased by two; physical key i therefore sounds at
 * 29 + 12 * out[8] + i when newly pressed. */
static uint32_t tk_panel_activity_pack(const tk_panel_activity_t *a, int32_t octave,
                                     uint32_t now, uint8_t *out)
{
    uint32_t i, buttons = a->buttons, encoders = 0u;
    for (i = 0; i < TK_PANEL_BUTTONS; i++)
        if ((a->button_pulses & (1u << i)) && now - a->button_ms[i] <= TK_PANEL_ACTIVITY_PULSE_MS)
            buttons |= 1u << i;
    for (i = 0; i < TK_PANEL_ENCODERS; i++)
        if ((a->encoder_pulses & (1u << i)) && now - a->encoder_ms[i] <= TK_PANEL_ACTIVITY_PULSE_MS)
            encoders |= 1u << i;
    out[0] = TK_PANEL_ACTIVITY_VERSION;
    out[1] = (uint8_t)(buttons & 127u); out[2] = (uint8_t)((buttons >> 7) & 127u);
    for (i = 0; i < 4u; i++) out[3u + i] = (uint8_t)((a->notes >> (7u * i)) & 127u);
    out[7] = (uint8_t)(encoders & 127u);
    out[8] = octave < -2 ? 0u : octave > 2 ? 4u : (uint8_t)(octave + 2);
    out[9] = (uint8_t)(a->buttons & 127u);
    out[10] = (uint8_t)((a->buttons >> 7) & 127u);
    out[11] = (uint8_t)(encoders >> 7);
    return TK_PANEL_ACTIVITY_BYTES;
}
/* Explicitly requested v4 adds the exact main-owned physical button lights.
 * Empty legacy queries retain their v3 body. No event is consumed. */
static uint32_t tk_panel_activity_pack_lights(const tk_panel_activity_t *a, int32_t octave,
                                            uint32_t now, uint8_t *out)
{
    tk_panel_activity_pack(a, octave, now, out);
    out[0] = 4u;
    out[12] = (uint8_t)(a->leds & 127u);
    out[13] = (uint8_t)((a->leds >> 7) & 127u);
    return 14u;
}
#endif
