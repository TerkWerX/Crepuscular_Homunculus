/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TERKINSTEIN_METRONOME_H
#define TERKINSTEIN_METRONOME_H

#include <stdint.h>

/* One independent, finite click; it never consumes a synth voice. Main owns
 * the clock and publishes one atomic word. Audio alone owns oscillator and
 * envelope state. Call init only at startup, before enabling app_audio. */
#define TK_METRONOME_DEFAULT_LEVEL 32u
#define TK_METRONOME_DEFAULT_BEATS 4u
#define TK_METRONOME_DEFAULT_UNIT 4u
#define TK_METRONOME_FRAMES 882u       /* 20 ms at the FM-1's 44,100 Hz */
#define TK_METRONOME_ATTACK_FRAMES 32u /* avoids an abrupt first sample */
#define TK_METRONOME_MAX_AMPLITUDE 8192u
#define TK_METRONOME_GENERATION_MASK 0x00FFFFFFu

/* level[6:0], accent[7], generation[31:8]. A zero-level publication cancels.
 * Comparing the entire word is safe across ordinary generation wrap. Like
 * every finite tag, it cannot distinguish 2^24 identical events missed by
 * a consumer; the main loop must not publish such a backlog to audio. */
static volatile uint32_t tk_metronome_command;
/* Audio publishes these atomic observations for main's silence-before-SAVE
 * guard. Main must not inspect audio-owned phase, position or amplitude. */
static volatile uint32_t tk_metronome_observed_command, tk_metronome_active;
static struct {
    uint32_t seen, phase, increment, position, amplitude;
} tk_metronome_audio;

static void tk_metronome_init(void)
{
    tk_metronome_command = 0;
    tk_metronome_observed_command = tk_metronome_active = 0;
    tk_metronome_audio.seen = tk_metronome_audio.phase = 0;
    tk_metronome_audio.increment = tk_metronome_audio.amplitude = 0;
    tk_metronome_audio.position = TK_METRONOME_FRAMES;
}

/* Main only. Latest publication wins: late/caught-up transport events cannot
 * create a queue of stale clicks. Accent changes pitch, not the level limit. */
static void tk_metronome_trigger(uint32_t accent, uint32_t level)
{
    uint32_t generation = ((tk_metronome_command >> 8) + 1u)
        & TK_METRONOME_GENERATION_MASK;
    if (level > 127u) level = 127u;
    tk_metronome_command = (generation << 8) | level
        | (level && accent ? 128u : 0u);
}

static void tk_metronome_cancel(void)
{
    tk_metronome_trigger(0u, 0u);
}

/* Main only. A pending click is not quiet; a pending cancellation of an
 * active click must wait for audio to consume it. An unplayed click replaced
 * by cancel can be quiet immediately. Main is the sole producer and audio
 * preempts the main loop, so these scalar reads can only conservatively wait
 * for the next completed audio callback, never read partial DSP state. */
static int tk_metronome_quiet(void)
{
    uint32_t command = tk_metronome_command;
    if (tk_metronome_active) return 0;
    return !(command & 127u) || command == tk_metronome_observed_command;
}

/* Saturating addition also accepts arbitrary int32 host input without signed
 * overflow. Disabled/finished clicks skip this helper and preserve all PCM. */
static int32_t tk_metronome_add_q15(int32_t base, int32_t click)
{
    if (base > 32767 - click) return 32767;
    if (base < -32767 - click) return -32767;
    return base + click;
}

/* Audio only; out is interleaved L/R Q15. Null or zero-frame calls consume
 * nothing. No allocation, voice, display, USB, flash or wall clock belongs
 * here. Rendering cost is bounded by frames, and idle cost is one word read. */
