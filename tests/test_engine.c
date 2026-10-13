/* SPDX-License-Identifier: GPL-3.0-only */
/* Behavioral checks for the portable integer DSP; no hardware or flashing. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "terkinstein_engine.h"
#include "terkinstein_memory.h"

#define FRAMES 256u
#define GUARD 0x13579BDF
static const tk_params_t NORMAL = {38, 46, 34, 76};
static const tk_params_t EXTREME = {127, 127, 127, 127};
static uint32_t checks;

static void expect(int condition, const char *description)
{
    checks++;
    if (!condition) { fprintf(stderr, "FAIL: %s\n", description); exit(1); }
}

static void block(const tk_params_t *params, int32_t *samples)
{
    uint32_t i;
    int32_t guarded[FRAMES * 2u + 2u];
    guarded[0] = GUARD; guarded[FRAMES * 2u + 1u] = GUARD;
    memset(guarded + 1, 0, FRAMES * 2u * sizeof(int32_t));
    tk_render(guarded + 1, FRAMES, params);
    expect(guarded[0] == GUARD && guarded[FRAMES * 2u + 1u] == GUARD, "render stays inside its stereo buffer");
    for (i = 1; i <= FRAMES * 2u; i++)
        expect(guarded[i] >= -24384 && guarded[i] <= 24384, "six-voice output retains PCM headroom");
    if (samples) memcpy(samples, guarded + 1, FRAMES * 2u * sizeof(int32_t));
}

static int silent(const int32_t *samples)
{
    uint32_t i;
    for (i = 0; i < FRAMES * 2u; i++) if (samples[i]) return 0;
    return 1;
}

static int source_held(uint32_t note, uint32_t source)
{
    uint32_t v;
    for (v = 0; v < TK_VOICES; v++) {
        uint32_t cmd = tk_voice[v].command;
        if ((cmd & TK_GATE) && (cmd & 127u) == note && ((cmd >> 14) & 63u) == source)
            return 1;
    }
    return 0;
}

static void idle_and_panic(void)
{
    int32_t samples[FRAMES * 2u];
    tk_init();
    block(&NORMAL, samples);
    expect(tk_quiet() && silent(samples), "unplayed engine is exactly silent");
    tk_note_on(36, 127, 0); tk_note_on(127, 127, 16);
    block(&EXTREME, samples);
    expect(!silent(samples), "held notes produce audio");
    tk_panic();
    block(&EXTREME, samples);
    expect(tk_quiet(), "panic fades every voice within one 256-frame block");
    block(&EXTREME, samples);
    expect(silent(samples), "panic leaves no audio after its short fade");
}

static void source_isolation(void)
{
    uint32_t mask = 1u << 10;
    tk_init();
    tk_note_on(60, 100, 0); tk_note_on(60, 100, 1); tk_note_on(60, 100, 2);
    tk_note_off(60, 1);
    expect(tk_active_notes(50) == mask, "MIDI note-off preserves panel and another channel");
    tk_note_off(60, 0);
    expect(tk_active_notes(50) == mask, "panel note-off preserves MIDI channel 2");
    tk_note_off(60, 2);
    expect(tk_active_notes(50) == 0, "last matching source releases the note");
    tk_note_on(61, 100, 16);
    tk_note_off(61, 15);
    expect(tk_active_notes(50) == (1u << 11), "channel 16 is isolated from channel 15");
    tk_note_on(61, 0, 16);
    expect(tk_active_notes(50) == 0, "velocity-zero note-on acts as note-off");
    tk_note_on(128, 127, 0); tk_note_on(60, 127, TK_SOURCES);
    expect(tk_active_notes(50) == 0, "invalid note and source cannot start a voice");
    tk_note_on(127, UINT32_MAX, 16);
    expect(tk_active_notes(127) == 1u && tk_active_notes(128) == 0,
           "MIDI 127 and excessive velocity remain bounded at the top of the note range");
    block(&EXTREME, NULL);
    tk_panic();

    tk_init();
    tk_note_on(60, 100, 0); tk_note_on(60, 100, 1);
    tk_note_on(60, 100, TK_SOURCE_SEQUENCE);
    expect(source_held(60, 0) && source_held(60, 1) &&
           source_held(60, TK_SOURCE_SEQUENCE),
           "sequencer, panel and USB MIDI own their notes independently");
    tk_note_off(60, TK_SOURCES);
    expect(source_held(60, TK_SOURCE_SEQUENCE),
           "invalid source cannot release the sequencer's note");
    tk_note_off(60, TK_SOURCE_SEQUENCE);
    expect(!source_held(60, TK_SOURCE_SEQUENCE) && source_held(60, 0) && source_held(60, 1),
           "sequencer note-off leaves matching panel and USB notes held");
    tk_note_off(60, 0); tk_note_off(60, 1);
    block(&NORMAL, NULL); tk_panic();
}

static void release_to_silence(const tk_params_t *params)
{
    uint32_t i;
    for (i = 0; i < 2000u && !tk_quiet(); i++) block(params, NULL);
    expect(tk_quiet(), "released expressive notes eventually reach complete silence");
}

static void sustain_controls(void)
{
    uint32_t i;
    int32_t samples[FRAMES * 2u];
    tk_init();
    tk_note_on(60, 110, 1);
    for (i = 0; i < 100u; i++) block(&NORMAL, NULL);
    tk_sustain(1, 64);
    tk_note_off(60, 1);
    expect(tk_active_notes(60) == 1u, "CC64 threshold 64 sustains a released key");
    block(&NORMAL, samples);
    expect(!tk_quiet() && !silent(samples), "pedal-held notes continue sounding");
    tk_note_on(64, 100, 1);
    tk_sustain(1, 63);
    expect(tk_active_notes(60) == (1u << 4), "pedal lift releases only keys already lifted");
    tk_note_off(64, 1);
    release_to_silence(&NORMAL);

    tk_init();
    tk_sustain(1, 127);
    tk_note_on(60, 100, 1); tk_note_on(60, 100, 16);
    block(&NORMAL, NULL);
    tk_note_off(60, 1); tk_note_off(60, 16);
    expect(tk_active_notes(60) == 1u, "sustain belongs to its MIDI channel");
    tk_sustain(TK_SOURCES, 0);
    expect(tk_active_notes(60) == 1u, "invalid source cannot lift another source's pedal");
    tk_sustain(1, 0);
    expect(tk_active_notes(60) == 0, "source-local pedal lift releases its retained notes");
    release_to_silence(&NORMAL);

    tk_init();
    tk_sustain(1, 127);
    tk_note_on(60, 127, 1);
    for (i = 0; i < 100u; i++) block(&NORMAL, NULL);
    tk_note_off(60, 1); tk_note_on(60, 127, 1);
    block(&NORMAL, NULL);
    tk_sustain(1, 0);
    expect(tk_active_notes(60) == 1u, "retriggering a sustained note restores its physical key state");
    tk_note_off(60, 1);
    release_to_silence(&NORMAL);

    tk_init();
    tk_sustain(1, 127);
    tk_note_on(60, 127, 1);
    for (i = 0; i < 100u; i++) block(&NORMAL, NULL);
    tk_note_on(60, 127, 1);
    memset(samples, 0, sizeof(samples));
    tk_render(samples, 1u, &NORMAL);
    tk_note_off(60, 1); tk_sustain(1, 0);
    release_to_silence(&NORMAL);
    block(&NORMAL, samples);
    expect(silent(samples), "lifting sustain cancels a released pending retrigger");

    tk_init();
    tk_sustain(1, 127);
    tk_note_on(60, 127, 1); block(&NORMAL, NULL); tk_note_off(60, 1);
    tk_note_on(62, 127, 1); tk_note_on(64, 127, 1); tk_note_on(65, 127, 1);
    block(&NORMAL, NULL); tk_note_on(67, 127, 1);
    tk_note_on(69, 127, 1); tk_note_on(71, 127, 1);
    expect(tk_active_notes(60) == ((1u << 2) | (1u << 4) | (1u << 5) |
                                   (1u << 7) | (1u << 9) | (1u << 11)),
           "a seventh note steals the old pedal-held voice within the six-voice budget");
    tk_note_off(60, 1); tk_sustain(1, 0);
    expect(tk_active_notes(60) == ((1u << 2) | (1u << 4) | (1u << 5) |
                                   (1u << 7) | (1u << 9) | (1u << 11)),
           "the stolen key's note-off and pedal lift cannot release its physically held replacement");
    tk_all_notes_off(1);
    release_to_silence(&NORMAL);
}

static void channel_mode_controls(void)
{
    uint32_t i;
    tk_init();
    tk_sustain(1, 127); tk_sustain(2, 127);
    tk_note_on(60, 127, 1); tk_note_on(64, 127, 2);
    for (i = 0; i < 100u; i++) block(&NORMAL, NULL);
    tk_all_notes_off(1);
    expect(tk_active_notes(60) == (1u | (1u << 4)), "all-notes-off respects sustain and other sources");
    tk_reset_controllers(1);
    expect(tk_active_notes(60) == (1u << 4), "reset controllers lifts only its own pedal-held note");
    tk_reset_controllers(2);
    expect(tk_active_notes(60) == (1u << 4), "reset controllers preserves physically held keys");
    tk_note_off(64, 2);
    release_to_silence(&NORMAL);

    tk_init();
    tk_sustain(1, 127);
    tk_note_on(60, 127, 1); tk_note_on(64, 127, 2);
    for (i = 0; i < 100u; i++) block(&NORMAL, NULL);
    tk_note_off(60, 1); tk_all_sound_off(1);
    block(&NORMAL, NULL);
    expect(tk_active_notes(60) == (1u << 4), "all-sound-off ignores pedal and leaves another source held");
    tk_note_off(64, 2);
    release_to_silence(&NORMAL);
    tk_note_on(62, 127, 1); block(&NORMAL, NULL); tk_note_off(62, 1);
    expect(tk_active_notes(60) == (1u << 2), "all-sound-off preserves the channel's pedal position");
    tk_panic(); block(&NORMAL, NULL);
    expect(tk_quiet(), "panic clears sustained voices within its short fade");
    tk_note_on(62, 127, 1); block(&NORMAL, NULL); tk_note_off(62, 1);
    expect(tk_active_notes(60) == 0, "panic resets sustain before subsequent notes");
    release_to_silence(&NORMAL);

    tk_init();
    tk_note_on(60, 127, 16);
    for (i = 0; i < 100u; i++) block(&EXTREME, NULL);
    tk_note_off(60, 16); block(&EXTREME, NULL);
    expect(!tk_quiet(), "a normal released note retains its bloom tail");
    tk_all_sound_off(16); block(&EXTREME, NULL);
    expect(tk_quiet(), "all-sound-off also kills a channel's release tails");
}

static void sequence_source_controls(void)
{
    tk_init();
    tk_sustain(TK_SOURCE_SEQUENCE, 127);
    tk_note_on(60, 127, TK_SOURCE_SEQUENCE);
    tk_note_on(62, 127, 1);
    block(&NORMAL, NULL);
    tk_note_off(60, TK_SOURCE_SEQUENCE);
    expect(source_held(60, TK_SOURCE_SEQUENCE) && source_held(62, 1),
           "sequencer sustain holds only its own released note");
    tk_all_notes_off(TK_SOURCE_SEQUENCE);
    expect(source_held(60, TK_SOURCE_SEQUENCE) && source_held(62, 1),
           "sequencer all-notes-off respects its sustain state");
    tk_reset_controllers(TK_SOURCE_SEQUENCE);
    expect(!source_held(60, TK_SOURCE_SEQUENCE) && source_held(62, 1),
           "resetting sequencer controllers releases its sustain, leaving USB MIDI held");
    tk_note_on(64, 127, TK_SOURCE_SEQUENCE);
    block(&NORMAL, NULL);
    tk_all_sound_off(TK_SOURCE_SEQUENCE);
    expect(!source_held(64, TK_SOURCE_SEQUENCE) && source_held(62, 1),
           "sequencer all-sound-off does not silence a USB MIDI note");
    tk_panic(); block(&NORMAL, NULL);
    expect(tk_quiet(), "panic also clears the sequencer and its sustain state");
}

static uint32_t source_slot(uint32_t note, uint32_t source)
{
    uint32_t v;
    for (v = 0; v < TK_VOICES; v++) {
        uint32_t cmd = tk_voice[v].command;
        if ((cmd & TK_GATE) && (cmd & 127u) == note && ((cmd >> 14) & 63u) == source) return v;
    }
    expect(0, "pitch measurement has a live voice from the expected source");
    return 0;
}

/* Measure actual rendered phase motion rather than a pitch lookup helper.
 * Averaging the two oscillators removes intentional equal-and-opposite drift;
 * zero crossings are ambiguous in a detuned, filtered, folded pad. */
