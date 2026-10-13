/* SPDX-License-Identifier: GPL-3.0-only */
/* Fixed-point click/clock checks; no hardware or wall-clock qualification. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "terkinstein_metronome.h"

static uint32_t checks;
static int32_t pcm[2048], reference[2048], alternate[2048];

static void expect(int condition, const char *why)
{
    checks++;
    if (!condition) { fprintf(stderr, "FAIL: %s\n", why); exit(1); }
}

static uint32_t peak(const int32_t *samples, uint32_t frames)
{
    uint32_t i, maximum = 0;
    for (i = 0; i < frames * 2u; i++) {
        uint32_t magnitude = samples[i] < 0 ? (uint32_t)-samples[i] : (uint32_t)samples[i];
        if (magnitude > maximum) maximum = magnitude;
    }
    return maximum;
}

static void silent_checks(void)
{
    uint32_t i;
    tk_metronome_clock_t clock;
    for (i = 0; i < 2048u; i++) pcm[i] = reference[i] = (int32_t)(i * 73u) - 55000;
    pcm[0] = reference[0] = INT32_MIN;
    pcm[1] = reference[1] = INT32_MAX;
    tk_metronome_init();
    tk_metronome_render(pcm, 1024u);
    expect(!memcmp(pcm, reference, sizeof(pcm)), "startup off preserves PCM byte for byte, including out-of-range input");
    tk_metronome_clock_init(&clock);
    expect(clock.beats == 4u && clock.unit == 4u && !clock.step_in_bar,
           "clock defaults to four quarter-note beats");
    expect(TK_METRONOME_DEFAULT_LEVEL == 32u, "default click level is restrained");
    for (i = 0; i < 71u; i++) {
        expect(!tk_metronome_clock_step(&clock, 0u, 127u), "disabled clock reports no audible beat");
        tk_metronome_render(pcm, 1024u);
        expect(!memcmp(pcm, reference, sizeof(pcm)), "disabled clock cannot reshape or clip existing PCM");
    }
    expect(clock.step_in_bar == 7u, "disabled steps still advance musical meter");
    tk_metronome_trigger(1u, 0u);
    tk_metronome_render(pcm, 1024u);
    expect(!memcmp(pcm, reference, sizeof(pcm)), "zero volume preserves arbitrary PCM");
}

static void sound_checks(void)
{
    uint32_t i, first_peak, accent_peak;
    memset(pcm, 0, sizeof(pcm));
    tk_metronome_init();
    tk_metronome_trigger(0u, TK_METRONOME_DEFAULT_LEVEL);
    tk_metronome_render(pcm, 1024u);
    first_peak = peak(pcm, 1024u);
    expect(first_peak > 1000u && first_peak <= 8192u, "normal click is audible and conservatively bounded");
    expect(!pcm[0] && !pcm[1], "click starts from zero to avoid a discontinuous onset");
    for (i = 0; i < 1024u; i++) {
        expect(pcm[2u * i] == pcm[2u * i + 1u], "click has equal stereo placement");
        if (i >= TK_METRONOME_FRAMES)
            expect(!pcm[2u * i], "finite click leaves the remainder of a long block silent");
    }
    expect(peak(pcm + 2u * 800u, 82u) < first_peak / 4u,
           "click decays near silence within twenty milliseconds");
    memcpy(reference, pcm, sizeof(pcm));
    memset(pcm, 0, sizeof(pcm));
    tk_metronome_render(pcm, 1024u);
    expect(!peak(pcm, 1024u), "one trigger never repeats itself in later blocks");
    tk_metronome_trigger(1u, TK_METRONOME_DEFAULT_LEVEL);
    tk_metronome_render(pcm, 1024u);
    accent_peak = peak(pcm, 1024u);
    expect(accent_peak > 1000u && accent_peak <= 8192u, "accent stays within the same headroom limit");
    expect(memcmp(pcm, reference, sizeof(pcm)) != 0, "bar accent has a distinct sound from ordinary beat");
}

static void block_checks(void)
{
    const uint32_t blocks[] = {1u, 0u, 17u, 256u, 2u, 511u, 237u};
    uint32_t i, at = 0, before;
    tk_metronome_init();
    memset(reference, 0, sizeof(reference));
    tk_metronome_trigger(1u, 91u);
    tk_metronome_render(reference, 1024u);
    tk_metronome_init();
    memset(pcm, 0, sizeof(pcm));
    tk_metronome_trigger(1u, 91u);
    before = tk_metronome_audio.seen;
    tk_metronome_render(0, 256u);
    tk_metronome_render(pcm, 0u);
    expect(tk_metronome_audio.seen == before && tk_metronome_audio.position == TK_METRONOME_FRAMES,
           "null and zero-frame calls do not consume a pending command");
    for (i = 0; i < sizeof(blocks) / sizeof(blocks[0]); i++) {
        uint32_t old_position = tk_metronome_audio.position;
        tk_metronome_render(pcm + 2u * at, blocks[i]);
        if (!blocks[i]) expect(tk_metronome_audio.position == old_position, "zero block cannot advance envelope");
        at += blocks[i];
    }
    expect(at == 1024u, "variable block fixture spans the same number of samples");
    expect(!memcmp(reference, pcm, sizeof(pcm)), "click PCM is independent of nonzero block partitioning");
}

static void command_checks(void)
{
    uint32_t first_word, i;
    tk_metronome_init();
    memset(pcm, 0, sizeof(pcm));
    tk_metronome_trigger(0u, 127u);
    first_word = tk_metronome_command;
    tk_metronome_render(pcm, 100u);
    expect(peak(pcm, 100u) > 6000u, "maximum-volume click becomes audible");
    tk_metronome_cancel();
    for (i = 0; i < 2048u; i++) pcm[i] = reference[i] = (int32_t)(i % 65001u) - 32500;
    tk_metronome_render(pcm, 1024u);
    expect(!memcmp(pcm, reference, sizeof(pcm)), "STOP cancellation ends an active click without touching following PCM");
    expect(tk_metronome_command != first_word, "cancel is a distinct atomic publication");
    tk_metronome_trigger(1u, 127u);
    tk_metronome_trigger(0u, 127u);
    memset(pcm, 0, sizeof(pcm));
    tk_metronome_render(pcm, 1024u);
    tk_metronome_init();
    memset(reference, 0, sizeof(reference));
    tk_metronome_trigger(0u, 127u);
    tk_metronome_render(reference, 1024u);
    expect(!memcmp(pcm, reference, sizeof(pcm)), "latest atomic trigger wins without queued stale clicks");
    memset(pcm, 0, sizeof(pcm));
    tk_metronome_trigger(0u, 127u);
    tk_metronome_render(pcm, 100u);
    memset(pcm, 0, sizeof(pcm));
    tk_metronome_trigger(0u, 127u);
    tk_metronome_render(pcm, 100u);
    expect(!memcmp(pcm, reference, 200u * sizeof(pcm[0])),
           "identical repeated trigger retriggers from zero rather than continuing old phase");
    tk_metronome_init();
    tk_metronome_command = 0xFFFFFF7Fu;
    tk_metronome_audio.seen = tk_metronome_command;
    tk_metronome_trigger(1u, 127u);
    expect(tk_metronome_command == 255u, "generation wraps with unsigned arithmetic and retains complete payload");
    memset(pcm, 0, sizeof(pcm));
    tk_metronome_render(pcm, 1024u);
    expect(peak(pcm, 1024u) > 6000u && tk_metronome_audio.seen == 255u,
           "audio accepts the first trigger after generation wrap");
    tk_metronome_command = 0xFFFFFF00u;
    tk_metronome_audio.seen = tk_metronome_command;
    tk_metronome_cancel();
    memset(pcm, 0, sizeof(pcm));
    tk_metronome_render(pcm, 1024u);
    expect(!peak(pcm, 1024u) && !tk_metronome_command,
           "zero-generation cancellation remains distinguishable across wrap");
}

static void level_and_mix_checks(void)
{
    uint32_t i, maximum;
    tk_metronome_init();
    memset(reference, 0, sizeof(reference));
    tk_metronome_trigger(0u, 127u);
    tk_metronome_render(reference, 1024u);
    tk_metronome_init();
    memset(pcm, 0, sizeof(pcm));
    tk_metronome_trigger(0u, UINT32_MAX);
    tk_metronome_render(pcm, 1024u);
    expect(!memcmp(pcm, reference, sizeof(pcm)), "out-of-range level clamps to 127 without overflow");
    maximum = peak(pcm, 1024u);
    expect(maximum > 6000u && maximum <= TK_METRONOME_MAX_AMPLITUDE,
           "maximum click reserves mix headroom");
    tk_metronome_init();
    memset(pcm, 0, sizeof(pcm));
    tk_metronome_trigger(0u, 8u);
    tk_metronome_render(pcm, 1024u);
    expect(peak(pcm, 1024u) < maximum / 8u && peak(pcm, 1024u) > 100u,
           "level parameter audibly scales rather than only gating click");
    for (i = 0; i < 2048u; i++) {
        uint32_t frame = i / 2u;
        if (frame >= TK_METRONOME_FRAMES) pcm[i] = alternate[i] = INT32_MAX;
        else if (reference[i] < 0) pcm[i] = alternate[i] = INT32_MIN;
        else pcm[i] = alternate[i] = INT32_MAX;
    }
    tk_metronome_init();
    tk_metronome_trigger(0u, 127u);
    tk_metronome_render(pcm, 1024u);
    for (i = 0; i < TK_METRONOME_FRAMES * 2u; i++)
        expect(pcm[i] == (reference[i] < 0 ? -32767 : 32767),
               "click mix saturates either extreme int32 input without signed overflow");
    expect(!memcmp(pcm + TK_METRONOME_FRAMES * 2u,
                   alternate + TK_METRONOME_FRAMES * 2u,
                   sizeof(pcm) - TK_METRONOME_FRAMES * 2u * sizeof(pcm[0])),
           "finished click does not clip untouched remainder of the block");
    expect(tk_metronome_add_q15(32000, 2000) == 32767 &&
           tk_metronome_add_q15(-32000, -2000) == -32767,
           "ordinary positive and negative mix clipping is bounded");
    expect(tk_metronome_add_q15(17000, -2000) == 15000 &&
           tk_metronome_add_q15(-17000, 2000) == -15000,
           "mix inside Q15 bounds adds click correctly");
}

static void meter_checks(void)
{
    tk_metronome_clock_t clock;
    uint32_t beats, unit, i, event, old_command;
    tk_metronome_init();
    tk_metronome_clock_init(&clock);
    expect(!tk_metronome_clock_step(0, 1u, 127u), "null clock is a harmless no-op");
    tk_metronome_clock_init(0);
    tk_metronome_clock_configure(0, 0, 0);
    tk_metronome_clock_reset(0);
    tk_metronome_clock_configure(&clock, 0u, 99u);
    expect(clock.beats == 1u && clock.unit == 4u, "low/invalid meter inputs normalize safely");
    tk_metronome_clock_configure(&clock, UINT32_MAX, 8u);
    expect(clock.beats == 12u && clock.unit == 8u, "high beats clamp to twelve and eighth-note unit is supported");
    for (beats = 1u; beats <= 12u; beats++) {
        for (unit = 4u; unit <= 8u; unit += 4u) {
            uint32_t per_beat = unit == 8u ? 2u : 4u;
            uint32_t bar_steps = beats * per_beat;
            tk_metronome_clock_configure(&clock, beats, unit);
            tk_metronome_clock_reset(&clock);
            for (i = 0; i < bar_steps * 3u; i++) {
                uint32_t wanted = i % bar_steps == 0u ? 2u :
                    (i % per_beat == 0u ? 1u : 0u);
                old_command = tk_metronome_command;
                event = tk_metronome_clock_step(&clock, 1u, 32u);
                expect(event == wanted, "supported meter clicks on each beat and accents only its first beat");
                expect((old_command != tk_metronome_command) == (event != 0u),
                       "non-beat sixteenths publish no retrigger");
                expect(clock.step_in_bar == (i + 1u) % bar_steps,
                       "clock stays bounded and wraps independent of track length");
            }
        }
    }
    tk_metronome_clock_init(&clock);
    for (i = 0; i < 4u; i++) tk_metronome_clock_step(&clock, 0u, 127u);
    expect(tk_metronome_clock_step(&clock, 1u, 32u) == 1u,
           "enabling on second beat resumes meter without a false downbeat");
    tk_metronome_clock_step(&clock, 0u, 32u);
    for (i = 0; i < 2u; i++) tk_metronome_clock_step(&clock, 1u, 0u);
    expect(tk_metronome_clock_step(&clock, 1u, 32u) == 1u,
           "zero-level steps retain beat alignment");
    tk_metronome_cancel();
    expect(clock.step_in_bar == 9u, "STOP cancellation preserves clock for Continue");
    tk_metronome_clock_reset(&clock);
    expect(tk_metronome_clock_step(&clock, 1u, 32u) == 2u,
           "fresh START reset begins with an accent");
    clock.step_in_bar = 15u;
    tk_metronome_clock_configure(&clock, 3u, 8u);
    expect(clock.step_in_bar == 3u, "meter changes normalize phase inside the new bar");
    clock.beats = clock.unit = 0u; clock.step_in_bar = 255u;
    tk_metronome_clock_step(&clock, 1u, 32u);
    expect(clock.beats == 4u && clock.unit == 4u && clock.step_in_bar == 0u,
           "malformed clock state recovers without dividing by zero");
}

static void silence_guard_checks(void)
{
    uint32_t i;
    tk_metronome_init();
    expect(tk_metronome_quiet(), "startup metronome is quiet for flash guard");
    tk_metronome_trigger(1u, 64u);
    expect(!tk_metronome_quiet(), "unconsumed nonzero trigger blocks flash guard");
    tk_metronome_render(0, 256u); tk_metronome_render(pcm, 0u);
    expect(!tk_metronome_quiet(), "null/zero audio cannot acknowledge a pending trigger as quiet");
    memset(pcm, 0, sizeof pcm); tk_metronome_render(pcm, 256u);
    expect(!tk_metronome_quiet() && tk_metronome_active,
           "partially rendered click blocks silence guard without using an engine voice");
    tk_metronome_cancel();
    expect(!tk_metronome_quiet(), "published cancellation cannot claim an active click is already silent");
    tk_metronome_render(0, 256u); tk_metronome_render(pcm, 0u);
    expect(!tk_metronome_quiet(), "null/zero blocks cannot acknowledge an active-click cancellation");
    memset(pcm, 0, sizeof pcm); tk_metronome_render(pcm, 256u);
    expect(tk_metronome_quiet() && !tk_metronome_active && !peak(pcm,256u),
           "nonzero audio consumes STOP before flash guard becomes quiet");
    tk_metronome_trigger(0u,64u); tk_metronome_cancel();
    expect(tk_metronome_quiet(), "cancelling an unplayed pending click creates no artificial tail");
    for(i=0;i<3u;i++) {
        tk_metronome_trigger(i&1u,127u);
        memset(pcm,0,sizeof pcm); tk_metronome_render(pcm,1024u);
        expect(tk_metronome_quiet() && !tk_metronome_active,
               "fully decayed nonzero command is quiet after audio consumes it");
    }
    tk_metronome_command=0xFFFFFF7Fu;
    tk_metronome_observed_command=tk_metronome_command;
    tk_metronome_trigger(1u,127u);
    expect(!tk_metronome_quiet(), "pending trigger remains unacknowledged at generation wrap");
    memset(pcm,0,sizeof pcm); tk_metronome_render(pcm,256u);
    expect(!tk_metronome_quiet(), "wrapped generation's active click still blocks SAVE");
    tk_metronome_cancel();
    expect(!tk_metronome_quiet(), "wrapped active cancellation waits for audio");
    memset(pcm,0,sizeof pcm); tk_metronome_render(pcm,256u);
    expect(tk_metronome_quiet(), "wrapped cancel becomes quiet only after audio consumes it");
}

int main(void)
{
    silent_checks();
    sound_checks();
    block_checks();
    command_checks();
    level_and_mix_checks();
    meter_checks();
    silence_guard_checks();
    printf("Metronome checks: %u passed\n", checks);
    return 0;
}
