/* SPDX-License-Identifier: GPL-3.0-only */
/* SCRAP FORGE regression checks; no hardware, allocation or flashing. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "terkinstein_engine.h"

#define FORGE_FRAMES 256u
static const tk_params_t NEUTRAL = {64u, 64u, 64u, 64u};
static uint32_t checks;
static const uint32_t LEGACY_KIT[12] = {
    0x485A331Cu, 0x8E08E97Fu, 0xFE597BFBu, 0x063CB1EEu,
    0x397EECB8u, 0xDB8F8A38u, 0x64788FB2u, 0xE9540FFDu,
    0x25B11F1Bu, 0x4704F869u, 0x4A50D275u, 0xCA11CF06u
}; /* Captured from unmodified 0.10 engine before SCRAP FORGE was added. */

static void expect(int condition, const char *message)
{
    ++checks;
    if (!condition) { fprintf(stderr, "FAIL: %s\n", message); exit(1); }
}

static void block(const tk_params_t *params, int32_t *samples)
{
    int32_t guarded[FORGE_FRAMES * 2u + 2u];
    uint32_t i;
    guarded[0] = guarded[FORGE_FRAMES * 2u + 1u] = 0x13579BDF;
    memset(guarded + 1, 0, FORGE_FRAMES * 2u * sizeof(int32_t));
    tk_render(guarded + 1, FORGE_FRAMES, params);
    expect(guarded[0] == 0x13579BDF && guarded[FORGE_FRAMES * 2u + 1u] == 0x13579BDF,
           "forge render stays inside its buffer");
    for (i = 1u; i <= FORGE_FRAMES * 2u; ++i)
        expect(guarded[i] >= -(int32_t)(TK_VOICES * TK_VOICE_PCM_BOUND) &&
               guarded[i] <= (int32_t)(TK_VOICES * TK_VOICE_PCM_BOUND), "forge keeps bounded eight-voice headroom");
    if (samples) memcpy(samples, guarded + 1, FORGE_FRAMES * 2u * sizeof(int32_t));
}

static void settle(const tk_params_t *params)
{
    uint32_t i;
    for (i = 0u; i < 100u; ++i) block(params, NULL);
}

static uint32_t fixture_hash(uint32_t lane, const tk_params_t *params,
                             uint32_t source, const tk_params_t *live,
                             uint32_t change_live, uint32_t change_patch)
{
    uint32_t i, j, hash = 2166136261u;
    int32_t samples[FORGE_FRAMES * 2u];
    tk_init(); tk_set_instrument(TK_INSTRUMENT_DRUM_KIT);
    if (source >= TK_SOURCE_SEQUENCE) {
        tk_set_source_instrument(source, TK_INSTRUMENT_DRUM_KIT);
        tk_set_source_patch(source, params, 64u, 64u);
    }
    settle(live);
    expect(tk_note_on(60u + lane, 127u, source), "kit note allocated");
    for (i = 0u; i < 64u; ++i) {
        static const tk_params_t changed = {127u, 0u, 127u, 0u};
        if (i == 4u && change_patch) {
            tk_set_source_patch(source, &changed, 0u, 127u);
            tk_set_source_instrument(source, TK_INSTRUMENT_PAD);
        }
        if (i == 4u && change_live) {
            tk_set_instrument(TK_INSTRUMENT_METAL);
            tk_set_envelope(0u, 0u);
            tk_set_memory(127u); tk_set_ghost_mask(127u);
            tk_set_source_shade(source, 127u);
        }
        block(change_live && i >= 4u ? &changed : live, samples);
        for (j = 0u; j < FORGE_FRAMES * 2u; ++j) {
            uint32_t value = (uint32_t)samples[j];
            hash = (hash ^ (value & 255u)) * 16777619u;
            hash = (hash ^ ((value >> 8) & 255u)) * 16777619u;
            hash = (hash ^ ((value >> 16) & 255u)) * 16777619u;
            hash = (hash ^ (value >> 24)) * 16777619u;
        }
    }
    return hash;
}

static uint32_t lane_hash(uint32_t lane, const tk_params_t *params, uint32_t source)
{
    return fixture_hash(lane, params, source, params, 0u, 0u);
}