static uint32_t phase_rate(uint32_t slot)
{
    uint32_t i;
    uint64_t sum = 0;
    for (i = 0; i < 128u; i++) {
        uint32_t a = tk_voice[slot].phase_a, b = tk_voice[slot].phase_b;
        int32_t samples[2] = {0, 0};
        tk_render(samples, 1u, &NORMAL);
        sum += (uint32_t)(tk_voice[slot].phase_a - a);
        sum += (uint32_t)(tk_voice[slot].phase_b - b);
    }
    return (uint32_t)(sum / 256u);
}

static uint32_t bent_rate(uint32_t bend)
{
    uint32_t i;
    tk_init(); tk_pitch_bend(1, bend); tk_note_on(69, 127, 1);
    for (i = 0; i < 128u; i++) block(&NORMAL, NULL);
    return phase_rate(source_slot(69, 1));
}

static void expect_ratio(uint32_t value, uint32_t reference, uint32_t ratio, const char *description)
{
    int64_t difference = (int64_t)value * 1000000 - (int64_t)reference * ratio;
    if (difference < 0) difference = -difference;
    expect(difference <= (int64_t)reference * 500, description); /* +/-0.05% */
}

static void bend_controls(void)
{
    uint32_t i, centered = bent_rate(8192), up = bent_rate(16383), down = bent_rate(0);
    expect_ratio(up, centered, 1122462, "maximum MIDI bend raises rendered pitch by two semitones");
    expect_ratio(down, centered, 890899, "minimum MIDI bend lowers rendered pitch by two semitones");
    expect(up > centered && down < centered, "bend direction agrees with MIDI wheel direction");
    expect(bent_rate(UINT32_MAX) == up, "excessive pitch-bend input clamps at the maximum MIDI endpoint");
    tk_init(); tk_note_on(69, 127, 1); tk_note_on(69, 127, 16);
    tk_pitch_bend(1, 16383); tk_pitch_bend(TK_SOURCES, 0);
    for (i = 0; i < 128u; i++) block(&NORMAL, NULL);
    expect_ratio(phase_rate(source_slot(69, 1)), centered, 1122462, "bend reaches notes already held on its channel");
    expect(phase_rate(source_slot(69, 16)) == centered, "channel 1 bend and invalid sources leave channel 16 pitch centered");
    tk_reset_controllers(1);
    expect(phase_rate(source_slot(69, 1)) == centered, "controller reset immediately centers a held note's pitch");
    tk_pitch_bend(16, 0);
    for (i = 0; i < 128u; i++) block(&NORMAL, NULL);
    tk_panic(); block(&NORMAL, NULL);
    tk_note_on(69, 127, 16);
    for (i = 0; i < 128u; i++) block(&NORMAL, NULL);
    expect(phase_rate(source_slot(69, 16)) == centered, "panic resets bend for subsequent notes");

    tk_init(); tk_note_on(69, 127, 0); tk_note_on(69, 127, TK_SOURCE_SEQUENCE);
    tk_pitch_bend(TK_SOURCE_SEQUENCE, 16383);
    for (i = 0; i < 128u; i++) block(&NORMAL, NULL);
    expect_ratio(phase_rate(source_slot(69, TK_SOURCE_SEQUENCE)), centered, 1122462,
                 "sequencer bend reaches its own held note");
    expect(phase_rate(source_slot(69, 0)) == centered,
           "sequencer bend leaves the panel note centered");
    tk_reset_controllers(TK_SOURCE_SEQUENCE);
    expect(phase_rate(source_slot(69, TK_SOURCE_SEQUENCE)) == centered,
           "sequencer controller reset immediately centers its held note");
}

