/* SPDX-License-Identifier: GPL-3.0-only */
/* Portable encoder timing/overflow checks. These do not qualify physical feel. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "terkinstein_encoder.h"

static uint32_t checks;

static void expect(int good, const char *why)
{
    checks++;
    if (!good) { fprintf(stderr, "FAIL encoder %u: %s\n", checks, why); exit(1); }
}

static tk_encoder_response_t primed(uint32_t context, uint32_t time, int direction)
{
    tk_encoder_response_t state;
    memset(&state, 0, sizeof state);
    state.context = context; state.last_ms = time;
    state.direction = (int8_t)direction; state.valid = 1u;
    return state;
}

static void linear_checks(void)
{
    static const int32_t delta[] = {INT32_MIN, -32768, -32767, -127, -1,
                                  0, 1, 127, 32767, 32768, INT32_MAX};
    uint32_t i;
    tk_encoder_response_t state = primed(7u, 999u, 1);
    for (i = 0; i < sizeof delta / sizeof delta[0]; i++) {
        int32_t wanted = delta[i] < -32767 ? -32767 : delta[i] > 32767 ? 32767 : delta[i];
        expect(tk_encoder_response(&state, 7u, TK_ENCODER_LINEAR, delta[i], 1000u) == wanted,
               "LINEAR preserves ordinary detents and bounds extreme signed input");
    }
}

static void boundary_checks(void)
{
    static const uint32_t elapsed[] = {0u, 1u, 24u, 25u, 26u, 59u, 60u, 61u, 179u, 180u, 181u};
    static const int32_t gain[] = {4, 4, 4, 4, 2, 2, 2, 1, 1, 1, 1};
    uint32_t i;
    for (i = 0; i < sizeof elapsed / sizeof elapsed[0]; i++) {
        tk_encoder_response_t forward = primed(11u, 1000u, 1);
        tk_encoder_response_t backward = primed(11u, 1000u, -1);
        expect(tk_encoder_response(&forward, 11u, TK_ENCODER_ADAPTIVE, 1, 1000u + elapsed[i]) == gain[i],
               "forward acceleration thresholds are inclusive at 25/60 milliseconds");
        expect(tk_encoder_response(&backward, 11u, TK_ENCODER_ADAPTIVE, -1, 1000u + elapsed[i]) == -gain[i],
               "backward acceleration has identical magnitude thresholds");
        expect(forward.context == 11u && forward.last_ms == 1000u + elapsed[i] &&
               forward.direction == 1 && forward.valid,
               "processed adaptive movement records its real event time/context/direction");
    }
}

static void reset_checks(void)
{
    tk_encoder_response_t state;
    memset(&state, 0, sizeof state);
    expect(tk_encoder_response(&state, 2u, TK_ENCODER_ADAPTIVE, 4, 1000u) == 4,
           "first batched movement starts at fine speed");
    expect(tk_encoder_response(&state, 2u, TK_ENCODER_ADAPTIVE, 4, 1100u) == 16,
           "subsequent batched movement uses elapsed time per detent");
    expect(tk_encoder_response(&state, 3u, TK_ENCODER_ADAPTIVE, 4, 1105u) == 4,
           "a different page/preset/control context starts at fine speed");
    expect(tk_encoder_response(&state, 3u, TK_ENCODER_ADAPTIVE, 4, 1110u) == 16,
           "acceleration can resume after context change");
    expect(tk_encoder_response(&state, 3u, TK_ENCODER_ADAPTIVE, -4, 1115u) == -4,
           "direction reversal starts fine even for a batch");
    expect(tk_encoder_response(&state, 3u, TK_ENCODER_ADAPTIVE, -4, 1120u) == -16,
           "the second reverse movement can accelerate normally");
    expect(tk_encoder_response(&state, 3u, TK_ENCODER_ADAPTIVE, -4, 1300u) == -8,
           "180-millisecond batch pause still permits per-detent medium gain");
    expect(tk_encoder_response(&state, 3u, TK_ENCODER_ADAPTIVE, -4, 1481u) == -4,
           "pause over 180 milliseconds resets fine regardless of batch size");
    expect(tk_encoder_response(&state, 3u, TK_ENCODER_ADAPTIVE, -1, 1490u) == -4,
           "acceleration resumes only after the first post-pause movement");
}

static void quiet_and_independent_checks(void)
{
    tk_encoder_response_t slots[4], copy;
    uint32_t i;
    memset(slots, 0, sizeof slots);
    for (i = 0; i < 4u; i++) {
        expect(tk_encoder_response(&slots[i], 99u, TK_ENCODER_ADAPTIVE, 1, 1000u + i) == 1,
               "first movement is fine independently on each physical knob");
        copy = slots[i];
        expect(!tk_encoder_response(&slots[i], 100u, TK_ENCODER_ADAPTIVE, 0, 2000u),
               "idle polling produces no detents");
        expect(!memcmp(&copy, &slots[i], sizeof copy),
               "idle polling cannot overwrite event timing, direction or context");
    }
    expect(tk_encoder_response(&slots[0], 99u, TK_ENCODER_ADAPTIVE, 1, 1010u) == 4,
           "turning knob one rapidly can accelerate that knob");
    expect(tk_encoder_response(&slots[1], 99u, TK_ENCODER_ADAPTIVE, 1, 1500u) == 1,
           "rapid knob one motion cannot accelerate a later knob two event");
    expect(slots[2].last_ms == 1002u && slots[3].last_ms == 1003u,
           "untouched physical knobs retain independent state");
}

static void wrap_checks(void)
{
    tk_encoder_response_t state = primed(7u, UINT32_MAX - 10u, 1);
    expect(tk_encoder_response(&state, 7u, TK_ENCODER_ADAPTIVE, 1, 9u) == 4,
           "unsigned clock wrap retains the actual twenty-millisecond interval");
    state = primed(7u, UINT32_MAX - 100u, 1);
    expect(tk_encoder_response(&state, 7u, TK_ENCODER_ADAPTIVE, 1, 80u) == 1,
           "wrapped pause over 180 milliseconds resets fine");
    state = primed(7u, 500u, 1);
    expect(tk_encoder_response(&state, 7u, TK_ENCODER_ADAPTIVE, 1, 499u) == 1,
           "apparent backwards time is a long unsigned pause, not maximum acceleration");
}

static void extremes_and_properties(void)
{
    uint32_t random = 0x6D37A041u, i;
    static const int32_t extremes[] = {INT32_MIN, INT32_MAX, -32767, 32767, -16384,
                                      16384, -8192, 8192};
    for (i = 0; i < sizeof extremes / sizeof extremes[0]; i++) {
        tk_encoder_response_t state = primed(23u, 400u, extremes[i] < 0 ? -1 : 1);
        expect(tk_encoder_response(&state, 23u, TK_ENCODER_ADAPTIVE, extremes[i], 401u) ==
               (extremes[i] < 0 ? -32767 : 32767),
               "fast large movements saturate safely before multiplication can overflow");
    }
    for (i = 0; i < 4096u; i++) {
        uint32_t elapsed, magnitude, factor, wanted;
        int32_t positive, negative;
        tk_encoder_response_t forward, backward;
        random = random * 1664525u + 1013904223u;
        magnitude = (random & 32767u) + 1u;
        random = random * 1664525u + 1013904223u;
        elapsed = random % 300u;
        factor = elapsed > 180u ? 1u : elapsed / magnitude <= 25u ? 4u :
                 elapsed / magnitude <= 60u ? 2u : 1u;
        wanted = magnitude * factor;
        if (wanted > 32767u) wanted = 32767u;
        forward = primed(23u, 400u, 1); backward = primed(23u, 400u, -1);
        positive = tk_encoder_response(&forward, 23u, TK_ENCODER_ADAPTIVE,
                                       (int32_t)magnitude, 400u + elapsed);
        negative = tk_encoder_response(&backward, 23u, TK_ENCODER_ADAPTIVE,
                                       -(int32_t)magnitude, 400u + elapsed);
        expect(positive == (int32_t)wanted && negative == -(int32_t)wanted,
               "random batches preserve signed symmetry, bounded gain and pause reset");
        expect(positive >= 1 && positive <= 32767 && negative >= -32767 && negative <= -1,
               "adaptive output never leaves its bounded signed detent range");
    }
}

int main(void)
{
    linear_checks(); boundary_checks(); reset_checks();
    quiet_and_independent_checks(); wrap_checks(); extremes_and_properties();
    printf("encoder response: %u checks passed\n", checks);
    return 0;
}