static void legacy_and_roles(void)
{
    uint32_t lane, macro, side;
    for (lane = 0u; lane < 12u; ++lane) {
        expect(lane_hash(lane, &NEUTRAL, 0u) == LEGACY_KIT[lane],
               "all-centre kit is exact pre-forge PCM for every role");
        for (macro = 0u; macro < 4u; ++macro) for (side = 0u; side < 2u; ++side) {
            tk_params_t changed = NEUTRAL;
            uint8_t value = side ? 127u : 0u;
            if (macro == 0u) changed.shade = value;
            if (macro == 1u) changed.fracture = value;
            if (macro == 2u) changed.drift = value;
            if (macro == 3u) changed.bloom = value;
            expect(lane_hash(lane, &changed, 0u) != LEGACY_KIT[lane],
                   "each forge knob changes all twelve roles in both directions");
        }
    }
}

static uint32_t tail_blocks(uint32_t lane, uint32_t decay)
{
    tk_params_t params = NEUTRAL;
    uint32_t duration = 0u;
    params.drift = (uint8_t)decay;
    tk_init(); tk_set_instrument(TK_INSTRUMENT_DRUM_KIT); settle(&params);
    tk_note_on(60u + lane, 127u, 0u); block(&params, NULL);
    tk_note_off(60u + lane, 0u);
    while (!tk_quiet() && duration < 300u) { block(&params, NULL); ++duration; }
    expect(tk_quiet(), "even the longest forged hit reaches complete silence");
    return duration;
}

static void finite_decay(void)
{
    uint32_t lane;
    for (lane = 0u; lane < 12u; ++lane) {
        uint32_t short_tail = tail_blocks(lane, 0u);
        uint32_t neutral_tail = tail_blocks(lane, 64u);
        uint32_t long_tail = tail_blocks(lane, 127u);
        expect(short_tail < neutral_tail && neutral_tail < long_tail,
               "DECAY orders short, original and extended one-shot tails");
    }
}

static void frozen_patches(void)
{
    static const tk_params_t captured = {31u, 103u, 109u, 96u};
    static const tk_params_t live_other = {127u, 0u, 0u, 127u};
    uint32_t lane, source;
    for (source = TK_SOURCE_SEQUENCE; source < TK_SOURCES; ++source) {
        for (lane = 0u; lane < 12u; ++lane) {
            uint32_t a = fixture_hash(lane, &captured, source, &NEUTRAL, 0u, 0u);
            uint32_t b = fixture_hash(lane, &captured, source, &live_other, 1u, 1u);
            expect(a == b, "each frozen source keeps captured kit coefficients through live edits");
        }
    }
}

static uint32_t unpatched_hash(uint32_t source, uint32_t change)
{
    static const tk_params_t changed = {0u, 127u, 127u, 127u};
    uint32_t i, j, hash = 2166136261u;
    int32_t samples[FORGE_FRAMES * 2u];
    tk_init(); tk_set_instrument(TK_INSTRUMENT_DRUM_KIT);
    tk_set_source_instrument(source, TK_INSTRUMENT_DRUM_KIT);
    settle(&NEUTRAL); tk_note_on(60u, 127u, source);
    for (i = 0u; i < 64u; ++i) {
        block(change && i >= 1u ? &changed : &NEUTRAL, samples);
        for (j = 0u; j < FORGE_FRAMES * 2u; ++j) hash = (hash ^ (uint32_t)samples[j]) * 16777619u;
    }
    return hash;
}

static void unpatched_source_compatibility(void)
{
    uint32_t source;
    for (source = TK_SOURCE_SEQUENCE; source < TK_SOURCES; ++source) {
        uint32_t unchanged = unpatched_hash(source, 0u);
        uint32_t changed = unpatched_hash(source, 1u);
        expect(source == TK_SOURCE_ARP ? changed != unchanged : changed == unchanged,
               "uncaptured ARP follows live sculpting; uncaptured sequencer KIT retains legacy neutral audio");
    }
}

