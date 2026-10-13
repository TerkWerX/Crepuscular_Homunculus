/* SPDX-License-Identifier: GPL-3.0-only */
/* External performance controllers: portable DSP tests, no hardware install. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "terkinstein_engine.h"

#define FRAMES 256u
static uint32_t checks;
static const tk_params_t NORMAL = {38u, 46u, 0u, 76u};
static const tk_params_t CLEAN = {38u, 0u, 0u, 76u};
static void expect(int condition, const char *description)
{
    ++checks;
    if (!condition) { fprintf(stderr, "FAIL: %s\n", description); exit(1); }
}
static void render(uint32_t count, const tk_params_t *p)
{
    uint32_t block, i;
    int32_t pcm[FRAMES * 2u + 2u];
    for (block = 0; block < count; ++block) {
        memset(pcm, 0, sizeof(pcm));
        pcm[0] = pcm[FRAMES * 2u + 1u] = 0x12345678;
        tk_render(pcm + 1, FRAMES, p);
        expect(pcm[0] == 0x12345678 && pcm[FRAMES * 2u + 1u] == 0x12345678,
               "controller render respects buffer guards");
        for (i = 1u; i <= FRAMES * 2u; ++i)
            expect(pcm[i] >= -24384 && pcm[i] <= 24384,
                   "expressive six-voice PCM retains headroom");
    }
}
static tk_voice_t *voice(uint32_t note, uint32_t source)
{
    uint32_t i;
    for (i = 0; i < TK_VOICES; ++i) if (tk_voice[i].source == source &&
        tk_voice[i].audio_note == note && (tk_voice[i].gate || tk_voice[i].env)) return &tk_voice[i];
    expect(0, "expected sounding note exists");
    return &tk_voice[0];
}
static void defaults_and_sources(void)
{
    uint32_t i;
    for (i = 0u; i < 1024u; ++i) {
        uint32_t expected = ((i + 1u) & 1023u) << 22;
        expect(tk_revision(i << 22) == expected &&
               tk_revision((i << 22) | TK_PAYLOAD) == expected,
               "bounded revision packing and wrap ignore every payload bit");
    }
    tk_init();
    expect(TK_SOURCES == 40u && TK_SOURCE_SEQUENCE == 33u && TK_SOURCE_BASS == 37u,
           "both 16-channel MIDI ports precede protected generated sources");
    for (i = 0; i < TK_SOURCES; ++i) {
        expect(tk_source_performance[i] == TK_PERFORMANCE_DEFAULT &&
               tk_source_timbre[i] == (64u | (64u << 7)) &&
               tk_source_bend_range[i] == 2u && tk_gain_target_q15[i] == 32768,
               "every source starts with exact neutral performance values");
        tk_note_on(60u, 110u, i);
        expect(tk_note_is_held(60u, i), "six-bit command retains all source identities");
        tk_note_off(60u, i);
        expect(!tk_note_is_held(60u, i), "matching source note-off releases before render");
    }
    tk_init();
    for (i = 17u; i < 23u; ++i) tk_note_on(60u + i, 100u, i);
    expect(tk_live_voice_count() == TK_VOICES, "TRS port notes count as protected live voices");
    expect(!tk_note_on(40u, 100u, TK_SOURCE_SEQUENCE), "sequence cannot steal six held TRS notes");
    tk_note_off(77u, 1u);
    expect(tk_note_is_held(77u, 17u), "USB channel1 cannot release TRS channel1");
    tk_note_off(77u, 17u);
    expect(tk_note_on(40u, 100u, TK_SOURCE_SEQUENCE), "sequence can borrow a released TRS slot");
    expect(tk_live_voice_count() == 5u, "generated slot remains separate from live count");
}
static void bend_ranges(void)
{
    uint32_t range, value, previous;
    for (range = 0u; range <= 24u; ++range) {
        previous = 0u;
        for (value = 0u; value <= 16383u; ++value) {
            uint32_t factor = tk_bend_scale_range(value, range);
            expect(factor >= 4194304u && factor <= 67108864u && factor >= previous,
                   "all bend ranges and14-bit positions are bounded and monotonic");
            if (range == 2u) expect(factor == tk_bend_scale(value), "default2 retains legacy interpolation exactly");
            previous = factor;
        }
        expect(tk_bend_scale_range(TK_BEND_CENTER, range) == TK_PITCH_ONE,
               "every bend range has exact center unity");
    }
    expect(tk_bend_scale_range(0u, 24u) == TK_PITCH_ONE / 4u &&
           tk_bend_scale_range(16383u, 24u) == TK_PITCH_ONE * 4u,
           "24-semitone endpoints are exact two-octave factors");
    tk_init();
    tk_note_on(60u, 100u, 1u); tk_note_on(60u, 100u, 17u);
    tk_note_on(60u, 100u, 16u); tk_note_on(60u, 100u, 32u);
    render(30u, &NORMAL);
    tk_set_bend_range(17u, 24u); tk_pitch_bend(17u, 16383u);
    tk_set_bend_range(32u, 0u); tk_pitch_bend(32u, 0u);
    render(40u, &NORMAL);
    expect(tk_bend_current[17u] == TK_PITCH_ONE * 4u, "TRS bend reaches configured range");
    expect(tk_bend_current[1u] == TK_PITCH_ONE && tk_bend_current[16u] == TK_PITCH_ONE &&
           tk_bend_current[32u] == TK_PITCH_ONE, "range and bend do not leak across ports/channels");
    tk_set_bend_range(17u, UINT32_MAX); tk_pitch_bend(17u, UINT32_MAX);
    expect(tk_source_bend_range[17u] == 24u && (tk_source_control[17u] & TK_BEND_MASK) == 16383u,
           "excessive range and bend values clamp");
    tk_panic(); render(1u, &NORMAL);
    tk_set_bend_range(17u, 24u); tk_pitch_bend(17u, 16383u);
    tk_note_on(127u, 127u, 17u); render(40u, &NORMAL);
    expect(voice(127u, 17u)->inc_a > 2000000000u && voice(127u, 17u)->inc_b > 2000000000u,
           "extreme upper bend saturates high phase rate instead of wrapping low");
}
static void gain_and_modulation(void)
{
    tk_voice_t *a, *b;
    int32_t samples[FRAMES * 2u];
    uint32_t i, neutral, low = UINT32_MAX, high = 0u;
    tk_init();
    tk_note_on(60u, 127u, 1u); tk_note_on(60u, 127u, 17u);
    render(50u, &NORMAL);
    a = voice(60u, 1u); b = voice(60u, 17u);
    tk_expression(17u, 0u); render(20u, &NORMAL);
    expect(b->performance_gain_q15 == 0 && a->performance_gain_q15 == 32768,
           "expression reaches exact silence on its source only");
    tk_expression(17u, 127u); tk_volume(17u, 64u); render(20u, &NORMAL);
    expect(b->performance_gain_q15 > 16000 && b->performance_gain_q15 < 17000,
           "volume reaches half level smoothly without gain boost");
    tk_volume(17u, 127u); render(20u, &NORMAL);
    neutral = a->inc_a;
    tk_modulation(17u, 127u);
    for (i = 0u; i < 50u; ++i) {
        render(1u, &NORMAL);
        if (b->inc_a < low) low = b->inc_a;
        if (b->inc_a > high) high = b->inc_a;
        expect(a->inc_a == neutral, "TRS modwheel does not modulate matching USB channel");
    }
    expect(low < neutral && high > neutral && high - low < neutral / 40u,
           "modwheel produces subtle bounded vibrato in both directions");
    tk_modulation(17u, 0u); render(40u, &NORMAL);
    expect(b->inc_a == neutral && tk_modulation_q8[17u] == 0, "modulation zero returns exactly to neutral pitch");
    tk_note_off(60u, 1u); tk_all_sound_off(1u); render(1u, &NORMAL);
    tk_expression(17u, 0u); render(20u, &NORMAL);
    memset(samples, 0, sizeof(samples)); tk_render(samples, FRAMES, &NORMAL);
    for (i = 0u; i < FRAMES * 2u; ++i) expect(samples[i] == 0, "zero expression silences held PCM exactly");
}
static void pressure_and_colour(void)
{
    tk_voice_t *a, *b, *c;
    int32_t old_alpha, old_drive;
    int32_t macros[4];
    tk_memory_state_t history;
    tk_init();
    tk_note_on(60u, 127u, 1u); tk_note_on(64u, 127u, 1u); tk_note_on(60u, 127u, 17u);
    render(50u, &NORMAL);
    a = voice(60u, 1u); b = voice(64u, 1u); c = voice(60u, 17u);
    old_alpha = a->performance_alpha; old_drive = a->performance_drive;
    memcpy(macros, tk_macro, sizeof(macros)); history = tk_history;
    tk_poly_pressure(60u, 1u, 127u); render(30u, &NORMAL);
    expect(a->performance_alpha > old_alpha && a->performance_drive > old_drive,
           "poly pressure brightens and corrodes its exact note");
    expect(b->performance_alpha == old_alpha && c->performance_alpha == old_alpha,
           "poly pressure leaves other pitch and same pitch on other port unchanged");
    expect(!memcmp(macros, tk_macro, sizeof(macros)) && !memcmp(&history, &tk_history, sizeof(history)),
           "performance pressure does not edit macros or stored afterimages");
    tk_channel_pressure(1u, 127u); render(30u, &NORMAL);
    expect(b->performance_alpha == a->performance_alpha && c->performance_alpha == old_alpha,
           "channel pressure reaches own chord without cross-port leakage");
    tk_brightness(17u, 0u); tk_texture(17u, 127u); render(30u, &NORMAL);
    expect(c->performance_alpha < old_alpha && c->performance_drive > old_drive,
           "brightness and texture shape source independently of live knobs");
    tk_set_fx(0u); render(40u, &CLEAN);
    expect(!a->performance_fracture && !b->performance_fracture && !c->performance_fracture,
           "pressure and texture cannot restore corrosion to a clean/FX-bypassed patch");
    tk_soft_pedal(17u, 127u); render(30u, &CLEAN);
    expect(c->performance_gain_q15 < 25000 && c->performance_alpha <= c->color_alpha &&
           a->performance_gain_q15 == 32768, "soft pedal smoothly attenuates/darkens only own pitched source");
    tk_init(); tk_set_instrument(TK_INSTRUMENT_DRUM_KIT);
    { tk_params_t kit = {64u, 64u, 64u, 64u}; render(30u, &kit);
      tk_note_on(60u, 127u, 17u); tk_channel_pressure(17u, 127u); tk_texture(17u, 127u);
      render(1u, &kit); }
    expect(voice(60u, 17u)->forge_bite_q8 == 0, "neutral kit remains free of added pressure grit");
}
static void pedals_and_reset(void)
{
    uint32_t i;
    tk_init();
    tk_note_on(60u, 100u, 17u); tk_note_on(60u, 100u, 1u);
    tk_sostenuto(17u, 127u);
    tk_note_on(64u, 100u, 17u);
    tk_sostenuto(17u, 127u);       /* repeated pedal-down must not recapture */
    tk_note_off(60u, 17u); tk_note_off(64u, 17u); tk_note_off(60u, 1u);
    expect(tk_note_is_held(60u, 17u) && !tk_note_is_held(64u, 17u) && !tk_note_is_held(60u, 1u),
           "sostenuto captures rising-edge held keys only and is source isolated");
    tk_sustain(17u, 127u); tk_sostenuto(17u, 0u);
    expect(tk_note_is_held(60u, 17u), "sustain retains a captured note after sostenuto lift");
    tk_sustain(17u, 0u);
    expect(!tk_note_is_held(60u, 17u), "last holding pedal releases the key");
    tk_init();
    tk_sustain(17u, 127u); tk_note_on(60u, 100u, 17u); tk_note_off(60u, 17u);
    tk_sostenuto(17u, 127u); tk_sustain(17u, 0u);
    expect(!tk_note_is_held(60u, 17u), "sostenuto does not capture keys already released under sustain");
    tk_note_on(62u, 100u, 17u); tk_sostenuto(17u, 0u); tk_sostenuto(17u, 127u);
    tk_note_off(62u, 17u); tk_all_notes_off(17u);
    expect(tk_note_is_held(62u, 17u), "all notes off respects captured sostenuto");
    tk_volume(17u, 37u); tk_expression(17u, 0u); tk_modulation(17u, 127u);
    tk_channel_pressure(17u, 127u); tk_poly_pressure(62u, 17u, 127u); tk_soft_pedal(17u, 127u);
    tk_brightness(17u, 96u); tk_texture(17u, 32u); tk_set_bend_range(17u, 12u); tk_pitch_bend(17u, 0u);
    tk_reset_controllers(17u);
    expect(!tk_note_is_held(62u, 17u), "CC121 releases notes retained by either pedal");
    expect(tk_source_performance[17u] == (37u | (127u << 7)) &&
           tk_source_timbre[17u] == (96u | (32u << 7)) && tk_source_bend_range[17u] == 12u &&
           tk_source_control[17u] % TK_CONTROLLER_REV == TK_BEND_CENTER,
           "CC121 implements RP-015 preserved volume/sound controls/RPN with neutral performance");
    tk_panic(); render(1u, &NORMAL);
    for (i = 0u; i < TK_SOURCES; ++i) expect(tk_source_performance[i] == TK_PERFORMANCE_DEFAULT &&
        tk_source_timbre[i] == (64u | (64u << 7)) && tk_source_bend_range[i] == 2u,
        "panic resets complete performance state on every source");
    expect(tk_quiet(), "panic fades all controller-held notes in one block");
    tk_note_on(60u, 100u, 17u); tk_sostenuto(17u, 127u); tk_note_off(60u, 17u);
    tk_note_on(60u, 100u, 17u); tk_note_off(60u, 17u);
    expect(!tk_note_is_held(60u, 17u), "a retrigger cannot inherit an older sostenuto capture");
}
static void history_and_invalid_input(void)
{
    uint32_t i;
    tk_memory_state_t state;
    int32_t shade = 1, fracture = 2;
    tk_memory_init(&state); tk_memory_remember(&state, 60u, 17u, 8000, 9000);
    expect(tk_memory_pick(&state, 62u, 17u, &shade, &fracture) && shade == 8000 && fracture == 9000,
           "TRS channel histories are now accepted");
    expect(!tk_memory_pick(&state, 62u, 1u, &shade, &fracture), "same channel on USB does not borrow TRS history");
    tk_memory_remember(&state, 61u, 32u, 7000, 6000);
    expect(tk_memory_pick(&state, 62u, 32u, &shade, &fracture), "TRS channel16 supports afterimages");
    tk_memory_remember(&state, 62u, 99u, 1, 1);
    expect(state.count == 2u && !tk_memory_pick(&state, 62u, 99u, &shade, &fracture),
           "invalid history source is rejected");
    tk_init();
    tk_volume(TK_SOURCES, 0u); tk_expression(TK_SOURCES, 0u); tk_modulation(TK_SOURCES, 127u);
    tk_channel_pressure(TK_SOURCES, 127u); tk_poly_pressure(60u, TK_SOURCES, 127u);
    tk_sostenuto(TK_SOURCES, 127u); tk_soft_pedal(TK_SOURCES, 127u);
    tk_brightness(TK_SOURCES, 0u); tk_texture(TK_SOURCES, 127u); tk_set_bend_range(TK_SOURCES, 24u);
    for (i = 0u; i < TK_SOURCES; ++i) expect(tk_source_performance[i] == TK_PERFORMANCE_DEFAULT &&
        tk_source_control[i] == TK_BEND_CENTER && tk_source_bend_range[i] == 2u,
        "invalid controller source never aliases a valid mailbox");
}
int main(void)
{
    defaults_and_sources(); bend_ranges(); gain_and_modulation(); pressure_and_colour();
    pedals_and_reset(); history_and_invalid_input();
    printf("PASS: %lu external controller checks\n", (unsigned long)checks);
    return 0;
}
