/* SPDX-License-Identifier: GPL-3.0-only */
/* Portable behavioral checks for the read-only display tap; no hardware. */
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "terkinstein_visual.h"

static uint32_t checks;

static void expect(int condition, const char *description)
{
    checks++;
    if (!condition) { fprintf(stderr, "FAIL: %s\n", description); exit(1); }
}

static int16_t expected_sample(int64_t value)
{
    if (value > 32760) return 32760;
    if (value < -32760) return -32760;
    return (int16_t)value;
}

static void expect_snapshot(const tk_visual_snapshot_t *snapshot,
                            const int32_t *pcm, uint32_t frames)
{
    uint32_t i, peak = 0, rough = 0;
    int32_t previous = 0;
    for (i = 0; i < TK_VIS_SAMPLES; i++) {
        uint32_t at = (uint32_t)((uint64_t)i * frames / TK_VIS_SAMPLES);
        int64_t left = (int64_t)pcm[at * 2u] / 2;
        int64_t right = (int64_t)pcm[at * 2u + 1u] / 2;
        int16_t mono = expected_sample(left + right);
        int16_t side = expected_sample(left - right);
        uint32_t magnitude = (uint32_t)(mono < 0 ? -mono : mono);
        int32_t difference = (int32_t)mono - previous;
        expect(snapshot->mono[i] == mono, "uniform mono sampling has the expected level and sign");
        expect(snapshot->side[i] == side, "uniform stereo side sampling has the expected level and sign");
        if (magnitude > peak) peak = magnitude;
        if (i) rough += (uint32_t)(difference < 0 ? -difference : difference);
        previous = mono;
    }
    rough /= TK_VIS_SAMPLES - 1u;
    if (rough > 32760u) rough = 32760u;
    expect(snapshot->peak == peak, "peak describes the published mono trace");
    expect(snapshot->rough == rough, "roughness describes changes in the published mono trace");
}

static void reset_and_failed_reads(void)
{
    tk_visual_snapshot_t snapshot, sentinel;
    int32_t pcm[2] = {16000, -8000};
    uint32_t voices[TK_VIS_VOICES] = {60u, 61u, 62u, 63u, 64u, 65u};
    uint32_t i;
    memset(&sentinel, 0xA5, sizeof sentinel);
    snapshot = sentinel;
    tk_visual_init();
    expect(!tk_visual_read(&snapshot), "unpublished state is not a valid snapshot");
    expect(memcmp(&snapshot, &sentinel, sizeof snapshot) == 0,
           "an unpublished read leaves the caller's destination untouched");
    expect(!tk_visual_read(NULL), "a null snapshot destination is rejected");
    for (i = 0; i < 12u; i++) expect(!tk_visual_due(), "init leaves capture disabled");

    tk_visual_capture(pcm, 1u, voices, TK_VIS_VOICES);
    expect(tk_visual_read(&snapshot), "a completed publication can be read");
    expect(snapshot.generation == 2u, "the first published generation is nonzero and even");
    tk_visual_bus.sequence |= 1u;
    snapshot = sentinel;
    expect(!tk_visual_read(&snapshot), "an in-progress odd publication is rejected without waiting");
    expect(memcmp(&snapshot, &sentinel, sizeof snapshot) == 0,
           "an odd publication leaves the caller's destination untouched");

    tk_visual_set_enabled(1u);
    expect(!tk_visual_due(), "the first enabled block is not due");
    tk_visual_init();
    snapshot = sentinel;
    expect(!tk_visual_read(&snapshot), "reset invalidates an earlier publication");
    expect(memcmp(&snapshot, &sentinel, sizeof snapshot) == 0,
           "a reset read leaves the caller's destination untouched");
    expect(!tk_visual_due(), "reset disables a previously enabled tap");
    for (i = 0; i < TK_VIS_SAMPLES / 2u; i++) {
        expect(tk_visual_bus.mono[i] == 0u, "reset clears retained mono samples");
        expect(tk_visual_bus.side[i] == 0u, "reset clears retained side samples");
    }
    for (i = 0; i < TK_VIS_VOICES; i++)
        expect(tk_visual_bus.voice[i] == 0u, "reset clears retained voice data");
    expect(tk_visual_bus.energy == 0u, "reset clears retained energy");
    tk_visual_set_enabled(1u);
    for (i = 1; i <= 4u; i++)
        expect(tk_visual_due() == (i == 4u), "reset restarts the four-block capture divider");
}

