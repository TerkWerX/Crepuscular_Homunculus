/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../firmware/src/terkinstein_lfo.h"

int main(void)
{
    tk_lfo_t lfo, stepped, wrapped, plain;
    uint32_t t;

    assert(tk_lfo_period_ms(0) == 8000u);
    assert(tk_lfo_period_ms(127) == 250u);
    for (t = 1; t <= 127; ++t)
        assert(tk_lfo_period_ms((uint8_t)t) < tk_lfo_period_ms((uint8_t)(t - 1u)));

    tk_lfo_init(&lfo, 0);
    assert(tk_lfo_eval(&lfo, 0, 0, 127, TK_LFO_TRIANGLE, 64) == 64);
    assert(tk_lfo_eval(&lfo, 2000, 0, 127, TK_LFO_TRIANGLE, 64) == 127);
    assert(tk_lfo_eval(&lfo, 4000, 0, 127, TK_LFO_TRIANGLE, 64) == 64);
    assert(tk_lfo_eval(&lfo, 6000, 0, 127, TK_LFO_TRIANGLE, 64) == 0);
    assert(tk_lfo_eval(&lfo, 8000, 0, 127, TK_LFO_TRIANGLE, 64) == 64);
    assert(tk_lfo_eval(&lfo, 9000, 0, 0, TK_LFO_TRIANGLE, 77) == 77);

    tk_lfo_init(&lfo, 0);
    assert(tk_lfo_eval(&lfo, 0, 0, 127, TK_LFO_PULSE, 64) == 127);
    assert(tk_lfo_eval(&lfo, 4000, 0, 127, TK_LFO_PULSE, 64) == 0);
    tk_lfo_init(&lfo, 0);
    assert(tk_lfo_eval(&lfo, 0, 0, 127, TK_LFO_RAMP, 64) == 64);
    assert(tk_lfo_eval(&lfo, 4000, 0, 127, TK_LFO_RAMP, 64) == 0);

    /* Rate changes retain fractional phase and do not restart the cycle. */
    tk_lfo_init(&lfo, 0);
    (void)tk_lfo_eval(&lfo, 2000, 0, 127, TK_LFO_TRIANGLE, 64);
    (void)tk_lfo_eval(&lfo, 2000, 127, 127, TK_LFO_TRIANGLE, 64);
    assert(lfo.period_ms == 250u);
    assert(lfo.phase_ms == 63u);

    /* Cadence-independent elapsed time and uint32 timer rollover. */
    tk_lfo_init(&lfo, 0);
    tk_lfo_init(&stepped, 0);
    (void)tk_lfo_eval(&lfo, 0, 64, 100, TK_LFO_TRIANGLE, 64);
    (void)tk_lfo_eval(&lfo, 5750, 64, 100, TK_LFO_TRIANGLE, 64);
    (void)tk_lfo_eval(&stepped, 0, 64, 100, TK_LFO_TRIANGLE, 64);
    for (t = 250; t <= 5750; t += 250)
        (void)tk_lfo_eval(&stepped, t, 64, 100, TK_LFO_TRIANGLE, 64);
    assert(lfo.phase_ms == stepped.phase_ms);

    tk_lfo_init(&wrapped, UINT32_MAX - 99u);
    tk_lfo_init(&plain, 100u);
    (void)tk_lfo_eval(&wrapped, UINT32_MAX - 99u, 127, 127, TK_LFO_TRIANGLE, 64);
    (void)tk_lfo_eval(&plain, 100u, 127, 127, TK_LFO_TRIANGLE, 64);
    assert(tk_lfo_eval(&wrapped, 50u, 127, 127, TK_LFO_TRIANGLE, 64)
        == tk_lfo_eval(&plain, 250u, 127, 127, TK_LFO_TRIANGLE, 64));
    assert(wrapped.phase_ms == plain.phase_ms);

    /* Depth zero and clipped extremes remain within the macro's 0..127 range. */
    tk_lfo_init(&lfo, 0);
    for (t = 0; t <= 8000; t += 31) {
        assert(tk_lfo_eval(&lfo, t, 0, 0, TK_LFO_TRIANGLE, 100) == 100);
        assert(tk_lfo_eval(&lfo, t, 0, 127, TK_LFO_TRIANGLE, 127) <= 127u);
    }
    assert(tk_lfo_eval((tk_lfo_t *)0, 0, 0, 127, TK_LFO_TRIANGLE, 200) == 127);
    puts("LFO checks passed");
    return 0;
}