static int32_t envelope_attack_at(uint32_t attack)
{
    uint32_t i, slot;
    tk_init(); tk_set_envelope(attack, 64);
    tk_note_on(69, 100, TK_SOURCE_SEQUENCE);
    slot = source_slot(69, TK_SOURCE_SEQUENCE);
    for (i = 0; i < 8u; i++) block(&NORMAL, NULL);
    return tk_voice[slot].env;
}

static int32_t envelope_release_at(uint32_t release)
{
    uint32_t i, slot;
    tk_init(); tk_set_envelope(64, release);
    tk_note_on(69, 100, TK_SOURCE_SEQUENCE);
    slot = source_slot(69, TK_SOURCE_SEQUENCE);
    for (i = 0; i < 100u; i++) block(&NORMAL, NULL);
    tk_note_off(69, TK_SOURCE_SEQUENCE);
    for (i = 0; i < 8u; i++) block(&NORMAL, NULL);
    return tk_voice[slot].env;
}

static void envelope_controls(void)
{
    int32_t fast, neutral, slow;
    fast = envelope_attack_at(0); neutral = envelope_attack_at(64); slow = envelope_attack_at(127);
    expect(fast > neutral && neutral > slow,
           "independent attack control changes how quickly a held note blooms");
    fast = envelope_release_at(0); neutral = envelope_release_at(64); slow = envelope_release_at(127);
    expect(fast < neutral && neutral < slow,
           "independent release control changes how long a lifted note fades");
    tk_set_envelope(UINT32_MAX, UINT32_MAX);
    expect(tk_envelope_control == (127u | (127u << 7)),
           "envelope control clamps out-of-range values before its atomic publish");
}

static void timbre_history(void)
{
    tk_memory_state_t memory;
    int32_t shade = 17, fracture = 23, full_shade, full_fracture;
    uint32_t i, amount;
    tk_memory_init(&memory);
    expect(sizeof(memory) <= 32u, "four remembered timbres fit a compact bounded history");
    expect(!tk_memory_pick(&memory, 60, 1, &shade, &fracture) && shade == 17 && fracture == 23,
           "an empty history leaves the fresh timbre untouched");
    tk_memory_remember(&memory, 60, 1, 1000, 2000);
    tk_memory_remember(&memory, 64, 1, 4000, 5000);
    expect(tk_memory_pick(&memory, 61, 1, &shade, &fracture) && shade == 1000 && fracture == 2000,
           "new pitch chooses the nearest remembered timbre");
    expect(tk_memory_pick(&memory, 62, 1, &shade, &fracture) && shade == 4000 && fracture == 5000,
           "equal pitch distances choose the newer timbre deterministically");
    tk_memory_remember(&memory, 60, 1, 3000, 3500);
    expect(tk_memory_pick(&memory, 62, 1, &shade, &fracture) && shade == 3000 && fracture == 3500,
           "a repeated note refreshes its timbre and recency");
    shade = 17; fracture = 23;
    expect(!tk_memory_pick(&memory, 62, 2, &shade, &fracture) && shade == 17 && fracture == 23,
           "timbre lookup never borrows another MIDI channel's history");
    tk_memory_remember(&memory, 128, 1, 0, 0); tk_memory_remember(&memory, 60, 18, 0, 0);
    expect(tk_memory_pick(&memory, 60, 1, &shade, &fracture) && shade == 3000 && fracture == 3500,
           "invalid notes and sources cannot modify the history");
    tk_memory_remember(&memory, 60, TK_SOURCE_SEQUENCE, 4500, 5500);
    expect(tk_memory_pick(&memory, 62, TK_SOURCE_SEQUENCE, &shade, &fracture) &&
           shade == 4500 && fracture == 5500,
           "sequencer source records and retrieves its own afterimage");
    expect(tk_memory_pick(&memory, 62, 1, &shade, &fracture) &&
           shade == 3000 && fracture == 3500,
           "sequencer history never replaces a MIDI channel's color at lookup");

    tk_memory_init(&memory);
    for (i = 1; i <= 5u; i++) tk_memory_remember(&memory, 60, i, (int32_t)i * 100, 1000);
    expect(!tk_memory_pick(&memory, 60, 1, &shade, &fracture), "the fifth distinct timbre evicts the oldest entry");
    expect(tk_memory_pick(&memory, 60, 2, &shade, &fracture) && shade == 200,
           "history eviction preserves the other three old timbres");
    tk_memory_remember(&memory, 60, 2, 220, 1000);
    tk_memory_remember(&memory, 60, 6, 600, 1000);
    expect(!tk_memory_pick(&memory, 60, 3, &shade, &fracture) &&
           tk_memory_pick(&memory, 60, 2, &shade, &fracture) && shade == 220,
           "refreshing an entry protects it from the next oldest-entry eviction");
    tk_memory_remember(&memory, 127, 16, INT32_MIN, INT32_MAX);
    expect(tk_memory_pick(&memory, 127, 16, &shade, &fracture) && shade == 0 && fracture == TK_MEMORY_MAX_Q8,
           "remembered macro values clamp safely at both Q8 boundaries");

    for (amount = 0; amount <= 127u; amount++) {
        int32_t mixed = tk_memory_mix_q8(3000, 27000, amount);
        expect(mixed >= 3000 && mixed <= 27000, "memory amount stays between fresh and remembered timbres");
    }
    expect(tk_memory_mix_q8(3000, 27000, 0) == 3000 &&
           tk_memory_mix_q8(3000, 27000, 127) == 27000,
           "memory mix has exact fresh and remembered endpoints");
    expect(tk_memory_mix_q8(INT32_MIN, INT32_MAX, UINT32_MAX) == TK_MEMORY_MAX_Q8,
           "invalid memory-mix macros and amounts remain bounded");

    tk_memory_imprint_q8(38 * 256, 46 * 256, 0, &shade, &fracture);
    expect(shade == 38 * 256 && fracture == 46 * 256, "an unbloomed voice leaves no extra imprint");
    tk_memory_imprint_q8(38 * 256, 46 * 256, TK_MEMORY_ENV_FULL, &full_shade, &full_fracture);
    expect(full_shade > shade && full_fracture > fracture &&
           full_shade <= shade + 12 * 256 && full_fracture <= fracture + 16 * 256,
           "a mature envelope leaves a modest darker and corroded timbre imprint");
    tk_memory_imprint_q8(TK_MEMORY_MAX_Q8, 0, UINT32_MAX, &shade, &fracture);
    expect(shade == TK_MEMORY_MAX_Q8 && fracture == 0,
           "maximum imprint stays bounded and a clean oscillator stays clean");
    for (amount = 0; amount < 2u; amount++) {
        int32_t offset = amount ? TK_MEMORY_MAX_Q8 : -TK_MEMORY_MAX_Q8;
        for (i = 0; i < 5500u && offset; i++) {
            int32_t next = tk_memory_decay_q8(offset);
            expect(amount ? next >= 0 && next < offset : next <= 0 && next > offset,
                   "remembered timbre decays monotonically without crossing fresh timbre");
            offset = next;
        }
        expect(offset == 0 && tk_memory_decay_q8(offset) == 0,
               "both memory-offset directions reach exact fresh timbre within four seconds");
    }
}

/* The first mature note imprints history. Settle a brighter/cleaner live macro
 * state in silence, then hash only the next played gesture. A forgotten run
 * shares the same control timeline, source, phase seeds and allocation. */