static void capture_cadence(void)
{
    uint32_t i;
    tk_visual_init();
    for (i = 0; i < 32u; i++) expect(!tk_visual_due(), "disabled calls never request a capture");
    tk_visual_set_enabled(7u);
    for (i = 1; i <= 24u; i++)
        expect(tk_visual_due() == !(i & 3u), "exactly every fourth enabled call requests capture");
    expect(!tk_visual_due(), "a new group begins after a due call");
    tk_visual_set_enabled(0u);
    for (i = 0; i < 9u; i++) expect(!tk_visual_due(), "disabling pauses capture requests");
    tk_visual_set_enabled(1u);
    expect(!tk_visual_due() && !tk_visual_due() && tk_visual_due(),
           "disabled calls do not advance the audio-owned divider");
}

static void sampling_and_pcm_preservation(void)
{
    static const uint32_t lengths[] = {1u, 3u, 5u, 63u, 64u, 65u, 127u, 256u, 511u};
    static const int32_t pairs[][2] = {
        {10000, 6000}, {-10000, -6000}, {10000, -10000}, {-10000, 10000},
        {INT32_MAX, INT32_MAX}, {INT32_MIN, INT32_MIN},
        {INT32_MAX, INT32_MIN}, {INT32_MIN, INT32_MAX},
        {1, 1}, {-1, -1}, {3, -3}, {-3, 3}, {65522, 65522}, {-65522, -65522}
    };
    int32_t guarded[511u * 2u + 2u], before[511u * 2u + 2u];
    tk_visual_snapshot_t snapshot, repeated;
    uint32_t test, i;
    tk_visual_init();
    for (test = 0; test < sizeof lengths / sizeof lengths[0]; test++) {
        uint32_t frames = lengths[test];
        memset(guarded, 0x3C, sizeof guarded);
        for (i = 0; i < frames; i++) {
            uint32_t pair = i % (sizeof pairs / sizeof pairs[0]);
            guarded[1u + i * 2u] = pairs[pair][0];
            guarded[2u + i * 2u] = pairs[pair][1];
        }
        memcpy(before, guarded, sizeof guarded);
        tk_visual_capture(guarded + 1u, frames, NULL, 0u);
        expect(memcmp(guarded, before, sizeof guarded) == 0,
               "capturing leaves every PCM byte and surrounding guard unchanged");
        expect(tk_visual_read(&snapshot), "small and non-divisible frame counts publish a snapshot");
        expect_snapshot(&snapshot, guarded + 1u, frames);
        expect(!(snapshot.generation & 1u) && snapshot.generation == (test + 1u) * 2u,
               "completed captures advance by even generations");
        expect(tk_visual_read(&repeated), "an unchanged even publication remains readable");
        expect(memcmp(&snapshot, &repeated, sizeof snapshot) == 0,
               "repeated reads of an unchanged publication are identical");
    }
    /* Known stereo cases make the intended sign and scale clear independently
     * of the general sampling oracle above. */
    {
        int32_t same[2] = {10000, 6000}, opposite[2] = {10000, -10000};
        tk_visual_capture(same, 1u, NULL, 0u);
        expect(tk_visual_read(&snapshot), "a constant stereo block is readable");
        for (i = 0; i < TK_VIS_SAMPLES; i++)
            expect(snapshot.mono[i] == 8000 && snapshot.side[i] == 2000,
                   "mono is the stereo average and side is half the stereo difference");
        tk_visual_capture(opposite, 1u, NULL, 0u);
        expect(tk_visual_read(&snapshot), "an opposite-polarity stereo block is readable");
        for (i = 0; i < TK_VIS_SAMPLES; i++)
            expect(snapshot.mono[i] == 0 && snapshot.side[i] == 10000,
                   "opposite-polarity audio cancels in mono while retaining a side signal");
    }
}