static void live_slew_and_fx(void)
{
    static const tk_params_t hot = {0u, 127u, 127u, 127u};
    static const tk_params_t bypassed = {0u, 127u, 127u, 64u};
    int32_t samples[FORGE_FRAMES * 2u];
    uint32_t lane, i;
    for (lane = 0u; lane < 12u; ++lane) {
        uint32_t hot_hash = lane_hash(lane, &hot, 0u);
        uint32_t clean_hash = lane_hash(lane, &bypassed, 0u);
        expect(hot_hash != clean_hash, "FX bypass removes BITE corrosion from each role");
        expect(clean_hash != LEGACY_KIT[lane], "FX bypass retains BODY/WIRE/DECAY sculpting");
    }
    tk_init(); tk_set_instrument(TK_INSTRUMENT_DRUM_KIT); settle(&NEUTRAL);
    tk_note_on(61u, 127u, 0u); block(&NEUTRAL, NULL);
    {
        uint32_t phase = tk_control_phase;
        int32_t old_wire = tk_voice[0].forge_wire_q8;
        uint32_t old_decay = tk_voice[0].forge_decay_q8;
        memset(samples, 0, sizeof(samples)); tk_render(samples, 32u, &hot);
        expect(tk_control_phase == phase + 32u, "live edit keeps the existing control clock");
        expect(tk_voice[0].forge_wire_q8 > old_wire && tk_voice[0].forge_wire_q8 < 16128,
               "live WIRE approaches its target without an instantaneous coefficient jump");
        expect(tk_voice[0].forge_decay_q8 < old_decay && tk_voice[0].forge_decay_q8 > 64u,
               "live DECAY reshapes an existing sounding tail at control rate");
        expect(tk_voice[0].drum_elapsed == FORGE_FRAMES + 32u,
               "live forge edit does not restart a one-shot");
    }
    for (i = 0u; i < 100u; ++i) block(&bypassed, NULL);
    expect(tk_macro[3] == 16384 && tk_voice[0].forge_bite_q8 == 0,
           "settled live FX bypass forces exactly neutral BITE");
    tk_panic(); block(&bypassed, NULL); block(&bypassed, samples);
    expect(tk_quiet(), "panic clears forged sounding and pending tails");
    for (i = 0u; i < FORGE_FRAMES * 2u; ++i)
        expect(samples[i] == 0, "panic leaves no forged audio residue");
}

static uint32_t live_switch_hash(uint32_t lane, uint32_t switch_mode)
{
    static const tk_params_t captured = {31u, 103u, 109u, 96u};
    static const tk_params_t other = {127u, 0u, 0u, 127u};
    uint32_t i, j, hash = 2166136261u;
    int32_t samples[FORGE_FRAMES * 2u];
    tk_init(); tk_set_instrument(TK_INSTRUMENT_DRUM_KIT); settle(&captured);
    tk_note_on(60u + lane, 127u, 0u);
    for (i = 0u; i < 64u; ++i) {
        if (switch_mode && i == 4u) tk_set_instrument(TK_INSTRUMENT_METAL);
        block(switch_mode && i >= 4u ? &other : &captured, samples);
        for (j = 0u; j < FORGE_FRAMES * 2u; ++j) hash = (hash ^ (uint32_t)samples[j]) * 16777619u;
    }
    return hash;
}