static uint32_t afterimage_render(uint32_t amount, uint32_t origin_source,
                                  uint32_t destination_source, uint32_t erase)
{
    static const tk_params_t fresh = {18, 20, 34, 76};
    uint32_t i, j, hash = 2166136261u;
    int32_t samples[FRAMES * 2u];
    tk_init(); tk_set_memory(amount); tk_note_on(60, 110, origin_source);
    for (i = 0; i < 128u; i++) block(&NORMAL, NULL);
    tk_note_off(60, origin_source);
    release_to_silence(&NORMAL);
    for (i = 0; i < 128u; i++) block(&fresh, NULL);
    if (erase == 1u) tk_forget_memory();
    if (erase == 2u) tk_panic();
    tk_note_on(62, 110, destination_source);
    for (i = 0; i < 96u; i++) {
        block(&fresh, samples);
        for (j = 0; j < FRAMES * 2u; j++) {
            hash ^= (uint32_t)samples[j];
            hash *= 16777619u;
        }
    }
    return hash;
}

static uint32_t replaced_chord_render(uint32_t amount, int forget)
{
    uint32_t i, j, hash = 2166136261u;
    int32_t samples[FRAMES * 2u];
    tk_init(); tk_set_memory(amount);
    for (i = 0; i < 128u; i++) block(&NORMAL, NULL);
    tk_note_on(48, 110, 1); tk_note_on(51, 110, 1);
    tk_note_on(55, 110, 1); tk_note_on(60, 110, 1);
    for (i = 0; i < 128u; i++) block(&NORMAL, NULL);
    if (forget) tk_forget_memory();
    tk_note_on(62, 110, 1); tk_note_on(65, 110, 1);
    tk_note_on(69, 110, 1); tk_note_on(74, 110, 1);
    for (i = 0; i < 96u; i++) {
        block(&NORMAL, samples);
        for (j = 0; j < FRAMES * 2u; j++) {
            hash ^= (uint32_t)samples[j];
            hash *= 16777619u;
        }
    }
    return hash;
}

static uint32_t fresh_memory_render(uint32_t amount)
{
    static const tk_params_t clean = {18, 0, 34, 76};
    uint32_t i, j, hash = 2166136261u;
    int32_t samples[FRAMES * 2u];
    tk_init(); tk_set_memory(amount); tk_note_on(69, 100, 1);
    for (i = 0; i < 128u; i++) {
        const tk_params_t *params = i < 16u ? &NORMAL : i < 48u ? &EXTREME : i < 80u ? &clean : &NORMAL;
        block(params, samples);
        for (j = 0; j < FRAMES * 2u; j++) {
            hash ^= (uint32_t)samples[j];
            hash *= 16777619u;
        }
    }
    return hash;
}

static void afterimage_audio(void)
{
    static const tk_params_t clean = {18, 0, 34, 76};
    uint32_t i, fresh = afterimage_render(0, 1, 1, 0);
    uint32_t remembered = afterimage_render(127, 1, 1, 0);
    uint32_t cleared = afterimage_render(127, 1, 1, 1);
    uint32_t sequence_fresh = afterimage_render(0, TK_SOURCE_SEQUENCE, TK_SOURCE_SEQUENCE, 0);
    uint32_t slot;
    expect(fresh_memory_render(127) == fresh_memory_render(0),
           "enabled memory with no inherited timbre preserves fresh PCM through live macro gestures");
    expect(remembered != fresh, "remembered timbre changes the next gesture's rendered audio");
    expect(cleared == fresh, "forget before the next gesture restores fresh rendered audio exactly");
    expect(afterimage_render(127, 1, 1, 2) == fresh, "panic clears timbre history before subsequent notes");
    expect(afterimage_render(127, 1, 2, 0) == afterimage_render(127, 1, 2, 1),
           "another MIDI channel's remembered chord cannot color this channel's rendered audio");
    expect(afterimage_render(127, TK_SOURCE_SEQUENCE, TK_SOURCE_SEQUENCE, 0) != sequence_fresh,
           "a sequencer note inherits its own recorded afterimage in rendered audio");
    expect(afterimage_render(127, TK_SOURCE_SEQUENCE, TK_SOURCE_SEQUENCE, 1) == sequence_fresh,
           "forget clears a sequencer afterimage from rendered audio");
    expect(afterimage_render(127, 1, TK_SOURCE_SEQUENCE, 0) ==
           afterimage_render(127, 1, TK_SOURCE_SEQUENCE, 1) &&
           afterimage_render(127, TK_SOURCE_SEQUENCE, 1, 0) ==
           afterimage_render(127, TK_SOURCE_SEQUENCE, 1, 1),
           "sequencer and MIDI notes cannot borrow one another's afterimage");
    expect(replaced_chord_render(127, 1) == replaced_chord_render(0, 0),
           "forget and a full held-chord replacement in one block cannot restore the old trace");

    afterimage_render(127, 1, 1, 0);
    slot = source_slot(62, 1);
    expect(tk_voice[slot].memory_shade || tk_voice[slot].memory_fracture,
           "the trace persists during the next gesture's early bloom");
    for (i = 0; i < 800u; i++) block(&NORMAL, NULL);
    expect(tk_voice[slot].memory_shade == 0 && tk_voice[slot].memory_fracture == 0 &&
           tk_voice[slot].color_shade == tk_macro[0] && tk_voice[slot].color_fracture == tk_macro[1],
           "the live voice returns to fresh macro timbre after its finite memory decay");

    afterimage_render(127, 1, 1, 0);
    slot = source_slot(62, 1);
    for (i = 0; i < 128u; i++) block(&clean, NULL);
    expect(tk_voice[slot].color_fracture == 0 && tk_voice[slot].color_drive == 4096,
           "live FX bypass reaches clean synthesis despite remembered corrosion");
    afterimage_render(127, 1, 1, 0);
    slot = source_slot(62, 1);
    tk_set_memory(0);
    for (i = 0; i < 128u; i++) block(&clean, NULL);
    expect(tk_voice[slot].memory_shade || tk_voice[slot].memory_fracture,
           "the live MEMORY=0 comparison is exercised before natural trace decay");
    block(&NORMAL, NULL);
    expect(tk_voice[slot].color_shade == tk_macro[0] && tk_voice[slot].color_fracture == tk_macro[1],
           "settled MEMORY=0 restores ordinary macro response even while old offsets decay");
    tk_panic(); block(&NORMAL, NULL);
    expect(tk_quiet(), "panic remains short and silent with an active afterimage");
}

static void extreme_chords(void)
{
    uint32_t corner, i, j;
    for (corner = 0; corner < 16u; corner++) {
        tk_params_t params;
        params.shade = corner & 1u ? 127 : 0;
        params.fracture = corner & 2u ? 127 : 0;
        params.drift = corner & 4u ? 127 : 0;
        params.bloom = corner & 8u ? 127 : 0;
        tk_init();
        tk_set_memory(127);
        tk_sustain(0, 127); tk_sustain(1, 0);
        tk_pitch_bend(0, 0); tk_pitch_bend(1, 16383);
        tk_pitch_bend(2, 0); tk_pitch_bend(16, 16383);
        tk_note_on(0, 127, 0); tk_note_on(1, 127, 1);
        tk_note_on(126, 127, 2); tk_note_on(127, 127, 16);
        for (i = 0; i < 64u; i++) block(&params, NULL);
        /* Exercise deterministic stealing through the entire MIDI range. */
        for (j = 0; j < 128u; j++) {
            tk_note_on(j, 127, j % 17u);
            block(&params, NULL);
        }
        tk_panic();
    }
    {
        tk_params_t invalid = {255, 255, 255, 255};
        tk_init();
        for (j = 0; j < TK_VOICES; j++) tk_note_on(120u + j, UINT32_MAX, j);
        for (i = 0; i < 64u; i++) block(&invalid, NULL);
        tk_panic();
    }
}