static void tk_metronome_render(int32_t *out, uint32_t frames)
{
    uint32_t command, i;
    if (!out || !frames) return;
    command = tk_metronome_command;
    if (command != tk_metronome_audio.seen) {
        uint32_t level = command & 127u;
        tk_metronome_audio.seen = command;
        tk_metronome_audio.phase = 0;
        tk_metronome_audio.amplitude = level * TK_METRONOME_MAX_AMPLITUDE / 127u;
        /* A6 (1,760 Hz), E7 (2,640 Hz); phase increments precomputed offline. */
        tk_metronome_audio.increment = command & 128u ? 257113688u : 171409126u;
        tk_metronome_audio.position = level ? 0u : TK_METRONOME_FRAMES;
    }
    if (tk_metronome_audio.position >= TK_METRONOME_FRAMES) {
        tk_metronome_observed_command = command;
        tk_metronome_active = 0;
        return;
    }
    tk_metronome_active = 1;
    for (i = 0; i < frames; i++, out += 2) {
        uint32_t position = tk_metronome_audio.position, gain;
        int32_t triangle, click;
        if (position >= TK_METRONOME_FRAMES) break;
        if (position < TK_METRONOME_ATTACK_FRAMES)
            gain = tk_metronome_audio.amplitude * (position + 1u)
                / TK_METRONOME_ATTACK_FRAMES;
        else
            gain = tk_metronome_audio.amplitude * (TK_METRONOME_FRAMES - position)
                / (TK_METRONOME_FRAMES - TK_METRONOME_ATTACK_FRAMES);
        triangle = (int32_t)(tk_metronome_audio.phase >> 16);
        if (triangle < 16384) triangle *= 2;
        else if (triangle < 49152) triangle = 65536 - triangle * 2;
        else triangle = triangle * 2 - 131072;
        /* Both products fit signed 32 bits; division avoids a negative shift. */
        click = triangle * (int32_t)gain / 32768;
        out[0] = tk_metronome_add_q15(out[0], click);
        out[1] = tk_metronome_add_q15(out[1], click);
        tk_metronome_audio.phase += tk_metronome_audio.increment;
        tk_metronome_audio.position = position + 1u;
    }
    tk_metronome_observed_command = command;
    tk_metronome_active = tk_metronome_audio.position < TK_METRONOME_FRAMES;
}

/* Optional main-owned meter clock, independent of track loop lengths. The
 * caller invokes step once for each global sixteenth, including the initial
 * step at fresh START. Continue retains this clock; STOP calls cancel, and
 * fresh PLAY/REC/START resets it. No clock arithmetic runs in audio. */
typedef struct {
    uint8_t beats, unit, step_in_bar;
} tk_metronome_clock_t;

static void tk_metronome_clock_init(tk_metronome_clock_t *clock)
{
    if (!clock) return;
    clock->beats = TK_METRONOME_DEFAULT_BEATS;
    clock->unit = TK_METRONOME_DEFAULT_UNIT;
    clock->step_in_bar = 0;
}

static void tk_metronome_clock_configure(tk_metronome_clock_t *clock,
                                         uint32_t beats, uint32_t unit)
{
    uint32_t steps_per_beat;
    if (!clock) return;
    if (beats < 1u) beats = 1u;
    if (beats > 12u) beats = 12u;
    unit = unit == 8u ? 8u : 4u;
    steps_per_beat = unit == 8u ? 2u : 4u;
    clock->beats = (uint8_t)beats;
    clock->unit = (uint8_t)unit;
    clock->step_in_bar = (uint8_t)(clock->step_in_bar % (beats * steps_per_beat));
}

static void tk_metronome_clock_reset(tk_metronome_clock_t *clock)
{
    if (clock) clock->step_in_bar = 0;
}

/* Advance even when disabled or level=0: enabling mid-bar keeps the correct
 * accent position. Return 0=no audible beat, 1=ordinary beat, 2=bar accent. */
static uint32_t tk_metronome_clock_step(tk_metronome_clock_t *clock,
                                       uint32_t enabled, uint32_t level)
{
    uint32_t step, steps_per_beat, bar_steps, event = 0;
    if (!clock) return 0;
    if (!clock->beats || clock->beats > 12u ||
        (clock->unit != 4u && clock->unit != 8u))
        tk_metronome_clock_configure(clock,
            clock->beats ? clock->beats : TK_METRONOME_DEFAULT_BEATS, clock->unit);
    steps_per_beat = clock->unit == 8u ? 2u : 4u;
    bar_steps = clock->beats * steps_per_beat;
    step = clock->step_in_bar % bar_steps;
    if (!enabled || !level) tk_metronome_cancel();
    else if (!(step % steps_per_beat)) {
        event = step ? 1u : 2u;
        tk_metronome_trigger(!step, level);
    }
    clock->step_in_bar = (uint8_t)(step + 1u < bar_steps ? step + 1u : 0u);
    return event;
}

#endif