static void preset_tail_and_global_fx(void)
{
    static const tk_params_t hot = {64u, 103u, 127u, 127u};
    static const tk_params_t metal = {31u, 0u, 0u, 127u};
    uint32_t lane, i;
    for (lane = 0u; lane < 12u; ++lane)
        expect(live_switch_hash(lane, 0u) == live_switch_hash(lane, 1u),
               "a departing live KIT tail keeps its forge body after selecting a pitched preset");
    tk_init(); tk_set_instrument(TK_INSTRUMENT_DRUM_KIT); settle(&hot);
    tk_note_on(61u, 127u, 0u); block(&hot, NULL);
    tk_set_fx(0u); tk_set_instrument(TK_INSTRUMENT_METAL);
    block(&metal, NULL);
    expect(tk_voice[0].forge_latched && tk_voice[0].forge_bite_q8 > 0 &&
           tk_voice[0].forge_bite_q8 < 16128,
           "FX bypass slews old KIT corrosion rather than a hard spectral jump");
    for (i = 0u; i < 20u; ++i) block(&metal, NULL);
    expect(tk_voice[0].env > 0 && tk_voice[0].forge_bite_q8 == 0,
           "FX bypass removes BITE even while another preset's BLOOM is high");
    tk_set_fx(1u);
    for (i = 0u; i < 20u; ++i) block(&metal, NULL);
    expect(tk_voice[0].forge_bite_q8 == tk_voice[0].forge_bite_target_q8,
           "restoring FX restores the old KIT tail's own BITE setting");
    /* Global live FX must not override a recorded track's captured FX. */
    tk_init(); tk_set_fx(0u); tk_set_source_instrument(TK_SOURCE_DRUM, TK_INSTRUMENT_DRUM_KIT);
    tk_set_source_patch(TK_SOURCE_DRUM, &hot, 64u, 64u);
    settle(&NEUTRAL); tk_note_on(61u, 127u, TK_SOURCE_DRUM); block(&NEUTRAL, NULL);
    expect(tk_voice[0].forge_bite_q8 == 16128,
           "a frozen KIT uses captured BITE independently of live FX bypass");
}

static void extreme_and_isolation(void)
{
    static const tk_params_t extreme = {255u, 255u, 255u, 255u};
    static const tk_params_t clipped = {127u, 127u, 127u, 127u};
    uint32_t lane, i, v;
    for (lane = 0u; lane < 12u; ++lane) {
        expect(lane_hash(lane, &extreme, 0u) == lane_hash(lane, &clipped, 0u),
               "out-of-range live bytes clamp before coefficient lookup");
        expect(lane_hash(lane, &extreme, TK_SOURCE_DRUM) == lane_hash(lane, &clipped, TK_SOURCE_DRUM),
               "out-of-range captured bytes clamp before coefficient lookup");
    }
    tk_init(); tk_set_instrument(TK_INSTRUMENT_DRUM_KIT); settle(&NEUTRAL);
    tk_set_source_instrument(TK_SOURCE_DRUM, TK_INSTRUMENT_DRUM_KIT);
    tk_set_source_patch(TK_SOURCE_DRUM, &clipped, 64u, 64u);
    tk_note_on(60u, 127u, 0u); tk_note_on(60u, 127u, 1u);
    tk_note_on(60u, 127u, TK_SOURCE_DRUM);
    block(&NEUTRAL, NULL); tk_all_sound_off(1u); block(&NEUTRAL, NULL);
    expect(tk_note_is_held(60u, 0u) && tk_note_is_held(60u, TK_SOURCE_DRUM) &&
           !tk_note_is_held(60u, 1u), "kit CC120 preserves matching panel and captured-source hits");
    tk_panic(); block(&NEUTRAL, NULL);
    expect(tk_quiet(), "kit panic releases all independent owners");
    /* Repeated full-pool mixtures exercise table ends, steals and live changes. */
    tk_init(); tk_set_instrument(TK_INSTRUMENT_DRUM_KIT);
    for (i = 0u; i < 240u; ++i) {
        tk_params_t varied;
        varied.shade = (uint8_t)((i * 17u) & 127u);
        varied.fracture = (uint8_t)((i * 31u) & 127u);
        varied.drift = (uint8_t)((i * 47u) & 127u);
        varied.bloom = (uint8_t)((i * 61u) & 127u);
        for (v = 0u; v < TK_VOICES; ++v) tk_note_on(36u + (i + v) % 12u, 127u, v);
        block(&varied, NULL);
    }
    tk_panic(); block(&NEUTRAL, NULL);
    expect(tk_quiet(), "dense extreme forge mixture remains panic-recoverable");
}

int main(void)
{
    legacy_and_roles(); finite_decay(); frozen_patches();
    unpatched_source_compatibility(); live_slew_and_fx();
    preset_tail_and_global_fx(); extreme_and_isolation();
    printf("SCRAP FORGE: %u guarded checks; all 12 neutral legacy hashes retained\n", checks);
    return 0;
}