static void release(void)
{
    uint32_t i;
    int32_t samples[FRAMES * 2u];
    tk_init();
    tk_note_on(48, 127, 0); tk_note_on(55, 127, 1);
    for (i = 0; i < 350u; i++) block(&EXTREME, NULL);
    tk_note_off(48, 0); tk_note_off(55, 1);
    for (i = 0; i < 2000u && !tk_quiet(); i++) block(&EXTREME, NULL);
    expect(tk_quiet(), "maximum bloom eventually releases to silence within 12 seconds");
    block(&EXTREME, samples);
    expect(silent(samples), "released voices leave no residual DC or audio");
}

static void canceled_retrigger(void)
{
    uint32_t i;
    int32_t one[2] = {0, 0};
    tk_init(); tk_note_on(60, 127, 0);
    for (i = 0; i < 350u; i++) block(&EXTREME, NULL);
    tk_note_on(60, 127, 0);
    tk_render(one, 1u, &EXTREME); /* retrigger queued while the old voice fades */
    tk_note_off(60, 0);
    for (i = 0; i < 2000u && !tk_quiet(); i++) block(&EXTREME, NULL);
    expect(tk_quiet(), "a canceled pending retrigger cannot resurrect a held voice");
    tk_init();
    for (i = 0; i < 512u; i++) {
        tk_note_on(i % 128u, 127, i % 17u);
        if (i & 1u) tk_note_off(i % 128u, i % 17u);
    }
    tk_panic(); block(&EXTREME, NULL);
    expect(tk_quiet(), "panic cancels rapid pending note and steal commands");
}

static void additive_render(void)
{
    uint32_t i;
    int32_t zero[FRAMES * 2u] = {0}, offset[FRAMES * 2u];
    tk_init(); tk_note_on(69, 100, 0); tk_render(zero, FRAMES, &NORMAL);
    tk_init(); tk_note_on(69, 100, 0);
    for (i = 0; i < FRAMES * 2u; i++) offset[i] = 123;
    tk_render(offset, FRAMES, &NORMAL);
    for (i = 0; i < FRAMES * 2u; i++)
        expect(offset[i] == zero[i] + 123, "render adds to the platform's existing stereo buffer");
}

static void clean_fracture(void)
{
    uint32_t i;
    tk_params_t clean = {88, 0, 32, 80};
    tk_init();
    for (i = 0; i < 100u; i++) block(&clean, NULL);
    expect(tk_fold(-32760) == -32760 && tk_fold(-12000) == -12000 &&
           tk_fold(0) == 0 && tk_fold(12000) == 12000 && tk_fold(32760) == 32760,
           "zero fracture passes the clean oscillator through unchanged");
}

static uint32_t deterministic_render(void)
{
    uint32_t i, j, hash = 2166136261u;
    int32_t samples[FRAMES * 2u];
    tk_init();
    tk_note_on(48, 105, 0); tk_note_on(51, 99, 1); tk_note_on(55, 113, 2);
    for (i = 0; i < 96u; i++) {
        if (i == 32u) tk_note_on(70, 81, 16);
        if (i == 64u) tk_note_off(51, 1);
        block(i < 48u ? &NORMAL : &EXTREME, samples);
        for (j = 0; j < FRAMES * 2u; j++) {
            hash ^= (uint32_t)samples[j];
            hash *= 16777619u;
        }
    }
    return hash;
}

/* The live instrument can change without turning a recorded drum loop back
 * into a pad. Rendered PCM checks independent track instruments and patches,
 * rather than only checking published control words. */
static uint32_t kit_lane_render(uint32_t note, uint32_t release_after_start,
                                uint32_t *energy)
{
    uint32_t i, j, hash = 2166136261u;
    int32_t samples[FRAMES * 2u];
    *energy = 0;
    tk_init();
    tk_set_instrument(TK_INSTRUMENT_DRUM_KIT);
    tk_note_on(note, 112u, 0);
    block(&NORMAL, samples);
    if (release_after_start) tk_note_off(note, 0);
    for (i = 0; i < 12u; i++) {
        block(&NORMAL, samples);
        for (j = 0; j < FRAMES * 2u; j++) {
            int32_t sample = samples[j];
            *energy += (uint32_t)(sample < 0 ? -sample : sample);
            hash ^= (uint32_t)sample;
            hash *= 16777619u;
        }
    }
    if (release_after_start) {
        for (i = 0; i < 200u && !tk_quiet(); i++) block(&NORMAL, NULL);
        expect(tk_quiet(), "a released drum one-shot reaches silence without a stale tail");
    } else {
        uint32_t slot = source_slot(note, 0u);
        for (i = 0; i < 200u && tk_voice[slot].tail; i++) block(&NORMAL, NULL);
        expect(!tk_voice[slot].tail, "a held drum strike decays without retriggering");
        block(&NORMAL, samples);
        expect(silent(samples) && !tk_quiet(),
               "silent held drum keeps the conservative flash-save guard");
        tk_note_off(note, 0u);
        block(&NORMAL, samples);
        expect(silent(samples) && tk_quiet(),
               "lifting a finished drum key permits a silent flash save");
    }
    return hash;
}

static uint32_t instrument_render(uint32_t mode, uint32_t *energy)
{
    uint32_t i, j, hash = 2166136261u;
    int32_t samples[FRAMES * 2u];
    *energy = 0;
    tk_init();
    tk_set_instrument(mode);
    tk_note_on(60, 112, 0);
    for (i = 0; i < 24u; i++) {
        block(&NORMAL, samples);
        for (j = 0; j < FRAMES * 2u; j++) {
            int32_t sample = samples[j];
            *energy += (uint32_t)(sample < 0 ? -sample : sample);
            hash ^= (uint32_t)sample;
            hash *= 16777619u;
        }
    }
    return hash;
}

static uint32_t metal_control_render(uint32_t frozen, uint32_t drift,
                                     uint32_t attack, uint32_t *early_env)
{
    tk_params_t params = {38u, 100u, (uint8_t)drift, 76u};
    uint32_t i, j, source = frozen ? TK_SOURCE_SEQUENCE : 0u;
    uint32_t hash = 2166136261u;
    int32_t samples[FRAMES * 2u];
    tk_init();
    if (frozen) {
        tk_set_source_instrument(source, TK_INSTRUMENT_METAL);
        tk_set_source_patch(source, &params, attack, 64u);
    } else {
        tk_set_instrument(TK_INSTRUMENT_METAL);
        tk_set_envelope(attack, 64u);
    }
    tk_note_on(60u, 112u, source);
    for (i = 0; i < 16u; i++) {
        block(frozen ? &NORMAL : &params, samples);
        if (!i && early_env) *early_env = (uint32_t)tk_voice[source_slot(60u, source)].env;
        for (j = 0; j < FRAMES * 2u; j++) {
            hash ^= (uint32_t)samples[j];
            hash *= 16777619u;
        }
    }
    return hash;
}

static uint32_t frozen_pad_shade_render(uint32_t modulate, uint32_t *starting,
                                        uint32_t *atmos, uint32_t *melody)
{
    tk_params_t atmosphere = {38u, 46u, 34u, 76u};
    tk_params_t other = {95u, 32u, 18u, 80u};
    uint32_t i, j, hash = 2166136261u;
    int32_t samples[FRAMES * 2u];
    tk_init();
    tk_set_source_patch(TK_SOURCE_ATMOS, &atmosphere, 64u, 64u);
    tk_set_source_patch(TK_SOURCE_SEQUENCE, &other, 64u, 64u);
    tk_set_source_shade(TK_SOURCE_ATMOS, 80u);
    tk_note_on(60u, 112u, TK_SOURCE_ATMOS);
    tk_note_on(67u, 112u, TK_SOURCE_SEQUENCE);
    block(&NORMAL, NULL);
    if (starting) *starting = (uint32_t)tk_voice[source_slot(60u, TK_SOURCE_ATMOS)].color_shade;
    if (modulate) tk_set_source_shade(TK_SOURCE_ATMOS, 127u);
    for (i = 0; i < 64u; i++) {
        block(&NORMAL, samples);
        for (j = 0; j < FRAMES * 2u; j++) {
            hash ^= (uint32_t)samples[j];
            hash *= 16777619u;
        }
    }
    if (atmos) *atmos = (uint32_t)tk_voice[source_slot(60u, TK_SOURCE_ATMOS)].color_shade;
    if (melody) *melody = (uint32_t)tk_voice[source_slot(67u, TK_SOURCE_SEQUENCE)].color_shade;
    return hash;
}