static void invalid_capture_preserves_publication(void)
{
    int32_t pcm[2] = {9000, 3000};
    uint32_t voice = 60u | (127u << 8) | (17u << 16) | (1u << 24);
    tk_visual_snapshot_t before, after;
    tk_visual_init();
    tk_visual_capture(NULL, 1u, &voice, 1u);
    tk_visual_capture(pcm, 0u, &voice, 1u);
    expect(!tk_visual_read(&after), "null and zero-frame captures cannot publish stale data after init");
    tk_visual_capture(pcm, 1u, &voice, 1u);
    expect(tk_visual_read(&before), "valid capture publishes before invalid-input checks");
    tk_visual_capture(NULL, 64u, NULL, 0u);
    expect(tk_visual_read(&after) && memcmp(&before, &after, sizeof before) == 0,
           "a null PCM capture preserves the last complete snapshot");
    tk_visual_capture(pcm, 0u, NULL, 0u);
    expect(tk_visual_read(&after) && memcmp(&before, &after, sizeof before) == 0,
           "a zero-frame capture preserves the last complete snapshot");
}

static void energy_and_voices(void)
{
    int32_t pcm[TK_VIS_SAMPLES * 2u] = {0};
    uint32_t voices[TK_VIS_VOICES + 4u], i;
    tk_visual_snapshot_t silence, transient, snapshot;
    for (i = 0; i < sizeof voices / sizeof voices[0]; i++)
        voices[i] = (40u + i) | ((20u + i) << 8) | ((17u + i) << 16) | (1u << 24);
    tk_visual_init();
    tk_visual_capture(pcm, TK_VIS_SAMPLES, voices, TK_VIS_VOICES + 4u);
    expect(tk_visual_read(&silence), "silence still publishes valid visualization data");
    expect(silence.peak == 0u && silence.rough == 0u, "silence has no waveform energy");
    for (i = 0; i < TK_VIS_VOICES; i++)
        expect(silence.voice[i] == voices[i], "voice publication is bounded to six complete atomic words");

    pcm[20u] = pcm[21u] = 24000;
    tk_visual_capture(pcm, TK_VIS_SAMPLES, voices, 2u);
    expect(tk_visual_read(&transient), "a transient publishes a valid snapshot");
    expect(transient.peak == 24000u && transient.peak > silence.peak,
           "a transient increases peak energy");
    expect(transient.rough == 48000u / 63u && transient.rough > silence.rough,
           "a transient increases waveform roughness");
    expect(transient.voice[0] == voices[0] && transient.voice[1] == voices[1],
           "a shorter voice list preserves its supplied voices");
    for (i = 2; i < TK_VIS_VOICES; i++)
        expect(transient.voice[i] == 0u, "a shorter voice list clears previously published voices");

    for (i = 0; i < TK_VIS_SAMPLES; i++) {
        pcm[i * 2u] = pcm[i * 2u + 1u] = i & 1u ? INT32_MAX : INT32_MIN;
    }
    tk_visual_capture(pcm, TK_VIS_SAMPLES, NULL, UINT32_MAX);
    expect(tk_visual_read(&snapshot), "a null voice list is safe even with an excessive count");
    expect(snapshot.peak == 32760u && snapshot.rough == 32760u,
           "extreme alternating audio keeps both energy metrics bounded");
    for (i = 0; i < TK_VIS_VOICES; i++)
        expect(snapshot.voice[i] == 0u, "a null voice list clears every retained voice");
    tk_visual_capture(pcm, TK_VIS_SAMPLES, voices, 0u);
    expect(tk_visual_read(&snapshot), "an empty supplied voice list is readable");
    for (i = 0; i < TK_VIS_VOICES; i++)
        expect(snapshot.voice[i] == 0u, "a zero count never reads the supplied voice list");

    memset(pcm, 0, sizeof pcm);
    tk_visual_capture(pcm, TK_VIS_SAMPLES, NULL, 0u);
    expect(tk_visual_read(&snapshot), "silence after a transient publishes a new snapshot");
    expect(snapshot.peak == 0u && snapshot.rough == 0u,
           "energy cannot remain stale when sound becomes silent");
    for (i = 0; i < TK_VIS_SAMPLES; i++)
        expect(snapshot.mono[i] == 0 && snapshot.side[i] == 0,
               "waveform data cannot remain stale when sound becomes silent");
}

int main(void)
{
    reset_and_failed_reads();
    capture_cadence();
    sampling_and_pcm_preservation();
    invalid_capture_preserves_publication();
    energy_and_voices();
    printf("PASS: %lu visual tap checks\n", (unsigned long)checks);
    return 0;
}
