/* SPDX-License-Identifier: GPL-3.0-only */
/* Main-owned response for the FM-1's detented sound controls. No DSP or MIDI
 * value mapping: slow turns retain every stored unit, fast turns cover more.
 * Input is the bounded, potentially batched detents since the last poll. */
#ifndef TERKINSTEIN_ENCODER_H
#define TERKINSTEIN_ENCODER_H
enum { TK_ENCODER_LINEAR, TK_ENCODER_ADAPTIVE };
typedef struct {
    uint32_t context, last_ms;
    int8_t direction;
    uint8_t valid;
} tk_encoder_response_t;

static int32_t tk_encoder_response(tk_encoder_response_t *state,
    uint32_t context, uint32_t mode, int32_t delta, uint32_t now)
{
    uint32_t elapsed, magnitude, gain = 1u;
    int32_t direction;
    if (!delta) return 0;
    /* Clamp before negation/multiplication, including hostile INT32_MIN. */
    if (delta > 32767) delta = 32767;
    if (delta < -32767) delta = -32767;
    direction = delta < 0 ? -1 : 1;
    magnitude = (uint32_t)(delta < 0 ? -delta : delta);
    elapsed = now - state->last_ms;
    if (mode == TK_ENCODER_ADAPTIVE && state->valid
        && state->context == context && state->direction == direction
        && elapsed <= 180u) {
        /* Time per batched detent keeps LCD coalescing from being mistaken for
         * a slow turn. A long stalled batch deliberately receives no boost. */
        uint32_t interval = elapsed / magnitude;
        gain = interval <= 25u ? 4u : interval <= 60u ? 2u : 1u;
    }
    state->context = context; state->last_ms = now;
    state->direction = (int8_t)direction;
    state->valid = mode == TK_ENCODER_ADAPTIVE;
    magnitude *= gain;
    if (magnitude > 32767u) magnitude = 32767u;
    return direction < 0 ? -(int32_t)magnitude : (int32_t)magnitude;
}
#endif