static uint32_t track_kit_render(int switch_live_mode)
{
    uint32_t i, j, hash = 2166136261u;
    int32_t samples[FRAMES * 2u];
    tk_init();
    tk_set_source_instrument(TK_SOURCE_DRUM, TK_INSTRUMENT_DRUM_KIT);
    tk_note_on(60, 112, TK_SOURCE_DRUM);
    block(&NORMAL, NULL);
    if (switch_live_mode) tk_set_instrument(TK_INSTRUMENT_METAL);
    for (i = 0; i < 12u; i++) {
        block(switch_live_mode ? &EXTREME : &NORMAL, samples);
        for (j = 0; j < FRAMES * 2u; j++) {
            hash ^= (uint32_t)samples[j];
            hash *= 16777619u;
        }
    }
    return hash;
}

static uint32_t track_patch_render(uint32_t source, uint32_t instrument,
                                   int switch_live_patch)
{
    uint32_t i, j, hash = 2166136261u;
    int32_t samples[FRAMES * 2u];
    tk_init();
    tk_set_source_instrument(source, instrument);
    tk_set_source_patch(source, &NORMAL, 64u, 64u);
    tk_note_on(60, 112, source);
    block(&NORMAL, NULL);
    if (switch_live_patch) {
        tk_set_instrument(TK_INSTRUMENT_METAL);
        tk_set_envelope(0u, 127u);
        tk_set_memory(127u);
    }
    for (i = 0; i < 24u; i++) {
        block(switch_live_patch ? &EXTREME : &NORMAL, samples);
        for (j = 0; j < FRAMES * 2u; j++) {
            hash ^= (uint32_t)samples[j];
            hash *= 16777619u;
        }
    }
    return hash;
}

static void new_instruments(void)
{
    uint32_t lane, other, energy, held_energy, pad_energy, metal_energy;
    uint32_t hashes[12];
    uint32_t held_hash, released_hash, pad_hash, metal_hash;
    int32_t samples[FRAMES * 2u];

    pad_hash = instrument_render(TK_INSTRUMENT_PAD, &pad_energy);
    metal_hash = instrument_render(TK_INSTRUMENT_METAL, &metal_energy);
    expect(pad_energy > 1000u && metal_energy > 1000u,
           "PAD and METAL both make audible PCM");
    expect(pad_hash != metal_hash, "METAL changes the rendered timbre from PAD");
    {
        uint32_t start, static_atmos, static_melody, moved_atmos, moved_melody;
        uint32_t still = frozen_pad_shade_render(0u, &start, &static_atmos, &static_melody);
        uint32_t moving = frozen_pad_shade_render(1u, NULL, &moved_atmos, &moved_melody);
        expect(start == 80u * 256u && static_atmos == start,
               "a frozen PAD starts at published track SHADE and stays static without LFO updates");
        expect(moving != still && moved_atmos > static_atmos + 40u * 256u,
               "published atmosphere SHADE changes held frozen PAD audio and filter color");
        expect(static_melody == 95u * 256u && moved_melody == static_melody,
               "another frozen PAD source keeps its own SHADE during atmosphere modulation");
    }
    {
        uint32_t fast_live, slow_live, fast_track, slow_track;
        expect(metal_control_render(0u, 0u, 64u, NULL) !=
               metal_control_render(0u, 127u, 64u, NULL),
               "live METAL DRIFT changes rendered oscillator sidebands");
        expect(metal_control_render(1u, 0u, 64u, NULL) !=
               metal_control_render(1u, 127u, 64u, NULL),
               "a frozen METAL track retains its own DRIFT sidebands");
        expect(metal_control_render(0u, 64u, 0u, &fast_live) !=
               metal_control_render(0u, 64u, 127u, &slow_live) &&
               fast_live > slow_live * 8u,
               "live METAL ATTACK ranges from a strike to a slow swell");
        expect(metal_control_render(1u, 64u, 0u, &fast_track) !=
               metal_control_render(1u, 64u, 127u, &slow_track) &&
               fast_track > slow_track * 8u,
               "frozen METAL ATTACK remains audible in its recorded track");
    }

    /* C4 through B4 address every lane of the chromatic drum kit. */
    for (lane = 0; lane < 12u; lane++) {
        hashes[lane] = kit_lane_render(60u + lane, 1u, &energy);
        expect(energy > 1000u, "every drum key produces audible PCM after note-off");
        for (other = 0; other < lane; other++)
            expect(hashes[lane] != hashes[other],
                   "different drum keys have distinct rendered sounds");
    }
    held_hash = kit_lane_render(60u, 0u, &held_energy);
    released_hash = kit_lane_render(60u, 1u, &energy);
    expect(held_hash == released_hash && held_energy == energy,
           "short sequencer gates do not truncate a one-shot drum");
    expect(track_kit_render(0) == track_kit_render(1),
           "the drum track keeps its timbre when the live mode and macros change");
    expect(track_patch_render(TK_SOURCE_SEQUENCE, TK_INSTRUMENT_METAL, 0) ==
           track_patch_render(TK_SOURCE_SEQUENCE, TK_INSTRUMENT_METAL, 1),
           "the melody track keeps its recorded metal patch through live preset changes");
    expect(track_patch_render(TK_SOURCE_ATMOS, TK_INSTRUMENT_PAD, 0) ==
           track_patch_render(TK_SOURCE_ATMOS, TK_INSTRUMENT_PAD, 1),
           "the atmosphere track keeps its recorded pad patch through live preset changes");

    tk_init();
    tk_set_source_instrument(TK_SOURCE_DRUM, TK_INSTRUMENT_DRUM_KIT);
    tk_set_source_instrument(TK_SOURCE_SEQUENCE, TK_INSTRUMENT_METAL);
    tk_set_source_instrument(TK_SOURCE_ATMOS, TK_INSTRUMENT_PAD);
    tk_set_source_patch(TK_SOURCE_SEQUENCE, &NORMAL, 64u, 64u);
    tk_set_source_patch(TK_SOURCE_ATMOS, &NORMAL, 64u, 64u);
    tk_set_instrument(TK_INSTRUMENT_METAL);
    tk_note_on(60u, 127u, TK_SOURCE_DRUM);     /* kick in the drum loop */
    tk_note_on(64u, 127u, TK_SOURCE_SEQUENCE); /* metallic melody loop */
    tk_note_on(67u, 127u, TK_SOURCE_ATMOS);    /* pad atmosphere loop */
    tk_set_instrument(TK_INSTRUMENT_PAD);
    tk_note_on(72u, 127u, 0u);                  /* live pad over all tracks */
    for (lane = 0; lane < 64u; lane++) block(&EXTREME, samples);
    expect(!silent(samples), "four overlapping drum, metal and pad voices produce PCM");
    expect(source_held(64u, TK_SOURCE_SEQUENCE) &&
           source_held(67u, TK_SOURCE_ATMOS) && source_held(72u, 0u),
           "all three loop tracks coexist with live playing");
    tk_all_sound_off(TK_SOURCE_DRUM); /* a drum-track mute also kills one-shot tails */
    block(&EXTREME, NULL);
    expect(!source_held(60u, TK_SOURCE_DRUM) &&
           source_held(64u, TK_SOURCE_SEQUENCE) && source_held(67u, TK_SOURCE_ATMOS) &&
           source_held(72u, 0u),
           "muting drums does not stop melody, atmosphere or live playing");
    tk_note_off(64u, TK_SOURCE_SEQUENCE);
    tk_note_off(67u, TK_SOURCE_ATMOS);
    tk_note_off(72u, 0u);
    for (lane = 0; lane < 2000u && !tk_quiet(); lane++) block(&EXTREME, NULL);
    expect(tk_quiet(), "mixed instruments and one-shot drum tails reach complete silence");
    block(&EXTREME, samples);
    expect(silent(samples), "mixed instrument release leaves no residual audio");

    tk_init();
    tk_note_on(60u, 100u, TK_SOURCE_SEQUENCE);
    block(&NORMAL, NULL);
    expect(tk_voice[source_slot(60u, TK_SOURCE_SEQUENCE)].instrument == TK_INSTRUMENT_PAD,
           "sequencer source remains a PAD when no track program was published");

    /* Each track has its own source and a program is latched when a note
     * starts. Changing the ARP program must not repaint a sounding note. */
    tk_init();
    tk_set_source_instrument(TK_SOURCE_SEQUENCE, TK_INSTRUMENT_METAL);
    tk_set_source_instrument(TK_SOURCE_DRUM, TK_INSTRUMENT_DRUM_KIT);
    tk_set_source_instrument(TK_SOURCE_ATMOS, TK_INSTRUMENT_PAD);
    tk_set_source_instrument(TK_SOURCE_ARP, TK_INSTRUMENT_METAL);
    tk_note_on(60u, 100u, TK_SOURCE_SEQUENCE);
    tk_note_on(62u, 100u, TK_SOURCE_DRUM);
    tk_note_on(64u, 100u, TK_SOURCE_ATMOS);
    tk_note_on(67u, 100u, TK_SOURCE_ARP);
    block(&NORMAL, NULL);
    expect(tk_voice[source_slot(60u, TK_SOURCE_SEQUENCE)].instrument == TK_INSTRUMENT_METAL &&
           tk_voice[source_slot(62u, TK_SOURCE_DRUM)].instrument == TK_INSTRUMENT_DRUM_KIT &&
           tk_voice[source_slot(64u, TK_SOURCE_ATMOS)].instrument == TK_INSTRUMENT_PAD &&
           tk_voice[source_slot(67u, TK_SOURCE_ARP)].instrument == TK_INSTRUMENT_METAL,
           "melody, drums, atmosphere and ARP use separate instrument programs");
    tk_set_source_instrument(TK_SOURCE_ARP, TK_INSTRUMENT_PAD);
    block(&NORMAL, NULL);
    expect(tk_voice[source_slot(67u, TK_SOURCE_ARP)].instrument == TK_INSTRUMENT_METAL,
           "a new ARP program does not retimbre its already sounding note");
    tk_all_sound_off(TK_SOURCE_SEQUENCE);
    tk_all_sound_off(TK_SOURCE_DRUM);
    tk_all_sound_off(TK_SOURCE_ATMOS);
    tk_all_sound_off(TK_SOURCE_ARP);
    block(&NORMAL, NULL);
    expect(tk_quiet(), "stopping all four generated sources leaves no stale note");

    /* Deliberately stress the six-voice output limit with coincident strikes
     * and a maximum-corrosion metal chord. block() checks every PCM sample. */
    for (other = TK_INSTRUMENT_METAL; other <= TK_INSTRUMENT_DRUM_KIT; other++) {
        tk_init();
        tk_set_instrument(other);
        for (lane = 0; lane < TK_VOICES; lane++) tk_note_on(60u + lane, 127u, lane);
        for (lane = 0; lane < 64u; lane++) block(&EXTREME, NULL);
        tk_panic();
        block(&EXTREME, NULL);
        expect(tk_quiet(), "extreme six-voice instruments stop promptly on panic");
    }
}

