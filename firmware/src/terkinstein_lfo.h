/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TERKINSTEIN_LFO_H
#define TERKINSTEIN_LFO_H

#include <stdint.h>

/* A main-loop modulation helper for the SHADE macro. Audio still receives
 * only the app's atomically published parameter word. This state is never
 * touched from the audio callback. */
enum {
    TK_LFO_TRIANGLE = 0,
    TK_LFO_PULSE = 1,
    TK_LFO_RAMP = 2
};

typedef struct {
    uint32_t last_ms;
    uint16_t phase_ms;
    uint16_t period_ms;
} tk_lfo_t;

static uint16_t tk_lfo_period_ms(uint8_t rate)
{
    if (rate > 127u) rate = 127u;
    /* Rate endpoints are one cycle per 8 s and four cycles per second. */
    return (uint16_t)(250u + (127u - (uint32_t)rate) * 7750u / 127u);
}

static void tk_lfo_init(tk_lfo_t *state, uint32_t now_ms)
{
    if (!state) return;
    state->last_ms = now_ms;
    state->phase_ms = 0;
    state->period_ms = tk_lfo_period_ms(0);
}

/* Evaluate at a supplied millisecond clock. Subtraction handles a normal
 * uint32_t timer wrap. A rate change preserves the fraction of the current
 * cycle, instead of restarting the modulation. Call about every 10-20 ms;
 * the audio engine smooths the published macro between these updates. */
static uint8_t tk_lfo_eval(tk_lfo_t *state, uint32_t now_ms, uint8_t rate,
                           uint8_t depth, uint8_t shape, uint8_t base_shade)
{
    uint32_t old_period, phase, elapsed, new_period, section, fraction;
    int32_t wave, amplitude, value;
    if (base_shade > 127u) base_shade = 127u;
    if (!state) return base_shade;

    old_period = state->period_ms ? state->period_ms : tk_lfo_period_ms(0);
    elapsed = now_ms - state->last_ms;
    state->last_ms = now_ms;
    phase = (state->phase_ms + elapsed % old_period) % old_period;
    new_period = tk_lfo_period_ms(rate);
    if (new_period != old_period) {
        phase = (phase * new_period + old_period / 2u) / old_period;
        if (phase >= new_period) phase -= new_period;
    }
    state->phase_ms = (uint16_t)phase;
    state->period_ms = (uint16_t)new_period;
    if (!depth) return base_shade; /* exact bypass at every phase */
    if (depth > 127u) depth = 127u;

    if (shape == TK_LFO_PULSE) {
        wave = phase < new_period / 2u ? 127 : -127;
    } else if (shape == TK_LFO_RAMP) {
        /* Center the saw at start: it climbs, then drops halfway through. */
        uint32_t shifted = phase + new_period / 2u;
        if (shifted >= new_period) shifted -= new_period;
        wave = (int32_t)(shifted * 254u / new_period) - 127;
    } else {
        /* Default and shape 0: zero -> bright -> zero -> dark -> zero. */
        section = phase * 4u / new_period;
        fraction = phase * 4u % new_period;
        wave = (int32_t)((fraction * 127u + new_period / 2u) / new_period);
        if (section == 1u) wave = 127 - wave;
        else if (section == 2u) wave = -wave;
        else if (section == 3u) wave -= 127;
    }
    amplitude = (int32_t)(((uint32_t)depth * 64u + 63u) / 127u);
    value = (int32_t)base_shade + wave * amplitude / 127;
    if (value < 0) value = 0;
    if (value > 127) value = 127;
    return (uint8_t)value;
}

#endif