static void source_priority(void)
{
    uint32_t i;
    tk_init();
    expect(!tk_note_on(128u, 100u, 0u) &&
           !tk_note_on(60u, 100u, TK_SOURCES),
           "invalid pitches and sources report a dropped note-on");
    for (i = 0; i < TK_VOICES; i++)
        expect(tk_note_on(60u + i, 100u, i), "six live notes fill the voice pool");
    expect(tk_live_voice_count() == TK_VOICES,
           "live voice count includes every held panel/MIDI note");
    expect(!tk_note_on(72u, 100u, TK_SOURCE_DRUM),
           "a generated drum drops when all voices are held by live players");
    for (i = 0; i < TK_VOICES; i++)
        expect(source_held(60u + i, i), "a dropped drum cannot steal a held live note");

    tk_init();
    tk_note_on(60u, 100u, 0u); tk_note_on(62u, 100u, 1u);
    tk_note_on(64u, 100u, TK_SOURCE_DRUM);
    tk_note_on(65u, 100u, TK_SOURCE_SEQUENCE);
    tk_note_on(67u, 100u, TK_SOURCE_ATMOS);
    tk_note_on(69u, 100u, TK_SOURCE_ARP);
    expect(tk_live_voice_count() == 2u, "live count excludes generated loop notes");
    expect(tk_note_on(72u, 100u, 0u),
           "new live key can take a generated voice when the pool is full");
    expect(source_held(60u, 0u) && source_held(62u, 1u) && source_held(72u, 0u),
           "live note-on preserves earlier live notes while taking a loop slot");
    expect(!source_held(64u, TK_SOURCE_DRUM) && tk_live_voice_count() == 3u,
           "the oldest generated note yields to the live key");
    expect(tk_note_on(74u, 100u, TK_SOURCE_DRUM),
           "new generated note may replace another generated voice");
    expect(source_held(60u, 0u) && source_held(62u, 1u) && source_held(72u, 0u),
           "generated replacement still protects all live notes");

    tk_init();
    tk_note_on(60u, 100u, 0u);
    block(&NORMAL, NULL);
    tk_note_off(60u, 0u);
    for (i = 1u; i < TK_VOICES; i++) tk_note_on(60u + i, 100u, i);
    expect(tk_live_voice_count() == TK_VOICES - 1u,
           "released live tail no longer counts as a held live voice");
    expect(tk_note_on(72u, 100u, TK_SOURCE_DRUM),
           "a generated note can reuse a released live tail");
    for (i = 1u; i < TK_VOICES; i++)
        expect(source_held(60u + i, i), "tail reuse leaves held live notes intact");
}

/* Ghost Mask is an audio-owned interaction between an existing melodic voice
 * and an existing ATMOS voice. Matched renders keep the same voice allocation,
 * phases and live-note PCM, so a changed hash isolates the atmosphere duck. */
static uint32_t ghost_scene_hash(uint32_t amount, uint32_t source,
                                 uint32_t pitch, uint32_t instrument)
{
    uint32_t i, j, hash = 2166136261u;
    int32_t samples[FRAMES * 2u];
    tk_init();
    /* UINT32_MAX leaves the power-on default untouched for bypass identity. */
    if (amount != UINT32_MAX) tk_set_ghost_mask(amount);
    tk_set_source_patch(TK_SOURCE_ATMOS, &NORMAL, 64u, 64u);
    tk_note_on(60u, 112u, TK_SOURCE_ATMOS);
    for (i = 0; i < 24u; i++) block(&NORMAL, NULL);
    if (source < TK_SOURCE_SEQUENCE) tk_set_instrument(instrument);
    else tk_set_source_instrument(source, instrument);
    tk_note_on(pitch, 112u, source);
    for (i = 0; i < 32u; i++) {
        block(&NORMAL, samples);
        for (j = 0; j < FRAMES * 2u; j++) {
            hash ^= (uint32_t)samples[j];
            hash *= 16777619u;
        }
    }
    return hash;
}

static uint32_t ghost_target(uint32_t source, uint32_t pitch,
                             uint32_t instrument)
{
    uint32_t i;
    tk_init();
    tk_set_ghost_mask(127u);
    tk_set_source_patch(TK_SOURCE_ATMOS, &NORMAL, 64u, 64u);
    tk_note_on(60u, 112u, TK_SOURCE_ATMOS);
    for (i = 0; i < 24u; i++) block(&NORMAL, NULL);
    if (source < TK_SOURCE_SEQUENCE) tk_set_instrument(instrument);
    else tk_set_source_instrument(source, instrument);
    tk_note_on(pitch, 112u, source);
    block(&NORMAL, NULL);
    return (uint32_t)tk_voice[source_slot(60u, TK_SOURCE_ATMOS)].ghost_target_q15;
}

static void ghost_mask(void)
{
    uint32_t i, slot, first, second, peak, after_first, after_second;
    uint32_t dry, wet;
    int32_t samples[FRAMES * 2u];

    dry = ghost_scene_hash(0u, 0u, 60u, TK_INSTRUMENT_PAD);
    expect(dry == ghost_scene_hash(UINT32_MAX, 0u, 60u, TK_INSTRUMENT_PAD),
           "explicit zero Ghost Mask is PCM-identical to the default bypass");
    wet = ghost_scene_hash(127u, 0u, 60u, TK_INSTRUMENT_PAD);
    expect(wet != dry, "a nearby live note audibly ducks an ATMOS note");
    expect(ghost_scene_hash(127u, 1u, 60u, TK_INSTRUMENT_PAD) !=
           ghost_scene_hash(0u, 1u, 60u, TK_INSTRUMENT_PAD),
           "incoming MIDI melody triggers the atmosphere interaction");
    expect(ghost_scene_hash(127u, TK_SOURCE_SEQUENCE, 60u, TK_INSTRUMENT_PAD) !=
           ghost_scene_hash(0u, TK_SOURCE_SEQUENCE, 60u, TK_INSTRUMENT_PAD),
           "a recorded melody note also ducks its nearby atmosphere");
    expect(ghost_scene_hash(127u, 0u, 63u, TK_INSTRUMENT_PAD) ==
           ghost_scene_hash(0u, 0u, 63u, TK_INSTRUMENT_PAD),
           "a live note three semitones away leaves the atmosphere PCM unchanged");
    expect(ghost_scene_hash(127u, TK_SOURCE_DRUM, 60u, TK_INSTRUMENT_DRUM_KIT) ==
           ghost_scene_hash(0u, TK_SOURCE_DRUM, 60u, TK_INSTRUMENT_DRUM_KIT),
           "a generated drum never masks the atmosphere");
    expect(ghost_scene_hash(127u, 0u, 60u, TK_INSTRUMENT_DRUM_KIT) ==
           ghost_scene_hash(0u, 0u, 60u, TK_INSTRUMENT_DRUM_KIT),
           "a live kit strike never masquerades as a melodic trigger");
    expect(ghost_scene_hash(127u, TK_SOURCE_ARP, 60u, TK_INSTRUMENT_PAD) ==
           ghost_scene_hash(0u, TK_SOURCE_ARP, 60u, TK_INSTRUMENT_PAD),
           "ARP pulses do not trigger the recorded-melody atmosphere mask");

    first = ghost_target(0u, 60u, TK_INSTRUMENT_PAD);
    second = ghost_target(0u, 62u, TK_INSTRUMENT_PAD);
    expect(first < second && second < 32768u,
           "same-pitch atmosphere duck is stronger than the two-semitone edge");
    expect(ghost_target(0u, 63u, TK_INSTRUMENT_PAD) == 32768u,
           "outside the two-semitone mask range, atmosphere target is unity");

    tk_init();
    tk_set_ghost_mask(127u);
    tk_set_source_patch(TK_SOURCE_ATMOS, &NORMAL, 64u, 64u);
    tk_note_on(60u, 112u, TK_SOURCE_ATMOS);
    for (i = 0; i < 24u; i++) block(&NORMAL, NULL);
    slot = source_slot(60u, TK_SOURCE_ATMOS);
    expect((uint32_t)tk_voice[slot].ghost_gain_q15 == 32768u,
           "an unmasked atmosphere begins at unity gain");
    tk_note_on(60u, 112u, 0u);
    block(&NORMAL, samples);
    first = (uint32_t)tk_voice[slot].ghost_gain_q15;
    expect(first < 32768u && first > (uint32_t)tk_voice[slot].ghost_target_q15,
           "mask onset moves toward its target without an instantaneous gain jump");
    for (i = 0; i < 8u; i++) block(&NORMAL, samples);
    peak = (uint32_t)tk_voice[slot].ghost_gain_q15;
    expect(peak < first, "nearby melody continues to lower ATMOS over later samples");
    tk_note_off(60u, 0u);
    for (i = 0; i < 64u && (uint32_t)tk_voice[slot].ghost_target_q15 != 32768u; i++)
        block(&NORMAL, samples);
    expect((uint32_t)tk_voice[slot].ghost_target_q15 == 32768u,
           "the brief mask pulse ends even if the atmosphere remains held");
    first = (uint32_t)tk_voice[slot].ghost_gain_q15;
    for (i = 0; i < 16u; i++) block(&NORMAL, samples);
    expect((uint32_t)tk_voice[slot].ghost_gain_q15 > first,
           "ATMOS swells back after the melody pulse ends");
    for (i = 0; i < 96u; i++) block(&NORMAL, samples);
    expect((uint32_t)tk_voice[slot].ghost_gain_q15 == 32768u,
           "a finished mask returns ATMOS exactly to its original gain");
    expect(source_held(60u, TK_SOURCE_ATMOS),
           "mask recovery does not cut the sustained atmosphere note");

    tk_init();
    tk_set_ghost_mask(127u);
    tk_set_source_patch(TK_SOURCE_ATMOS, &NORMAL, 64u, 64u);
    tk_note_on(60u, 112u, TK_SOURCE_ATMOS);
    for (i = 0; i < 24u; i++) block(&NORMAL, NULL);
    slot = source_slot(60u, TK_SOURCE_ATMOS);
    tk_note_on(60u, 112u, 0u);
    for (i = 0; i < 16u; i++) block(&NORMAL, NULL); /* about half a pulse */
    tk_note_on(61u, 112u, 1u);
    for (i = 0; i < 20u; i++) block(&NORMAL, NULL); /* first pulse has ended */
    after_first = (uint32_t)tk_voice[slot].ghost_target_q15;
    expect(after_first < 32768u,
           "a second nearby melody pulse keeps the atmosphere masked");
    for (i = 0; i < 36u; i++) block(&NORMAL, NULL);
    after_second = (uint32_t)tk_voice[slot].ghost_target_q15;
    expect(after_second == 32768u,
           "overlapping melody pulses release after the later pulse ends");

    tk_init();
    tk_set_ghost_mask(127u);
    tk_set_source_patch(TK_SOURCE_ATMOS, &NORMAL, 64u, 64u);
    expect(tk_note_on(60u, 112u, TK_SOURCE_ATMOS), "atmosphere takes one voice");
    for (i = 0; i < TK_VOICES - 1u; i++)
        expect(tk_note_on(58u + i, 112u, i), "five live keys fit beside ATMOS");
    for (i = 0; i < 64u; i++) block(&EXTREME, samples);
    expect(source_held(60u, TK_SOURCE_ATMOS),
           "Ghost Mask does not allocate a seventh voice or steal ATMOS");
    for (i = 0; i < TK_VOICES - 1u; i++)
        expect(source_held(58u + i, i),
               "Ghost Mask does not steal any of the five held live keys");
    expect(tk_live_voice_count() == TK_VOICES - 1u,
           "Ghost Mask leaves the six-voice live/track budget unchanged");
}

int main(void)
{
    uint32_t first, second;
    idle_and_panic(); source_isolation(); sustain_controls(); channel_mode_controls();
    sequence_source_controls(); bend_controls(); envelope_controls(); timbre_history(); afterimage_audio();
    extreme_chords(); release(); canceled_retrigger(); additive_render(); clean_fracture();
    new_instruments();
    source_priority();
    ghost_mask();
    first = deterministic_render(); second = deterministic_render();
    expect(first == second, "identical events after reset render identical PCM");
    expect(first == 0xC738051Du, "centered bend and zero memory retain the legacy pad PCM exactly");
    printf("PASS: %u DSP checks; deterministic render %08X\n", checks, first);
    return 0;
}
