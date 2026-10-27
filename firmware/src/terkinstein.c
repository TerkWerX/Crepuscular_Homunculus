/* SPDX-License-Identifier: GPL-3.0-only */
/* TERKINSTEIN: corroded pads, chord afterimages and a live note transport.
 * Platform derived from Dinghy by
 * zvenson, on Felucca (Leo Kuroshita / Hugelton Instruments) and SLOOP (isod89).
 * The compact chord-afterimage experiment is described in docs/DESIGN.md. */
#define APP_NAME "Crepuscular Homunculus"
#ifndef APP_VER
#define APP_VER "0.26.19"
#endif
#define SETTINGS_MAGIC 0x4E494B54u /* TKIN: never interpret another app's settings */
#define TERKINSTEIN_SCREEN_MIRROR 1
#define TERKINSTEIN_LCD_COOPERATIVE 1
#include <stddef.h>
#include "platform.c"
#include "terkinstein_product_label.h"
#include "terkinstein_engine.h"
#include "terkinstein_transport.h"
#include "terkinstein_midi_clock.h"
#include "terkinstein_section_mutes.h"
#include "terkinstein_lfo.h"
#include "terkinstein_splash.h"
#include "terkinstein_intro.h"
#include "terkinstein_visual.h"
#include "terkinstein_metronome.h"
#include "terkinstein_encoder.h"
#include "terkinstein_home_visual.h"
static tk_drum_mix_t tk_drum_mix[TK_DRUM_LANES];
static uint8_t tk_drum_focus;
static void publish_drum_mix(void)
{
    uint32_t lane;
    for (lane = 0; lane < TK_DRUM_LANES; lane++) tk_set_drum_mix(lane, &tk_drum_mix[lane]);
}
typedef char tk_visual_voice_budget[(TK_VIS_VOICES == TK_VOICES) ? 1 : -1];

/* Main owns a single packed parameter word; audio reads an atomic snapshot.
 * Audio is enabled before app_main(), so it must remain silent until ready. */
static volatile uint32_t tk_parameter_word, tk_ready;
static tk_metronome_clock_t metro_clock;
#define TK_RENDER_BUDGET_US (HALF_FRAMES * 1000000u / FS)
#ifndef TERKINSTEIN_HOST
/* Split the constant quotient/remainder to preserve fractional microseconds
 * in the deadline without overflowing a 32-bit intermediate. */
#define TK_RENDER_BUDGET_TICKS (TK_RENDER_BUDGET_US * FM1_TICKS_PER_US \
    + (HALF_FRAMES * 1000000u % FS) * FM1_TICKS_PER_US / FS)
/* Audio alone writes these scalars. Main reads each atomically; they are
 * observations, not a coherent multi-field snapshot. Time tk_render, click
 * mixing and the optional visual tap; MASTER, USB, DAC and later ISR are excluded. */
static volatile uint32_t tk_render_last_ticks, tk_render_max_ticks, tk_render_late_blocks;
#endif
static void app_audio(int32_t *out, uint32_t n)
{
    uint32_t word;
#ifndef TERKINSTEIN_HOST
    uint32_t started, elapsed;
#endif
    tk_params_t p;
    if (!tk_ready) return;
    word = tk_parameter_word;
    p.shade = (uint8_t)word;
    p.fracture = (uint8_t)(word >> 8);
    p.drift = (uint8_t)(word >> 16);
    p.bloom = (uint8_t)(word >> 24);
#ifndef TERKINSTEIN_HOST
    started = fm1_ticks();
#endif
    tk_render(out, n, &p);
    tk_metronome_render(out, n);
    if (tk_visual_due()) {
        uint32_t i, voices[TK_VIS_VOICES];
        for (i = 0; i < TK_VIS_VOICES; i++) {
            uint32_t env = tk_voice[i].env > 0 ? (uint32_t)tk_voice[i].env >> 15 : 0u;
            if (env > 255u) env = 255u;
            voices[i] = tk_voice[i].audio_note | (env << 8) |
                (tk_voice[i].source << 16) | (tk_voice[i].gate << 24);
        }
        tk_visual_capture(out, n, voices, TK_VIS_VOICES);
    }
#ifndef TERKINSTEIN_HOST
    elapsed = fm1_ticks() - started; /* unsigned subtraction survives timer wrap */
    tk_render_last_ticks = elapsed;
    if (elapsed > tk_render_max_ticks) tk_render_max_ticks = elapsed;
    if (elapsed > TK_RENDER_BUDGET_TICKS) tk_render_late_blocks++;
#endif
}

typedef struct {
    uint32_t schema;
    tk_params_t p;
    int8_t octave;
    uint8_t preset, fx, memory;
} tk_settings_v2_t;
/* The installed 0.6 record has two-byte notes. Keep its layout independent
 * of the current pattern so existing three-track saves can be upgraded. */
typedef struct { uint8_t pitch, velocity; } tk_pattern_note_v4_t;
typedef struct {
    uint8_t count;
    tk_pattern_note_v4_t note[TK_PATTERN_NOTES];
} tk_pattern_step_v4_t;
typedef struct { tk_pattern_step_v4_t step[16]; } tk_pattern_v4_t;
typedef struct { uint8_t pitch, velocity, duration_q4; } tk_pattern_note_v5_t;
typedef struct {
    uint8_t count;
    tk_pattern_note_v5_t note[TK_PATTERN_NOTES];
} tk_pattern_step_v5_t;
typedef struct { tk_pattern_step_v5_t step[16]; } tk_pattern_v5_t;
typedef struct {
    uint32_t schema;
    tk_params_t p;
    int8_t octave;
    uint8_t preset, fx, memory;
    uint8_t tempo, lfo_rate, lfo_depth, lfo_shape, lfo_on, arp_mode;
    uint8_t attack, release;
    tk_pattern_v4_t pattern;
} tk_settings_v3_t;
/* Schema 3 was shipped in 0.3; read it without altering its exact layout. */
typedef char tk_settings_schema3_size[(sizeof(tk_settings_v3_t) == 164u) ? 1 : -1];
#define TK_SCHEMA 13u
#define TK_SETTINGS_V12_BYTES 3700u
#define TK_LEGACY_TRACKS 3u
#define TK_TRACKS 4u
#define TK_BANK_COUNT 4u
#include "terkinstein_arrangement_data.h"
enum { TK_TRACK_DRUM, TK_TRACK_MELODY, TK_TRACK_ATMOS, TK_TRACK_BASS };
static const uint8_t TRACK_SOURCE[TK_TRACKS + 1u] = {
    TK_SOURCE_DRUM, TK_SOURCE_SEQUENCE, TK_SOURCE_ATMOS, TK_SOURCE_BASS, TK_SOURCE_ARP
};
static const uint8_t TRACK_MIDI_ON[TK_TRACKS + 1u] = {0x99u, 0x91u, 0x92u, 0x93u, 0x91u};
static const uint8_t TRACK_MIDI_OFF[TK_TRACKS + 1u] = {0x89u, 0x81u, 0x82u, 0x83u, 0x81u};
typedef struct {
    tk_params_t p;
    uint8_t preset, fx, memory, attack, release;
    uint8_t lfo_on, lfo_shape, lfo_rate, lfo_depth, instrument;
} tk_track_patch_t;
typedef char tk_track_patch_wire_size[(sizeof(tk_track_patch_t) == 14u) ? 1 : -1];
typedef struct {
    uint32_t schema;
    tk_params_t p;
    int8_t octave;
    uint8_t preset, fx, memory;
    uint8_t tempo, lfo_rate, lfo_depth, lfo_shape, lfo_on, arp_mode;
    uint8_t attack, release;
    tk_pattern_v4_t pattern[TK_LEGACY_TRACKS];
    tk_track_patch_t track[TK_LEGACY_TRACKS];
    uint8_t track_mute;
} tk_settings_v4_t;
typedef char tk_settings_schema4_size[(sizeof(tk_settings_v4_t) == 496u) ? 1 : -1];
typedef struct {
    uint32_t schema;
    tk_params_t p;
    int8_t octave;
    uint8_t preset, fx, memory;
    uint8_t tempo, lfo_rate, lfo_depth, lfo_shape, lfo_on, arp_mode;
    uint8_t attack, release;
    tk_pattern_v5_t pattern[TK_LEGACY_TRACKS];
    tk_track_patch_t track[TK_LEGACY_TRACKS];
    uint8_t track_mute;
} tk_settings_v5_t;
typedef char tk_settings_schema5_size[(sizeof(tk_settings_v5_t) == 688u) ? 1 : -1];
typedef struct {
    uint32_t schema;
    tk_params_t p;
    int8_t octave;
    uint8_t preset, fx, memory;
    uint8_t tempo, lfo_rate, lfo_depth, lfo_shape, lfo_on, arp_mode;
    uint8_t attack, release;
    tk_pattern_v5_t pattern[TK_LEGACY_TRACKS];
    tk_track_patch_t track[TK_LEGACY_TRACKS];
    uint8_t track_mute, ghost_mask;
} tk_settings_v6_t;
typedef char tk_settings_schema6_size[(sizeof(tk_settings_v6_t) == 688u) ? 1 : -1];
typedef struct {
    uint32_t schema;
    tk_params_t p;
    int8_t octave;
    uint8_t preset, fx, memory;
    uint8_t tempo, lfo_rate, lfo_depth, lfo_shape, lfo_on, arp_mode;
    uint8_t attack, release;
    tk_pattern_t pattern[TK_TRACKS];
    tk_track_patch_t track[TK_TRACKS];
    uint8_t track_mute, ghost_mask, length[TK_TRACKS];
} tk_settings_v7_t;
typedef char tk_settings_schema7_size[(sizeof(tk_settings_v7_t) == 3412u) ? 1 : -1];
typedef struct {
    uint32_t schema;
    tk_params_t p;
    int8_t octave;
    uint8_t preset, fx, memory;
    uint8_t tempo, lfo_rate, lfo_depth, lfo_shape, lfo_on, arp_mode;
    uint8_t attack, release;
    tk_pattern_t pattern[TK_TRACKS];
    tk_track_patch_t track[TK_TRACKS];
    uint8_t track_mute, ghost_mask, length[TK_TRACKS];
    tk_track_patch_t graft_base;
    uint8_t graft_target, graft_amount;
} tk_settings_v8_t;
typedef char tk_settings_schema8_size[(sizeof(tk_settings_v8_t) == 3428u) ? 1 : -1];
#define TK_MIDI_BINDINGS 8u
typedef struct { uint8_t cc, port, channel; } tk_midi_binding_t;
typedef struct {
    uint32_t schema;
    tk_params_t p;
    int8_t octave;
    uint8_t preset, fx, memory;
    uint8_t tempo, lfo_rate, lfo_depth, lfo_shape, lfo_on, arp_mode;
    uint8_t attack, release;
    tk_pattern_t pattern[TK_TRACKS];
    tk_track_patch_t track[TK_TRACKS];
    uint8_t track_mute, ghost_mask, length[TK_TRACKS];
    tk_track_patch_t graft_base;
    uint8_t graft_target, graft_amount;
    tk_midi_binding_t midi_bind[TK_MIDI_BINDINGS];
    uint8_t midi_bend_range;
} tk_settings_v9_t;
typedef char tk_settings_schema9_size[(sizeof(tk_settings_v9_t) == 3452u) ? 1 : -1];

typedef struct {
    uint32_t schema;
    tk_params_t p;
    int8_t octave;
    uint8_t preset, fx, memory;
    uint8_t tempo, lfo_rate, lfo_depth, lfo_shape, lfo_on, arp_mode;
    uint8_t attack, release;
    tk_pattern_t pattern[TK_TRACKS];
    tk_track_patch_t track[TK_TRACKS];
    uint8_t track_mute, ghost_mask, length[TK_TRACKS];
    tk_track_patch_t graft_base;
    uint8_t graft_target, graft_amount;
    tk_midi_binding_t midi_bind[TK_MIDI_BINDINGS];
    uint8_t midi_bend_range, midi_pad;
    tk_fm_patch_t fm[6]; /* live, DRUM/MELODY/ATMOS/BASS, GRAFT base */
    uint8_t mono;
} tk_settings_v10_t;
typedef char tk_settings_schema10_size[(sizeof(tk_settings_v10_t) == 3696u) ? 1 : -1];

typedef struct {
    uint32_t schema;
    tk_params_t p;
    int8_t octave;
    uint8_t preset, fx, memory;
    uint8_t tempo, lfo_rate, lfo_depth, lfo_shape, lfo_on, arp_mode;
    uint8_t attack, release;
    tk_pattern_t pattern[TK_TRACKS];
    tk_track_patch_t track[TK_TRACKS];
    uint8_t track_mute, ghost_mask, length[TK_TRACKS];
    tk_track_patch_t graft_base;
    uint8_t graft_target, graft_amount;
    tk_midi_binding_t midi_bind[TK_MIDI_BINDINGS];
    uint8_t midi_bend_range, panel_response; /* formerly reserved zero byte */
    tk_fm_patch_t fm[6]; /* live, DRUM/MELODY/ATMOS/BASS, GRAFT base */
    uint8_t mono;
    uint8_t metro_enabled, metro_level, metro_beats, metro_unit;
} tk_settings_v11_t;
typedef char tk_settings_schema11_size[(sizeof(tk_settings_v11_t) == 3700u) ? 1 : -1];

typedef struct {
    uint32_t schema;
    tk_params_t p;
    int8_t octave;
    uint8_t preset, fx, memory;
    uint8_t tempo, lfo_rate, lfo_depth, lfo_shape, lfo_on, arp_mode;
    uint8_t attack, release;
    tk_pattern_t pattern[TK_TRACKS];
    tk_track_patch_t track[TK_TRACKS];
    uint8_t track_mute, ghost_mask, length[TK_TRACKS];
    tk_track_patch_t graft_base;
    uint8_t graft_target, graft_amount;
    tk_midi_binding_t midi_bind[TK_MIDI_BINDINGS];
    uint8_t midi_bend_range, panel_response;
    tk_fm_patch_t fm[6];
    uint8_t mono;
    uint8_t metro_enabled, metro_level, metro_beats, metro_unit;
    uint8_t home_visual; /* tagged cosmetic choice; former byte 3697 padding */
    uint16_t section_mutes; /* track n uses bits n*4..n*4+3; old tail padding */
    uint32_t swing; /* schema13: 0..100; zero is the exact old straight clock */
} tk_settings_t;
/* Four 64-step patterns stay inside the guarded 4 KiB settings sector. */
typedef char tk_settings_schema13_size[(sizeof(tk_settings_t) == 3704u) ? 1 : -1];
typedef char tk_settings_v12_prefix[(offsetof(tk_settings_t, swing) == TK_SETTINGS_V12_BYTES) ? 1 : -1];
typedef char tk_settings_home_visual_offset[(offsetof(tk_settings_t, home_visual) == 3697u) ? 1 : -1];
typedef char tk_settings_fits_flash[(sizeof(tk_settings_t) <= 3840u) ? 1 : -1];
#define KEY0 53u
#define C_ACC RGB(178, 76, 244)
#define TK_DIM RGB(120, 94, 148)
#define C_BAR RGB(36, 21, 49)
#define TK_GREEN RGB(135, 255, 101)
enum { TK_PAGE_PAD, TK_PAGE_MEMORY, TK_PAGE_GLO, TK_PAGE_ENV, TK_PAGE_LFO, TK_PAGE_ARP, TK_PAGE_SEQ, TK_PAGE_SOUND_EDIT, TK_PAGE_MIDI,
       TK_PAGE_OPERATOR, TK_PAGE_PITCH, TK_PAGE_GLOBAL, TK_PAGE_VOICE, TK_PAGE_FX, TK_PAGE_METRONOME, TK_PAGE_CONTROLS,
       TK_PAGE_ARRANGE, TK_PAGE_STEP_EDIT, TK_PAGE_SCENES, TK_PAGE_PIANO };
static uint8_t tk_arr_lock_edit, tk_scene_focus;
/* HOME from SEQ enters an explicit captured-track workbench. The live patch
 * stays in settings outside bounded panel/drawing scopes, including queries,
 * incoming MIDI and SAVE. HOME again returns to the independent live preset. */
static uint8_t tk_panel_edit_track = TK_TRACKS;
static uint8_t tk_panel_audition_track = TK_TRACKS;
static const char *const TK_SOUND_CONTEXT[] = {"DRUM", "MELODY", "ATMOS", "BASS", "LIVE"};
enum { TK_MODE_OFF, TK_MODE_ARP, TK_MODE_SEQ };
typedef struct { uint32_t pressed_ms; uint8_t pending, long_press; } tk_fx_button_t;
/* Short physical presses toggle on release. A held/released 700-ms press
 * navigates only, including a release exactly at the threshold or clock wrap.
 * A synthetic one-shot edge with no held state is an instantaneous short tap. */
static uint32_t tk_fx_button_event(tk_fx_button_t *state, uint32_t pressed,
                                  uint32_t released, uint32_t held, uint32_t now)
{
    uint32_t action = 0;
    if (pressed) {
        state->pressed_ms = now; state->pending = 1; state->long_press = 0;
        if (!held) { state->pending = 0; return 1u; }
    }
    if (state->pending && !state->long_press && now - state->pressed_ms >= 700u) {
        state->long_press = 1; action = 2u;
    }
    if (state->pending && (released || !held)) {
        if (!state->long_press) action = 1u;
        state->pending = 0;
    }
    return action;
}
enum { TK_RESET_OPEN = 1u, TK_RESET_READY = 2u, TK_RESET_CANCEL = 4u, TK_RESET_CONFIRM = 8u };
typedef struct {
    uint32_t pressed_ms, opened_ms;
    uint8_t tracking, prompt, ready;
} tk_home_reset_t;
static void tk_home_reset_cancel(tk_home_reset_t *r)
{ r->tracking = r->prompt = r->ready = 0u; }
/* Main-only, physical two-gesture confirmation. A raw held bit without a fresh
 * HOME edge cannot arm it. Navigation taps and virtual HOME/SEL never reset.
 * The prompt must observe every physical button released before a fresh,
 * physical SEL-only press can confirm. Other controls cancel it. */
static uint32_t tk_home_reset_event(tk_home_reset_t *r, uint32_t pressed,
                                  uint32_t held, uint32_t any_pressed,
                                  uint32_t cancel, uint32_t now)
{
    const uint32_t home = 1u << BTN_HOME, sel = 1u << BTN_SEL;
    if (cancel || (any_pressed & ~(home | sel))) {
        uint32_t action = r->prompt ? TK_RESET_CANCEL : 0u;
        tk_home_reset_cancel(r); return action;
    }
    if (r->prompt) {
        if (now - r->opened_ms >= 10000u || (any_pressed & home) ||
            ((any_pressed & sel) && !(pressed & sel))) {
            tk_home_reset_cancel(r); return TK_RESET_CANCEL;
        }
        if (!held && !r->ready) { r->ready = 1u; return TK_RESET_READY; }
        if (r->ready && (pressed & sel) && held == sel) {
            tk_home_reset_cancel(r); return TK_RESET_CONFIRM;
        }
        return 0u;
    }
    if ((pressed & home) && held == home) {
        r->pressed_ms = now; r->tracking = 1u;
    }
    if (held != home) r->tracking = 0u;
    if (r->tracking && now - r->pressed_ms >= 700u) {
        r->tracking = 0u; r->prompt = 1u; r->ready = 0u; r->opened_ms = now;
        return TK_RESET_OPEN;
    }
    return 0u;
}
typedef struct {
    const char *name;
    tk_params_t p;
    uint8_t memory, fx, attack, release;
    uint8_t lfo_on, lfo_shape, lfo_rate, lfo_depth;
    uint8_t instrument;
} tk_preset_t;
/* Factory data is generated from the authored catalog. Existing slots retain
 * their indices and settings; appended sounds can interleave instrument types. */
#include "terkinstein_presets.h"
#define TK_PRESET_COUNT ((uint32_t)(sizeof PRESETS / sizeof PRESETS[0]))
#define TK_KIT_PRESET_INDEX 30u
/* Only legacy-schema readers use this boundary: schemas before 10 contained
 * the original pre-FM bank. Current dispatch uses each slot's explicit map. */
#define TK_FM_PRESET_FIRST 32u
/* Count itself travels in one MIDI-safe HELLO/STATE byte. */
typedef char tk_presets_fit_index[(TK_PRESET_COUNT <= 127u) ? 1 : -1];
typedef char tk_fm_map_matches_bank[(sizeof TK_PRESET_FM_PATCH == TK_PRESET_COUNT) ? 1 : -1];
typedef char tk_kit_preset_exists[(TK_KIT_PRESET_INDEX < TK_PRESET_COUNT) ? 1 : -1];
/* Storage/Program Change IDs never move. PRESETS browsing presents the
 * 64 current choices; IDs24/51 remain readable old-song definitions. */
static uint32_t factory_position(uint32_t sound)
{
    return sound == 64u ? 24u : sound == 65u ? 51u : sound;
}
static uint32_t factory_next(uint32_t sound, int32_t delta)
{
    int32_t at=(int32_t)factory_position(sound), count=(int32_t)TK_FACTORY_COUNT;
    return TK_FACTORY_SOUND[(at+delta%count+count)%count];
}
static const char *const MACRO_NAME[] = {"SHADE", "FRACTURE", "DRIFT", "BLOOM"};
static const char *const FORGE_NAME[] = {"BODY", "WIRE", "DECAY", "BITE"};
static const char *const ARP_NAME[] = {"UP", "DOWN", "PING PONG", "OUTSIDE IN", "RANDOM"};
static const char *const LFO_NAME[] = {"TRIANGLE", "PULSE", "RAMP"};
static const char *const MIDI_TARGET[] = {
    "SHADE / BODY", "FRACTURE / WIRE", "DRIFT / DECAY", "BLOOM / BITE",
    "MEMORY", "GRAFT", "TEMPO", "PLAY / STOP", "BEND RANGE"
};
/* Main owns learn/RPN state. Performance values are audio-owned per source. */
static uint8_t midi_focus, midi_learning, midi_latch;
static uint8_t midi_rpn_msb[TK_SOURCES], midi_rpn_lsb[TK_SOURCES];
/* External-controller session preference; never stored in the song or flash. USB
 * channel 16 keeps its ordinary live meaning unless explicitly negotiated. */
static uint8_t midi_live_drum_enabled;
static uint32_t midi_live_drum_lease_ms;
static uint8_t midi_fire_mask;
static uint32_t midi_fire_lease_ms;
static uint8_t midi_keyboard_mask, midi_keyboard_channel[2] = {0u, 1u};
static uint32_t midi_keyboard_lease_ms;
#define TK_PAD_ROUTE_LEASE_MS 3000u
static uint32_t midi_source(uint32_t port, uint32_t channel)
{
    uint32_t slot;
    if (!port) for (slot = 0; slot < 2u; slot++)
        if ((midi_keyboard_mask & (1u << slot)) && channel == midi_keyboard_channel[slot])
            return TK_SOURCE_KEYBOARD0 + slot;
    if (!port && channel >= 11u && channel <= 14u && (midi_fire_mask & (1u << (channel - 11u))))
        return TK_SOURCE_FIRE0 + channel - 11u;
    return midi_live_drum_enabled && !port && channel == 15u ?
        TK_SOURCE_LIVE_DRUM : 1u + port * 16u + channel;
}
static void midi_mapping_defaults(tk_settings_t *s)
{
    uint32_t i;
    for (i = 0; i < TK_MIDI_BINDINGS; i++) {
        s->midi_bind[i].cc = i < 5u ? (uint8_t)(20u + i) : 255u;
        s->midi_bind[i].port = 2u; s->midi_bind[i].channel = 16u;
    }
    s->midi_bend_range = 2u;
}
static void midi_performance_init(const tk_settings_t *s)
{
    uint32_t source;
    for (source = 1u; source < TK_SOURCE_SEQUENCE; source++) {
        midi_rpn_msb[source] = midi_rpn_lsb[source] = 127u;
        tk_set_bend_range(source, s->midi_bend_range);
    }
    midi_live_drum_enabled = 0u;
    midi_fire_mask = 0u;
    midi_keyboard_mask = 0u;
    midi_keyboard_channel[0] = 0u; midi_keyboard_channel[1] = 1u;
    tk_keyboard_track[0] = 1u; tk_keyboard_track[1] = 2u;
    for (source = 0; source < 4u; source++) tk_fire_track[source] = source;
    midi_rpn_msb[TK_SOURCE_LIVE_DRUM] = midi_rpn_lsb[TK_SOURCE_LIVE_DRUM] = 127u;
    tk_set_bend_range(TK_SOURCE_LIVE_DRUM, s->midi_bend_range);
    midi_latch = 0;
}
static int midi_reserved_cc(uint8_t cc)
{
    return cc == 1u || cc == 6u || cc == 7u || cc == 11u || cc == 38u
        || cc == 64u || cc == 66u || cc == 67u || cc == 71u || cc == 74u
        || cc == 98u || cc == 99u || cc == 100u || cc == 101u || cc >= 120u;
}
static uint8_t midi_next_cc(uint8_t current, int32_t delta)
{
    uint8_t choices[129];
    uint32_t cc, count = 0, at = 0;
    for (cc = 0; cc < 128u; cc++) if (!midi_reserved_cc((uint8_t)cc)) {
        if (cc == current) at = count;
        choices[count++] = (uint8_t)cc;
    }
    if (current == 255u) at = count;
    choices[count++] = 255u;
    return choices[((int32_t)at + delta % (int32_t)count + (int32_t)count) % (int32_t)count];
}
static void midi_binding_unique(tk_settings_t *s, uint32_t index)
{
    uint32_t i;
    const tk_midi_binding_t *binding = &s->midi_bind[index];
    if (binding->cc == 255u) return;
    for (i = 0; i < TK_MIDI_BINDINGS; i++) if (i != index) {
        tk_midi_binding_t *other = &s->midi_bind[i];
        if (other->cc == binding->cc && other->port == binding->port
            && other->channel == binding->channel) other->cc = 255u;
    }
}

/* Keep schema-3 settings binary-compatible. Values 0..2 are the original
 * direction choices and decode to the old 1/16, 75% behavior. Other choices
 * pack order, rate and gate into the existing arp_mode byte. */
#define TK_ARP_MODE_COUNT (TK_ARP_LAST_MODE + 1u)
#define TK_ARP_DIV_COUNT 3u
#define TK_ARP_GATE_COUNT 4u
#define TK_ARP_CONFIG_BASE 3u
#define TK_ARP_CONFIG_LIMIT (TK_ARP_CONFIG_BASE + TK_ARP_MODE_COUNT * TK_ARP_DIV_COUNT * TK_ARP_GATE_COUNT)
static const uint8_t ARP_GATE_PERCENT[TK_ARP_GATE_COUNT] = {25u, 50u, 65u, 75u};
static uint8_t arp_mode_of(uint8_t packed)
{
    return packed < TK_ARP_CONFIG_BASE ? packed :
        (uint8_t)((packed - TK_ARP_CONFIG_BASE) % TK_ARP_MODE_COUNT);
}
static uint8_t arp_division_of(uint8_t packed)
{
    return packed < TK_ARP_CONFIG_BASE ? 1u :
        (uint8_t)(((packed - TK_ARP_CONFIG_BASE) / TK_ARP_MODE_COUNT) % TK_ARP_DIV_COUNT);
}
static uint8_t arp_gate_of(uint8_t packed)
{
    return packed < TK_ARP_CONFIG_BASE ? 3u :
        (uint8_t)((packed - TK_ARP_CONFIG_BASE) / (TK_ARP_MODE_COUNT * TK_ARP_DIV_COUNT));
}
static uint8_t arp_pack(uint8_t mode, uint8_t division, uint8_t gate)
{
    if (mode <= TK_ARP_PINGPONG && division == 1u && gate == 3u) return mode;
    return (uint8_t)(TK_ARP_CONFIG_BASE + mode + TK_ARP_MODE_COUNT * division
        + TK_ARP_MODE_COUNT * TK_ARP_DIV_COUNT * gate);
}

static void graft_reset(tk_settings_t *s);
static void fm_for_preset(tk_fm_patch_t *p, uint32_t index)
{
    uint32_t factory;
    if (!p) return;
    tk_fm_defaults(p);
    if (index >= TK_PRESET_COUNT || PRESETS[index].instrument != TK_INSTRUMENT_FM) return;
    factory = TK_PRESET_FM_PATCH[index];
    if (factory < 4u) tk_fm_factory(p, factory); /* Preserve the four legacy patches. */
    else if (factory - 4u < sizeof TK_NEW_FM_FACTORY / sizeof TK_NEW_FM_FACTORY[0])
        *p = TK_NEW_FM_FACTORY[factory - 4u];
}
static void settings_metro_defaults(tk_settings_t *s)
{
    s->metro_enabled = 0; s->metro_level = 32; s->metro_beats = 4; s->metro_unit = 4;
}
static void settings_fm_defaults(tk_settings_t *s)
{
    uint32_t i;
    for (i = 0; i < 6u; i++) tk_fm_defaults(&s->fm[i]);
    s->mono = 0; s->panel_response = TK_ENCODER_LINEAR;
}
static void preset_apply(tk_settings_t *s, uint32_t index)
{
    const tk_preset_t *preset;
    if (index >= TK_PRESET_COUNT) return;
    preset = &PRESETS[index];
    s->preset = (uint8_t)index;
    s->p = preset->p;
    s->memory = preset->memory;
    s->fx = preset->fx;
    s->attack = preset->attack; s->release = preset->release;
    s->lfo_on = preset->lfo_on; s->lfo_shape = preset->lfo_shape;
    s->lfo_rate = preset->lfo_rate; s->lfo_depth = preset->lfo_depth;
    fm_for_preset(&s->fm[0], index);
    graft_reset(s);
}
static void track_patch_capture(tk_track_patch_t *track, const tk_settings_t *s)
{
    track->p = s->p;
    track->preset = s->preset;
    track->fx = s->fx; track->memory = s->memory;
    track->attack = s->attack; track->release = s->release;
    track->lfo_on = s->lfo_on; track->lfo_shape = s->lfo_shape;
    track->lfo_rate = s->lfo_rate; track->lfo_depth = s->lfo_depth;
    track->instrument = PRESETS[s->preset].instrument;
}
static void track_patch_factory(tk_track_patch_t *track, uint32_t index)
{
    const tk_preset_t *preset = &PRESETS[index];
    track->p = preset->p;
    track->preset = (uint8_t)index;
    track->fx = preset->fx; track->memory = preset->memory;
    track->attack = preset->attack; track->release = preset->release;
    track->lfo_on = preset->lfo_on; track->lfo_shape = preset->lfo_shape;
    track->lfo_rate = preset->lfo_rate; track->lfo_depth = preset->lfo_depth;
    track->instrument = preset->instrument;
}
static void track_factory_apply(tk_settings_t *s, uint32_t track, uint32_t preset)
{
    if (track >= TK_TRACKS || preset >= TK_PRESET_COUNT) return;
    track_patch_factory(&s->track[track], preset);
    fm_for_preset(&s->fm[track + 1u], preset);
}
static void track_capture_apply(tk_settings_t *s, uint32_t track)
{
    if (track >= TK_TRACKS) return;
    track_patch_capture(&s->track[track], s);
    s->fm[track + 1u] = s->fm[0];
}

/* GRAFT is main-owned control interpolation, never a second audio voice.
 * The base keeps manual edits; the target is a compatible factory patch. */
static uint8_t graft_next_target(const tk_settings_t *s, int32_t delta)
{
    uint8_t candidates[TK_PRESET_COUNT];
    uint32_t i, count = 0, at = 0;
    uint8_t instrument = PRESETS[s->preset].instrument;
    if (instrument == TK_INSTRUMENT_DRUM_KIT) return s->preset;
    for (i = 0; i < TK_PRESET_COUNT; i++)
        if (i != s->preset && PRESETS[i].instrument == instrument) {
            if (i == s->graft_target) at = count;
            candidates[count++] = (uint8_t)i;
        }
    if (!count) return s->preset;
    return candidates[((int32_t)at + delta % (int32_t)count + (int32_t)count) % (int32_t)count];
}
static void graft_reset(tk_settings_t *s)
{
    uint32_t i, count = s->preset < 64u ? 64u : TK_PRESET_COUNT;
    s->graft_amount = 0;
    track_patch_capture(&s->graft_base, s);
    s->fm[5] = s->fm[0];
    s->graft_target = s->preset;
    if (PRESETS[s->preset].instrument != TK_INSTRUMENT_DRUM_KIT)
        for (i = 1; i < count; i++) {
            uint32_t candidate = (s->preset + i) % count;
            if (PRESETS[candidate].instrument == PRESETS[s->preset].instrument) {
                s->graft_target = (uint8_t)candidate;
                break;
            }
        }
}
static void graft_bake(tk_settings_t *s)
{
    s->graft_amount = 0;
    track_patch_capture(&s->graft_base, s);
    s->fm[5] = s->fm[0];
}
static uint8_t graft_mix(uint8_t a, uint8_t b, uint8_t amount)
{
    /* Convex integer blend, exact at both endpoints and safe in 32 bits. */
    return (uint8_t)(((uint32_t)a * (127u - amount) + (uint32_t)b * amount + 63u) / 127u);
}
static void graft_apply(tk_settings_t *s)
{
    tk_track_patch_t b, *a = &s->graft_base;
    uint8_t amount = s->graft_amount;
    track_patch_factory(&b, s->graft_target);
    if (a->instrument == TK_INSTRUMENT_FM) {
        tk_fm_patch_t target;
        uint32_t i;
        fm_for_preset(&target, s->graft_target);
        for (i = 0; i < 6u; i++) {
            const tk_fm_operator_t *base = &s->fm[5].op[i], *to = &target.op[i];
            tk_fm_operator_t *op = &s->fm[0].op[i];
            op->ratio = graft_mix(base->ratio, to->ratio, amount);
            op->level = graft_mix(base->level, to->level, amount);
            op->attack = graft_mix(base->attack, to->attack, amount);
            op->decay = graft_mix(base->decay, to->decay, amount);
            op->sustain = graft_mix(base->sustain, to->sustain, amount);
            op->release = graft_mix(base->release, to->release, amount);
        }
        s->fm[0].algorithm = amount == 127u ? target.algorithm : s->fm[5].algorithm;
        s->fm[0].feedback = graft_mix(s->fm[5].feedback, target.feedback, amount);
        s->fm[0].pitch_depth = graft_mix(s->fm[5].pitch_depth, target.pitch_depth, amount);
        s->fm[0].pitch_time = graft_mix(s->fm[5].pitch_time, target.pitch_time, amount);
    }
    s->p.shade = graft_mix(a->p.shade, b.p.shade, amount);
    s->p.fracture = graft_mix(a->p.fracture, b.p.fracture, amount);
    s->p.drift = graft_mix(a->p.drift, b.p.drift, amount);
    s->p.bloom = graft_mix(a->p.bloom, b.p.bloom, amount);
    s->memory = graft_mix(a->memory, b.memory, amount);
    s->attack = graft_mix(a->attack, b.attack, amount);
    s->release = graft_mix(a->release, b.release, amount);
    s->lfo_rate = graft_mix(a->lfo_rate, b.lfo_rate, amount);
    s->lfo_depth = graft_mix(a->lfo_depth, b.lfo_depth, amount);
    s->fx = amount == 127u ? b.fx : a->fx;
    s->lfo_on = amount == 127u ? b.lfo_on : a->lfo_on;
    s->lfo_shape = amount == 127u ? b.lfo_shape : a->lfo_shape;
    if (amount && amount < 127u) {
        if (a->fx != b.fx) {
            s->fx = 1;
            s->p.fracture = graft_mix(a->fx ? a->p.fracture : 0u,
                b.fx ? b.p.fracture : 0u, amount);
        }
        if (a->lfo_on != b.lfo_on) {
            s->lfo_on = 1;
            s->lfo_depth = graft_mix(a->lfo_on ? a->lfo_depth : 0u,
                b.lfo_on ? b.lfo_depth : 0u, amount);
        }
    }
}
static void settings_legacy_finish(tk_settings_t *s)
{
    uint32_t i;
    midi_mapping_defaults(s);
    settings_fm_defaults(s);
    settings_metro_defaults(s);
    /* KIT macros had no audible effect before 0.11. Keep its old sound. */
    if (s->preset < TK_PRESET_COUNT && PRESETS[s->preset].instrument == TK_INSTRUMENT_DRUM_KIT
        && s->p.shade <= 127u && s->p.fracture <= 127u && s->p.drift <= 127u && s->p.bloom <= 127u)
        s->p.shade = s->p.fracture = s->p.drift = s->p.bloom = 64u;
    for (i = 0; i < TK_TRACKS; i++)
        if (s->track[i].instrument == TK_INSTRUMENT_DRUM_KIT
            && s->track[i].p.shade <= 127u && s->track[i].p.fracture <= 127u
            && s->track[i].p.drift <= 127u && s->track[i].p.bloom <= 127u)
            s->track[i].p.shade = s->track[i].p.fracture =
                s->track[i].p.drift = s->track[i].p.bloom = 64u;
    if (s->preset < TK_PRESET_COUNT) graft_reset(s);
}

static void seed_pattern(tk_pattern_t *pattern)
{
    static const uint8_t pitch[16] = {
        48, 0, 55, 0, 51, 0, 58, 55, 60, 0, 55, 0, 51, 0, 58, 55
    };
    uint32_t i;
    tk_pattern_clear(pattern);
    for (i = 0; i < 16u; i++)
        if (pitch[i]) tk_pattern_record(pattern, i, pitch[i], 96u);
}
static int pattern_upgrade(tk_pattern_t *new_pattern, const tk_pattern_v4_t *old_pattern)
{
    uint32_t step, i;
    tk_pattern_clear(new_pattern);
    for (step = 0; step < 16u; step++) {
        const tk_pattern_step_v4_t *old_step = &old_pattern->step[step];
        if (old_step->count > TK_PATTERN_NOTES) return 0;
        for (i = 0; i < old_step->count; i++) {
            uint32_t pitch = old_step->note[i].pitch;
            if (pitch > 127u || !old_step->note[i].velocity ||
                old_step->note[i].velocity > 127u ||
                (i && old_step->note[i - 1u].pitch >= pitch) ||
                !tk_pattern_record(new_pattern, step, pitch,
                                   old_step->note[i].velocity)) return 0;
        }
    }
    return 1;
}
static void settings_defaults(tk_settings_t *s)
{
    uint32_t track;
    memset(s, 0, sizeof *s);
    s->schema = TK_SCHEMA;
    s->octave = -1; s->tempo = 90;
    settings_fm_defaults(s);
    settings_metro_defaults(s);
    preset_apply(s, 0);
    midi_mapping_defaults(s);
    for (track = 0; track < TK_TRACKS; track++) {
        tk_pattern_clear(&s->pattern[track]);
        s->length[track] = 16u;
    }
    track_factory_apply(s, TK_TRACK_DRUM, TK_KIT_PRESET_INDEX);
    track_capture_apply(s, TK_TRACK_MELODY);
    track_factory_apply(s, TK_TRACK_ATMOS, 3u);
    track_factory_apply(s, TK_TRACK_BASS, 18u); /* TOMB CURRENT */
    seed_pattern(&s->pattern[TK_TRACK_MELODY]);
}
static int pattern_valid(const tk_pattern_t *pattern)
{
    uint32_t step, i;
    for (step = 0; step < TK_PATTERN_STEPS; step++) {
        const tk_pattern_step_t *part = &pattern->step[step];
        if (part->count > TK_PATTERN_NOTES) return 0;
        for (i = 0; i < part->count; i++) {
            if (part->note[i].pitch > 127u || !part->note[i].velocity || part->note[i].velocity > 127u
                || (i && part->note[i - 1u].pitch >= part->note[i].pitch)) return 0;
        }
    }
    return 1;
}
static int track_patch_valid(const tk_track_patch_t *p)
{
    return p->preset < TK_PRESET_COUNT && p->fx <= 1u
        && p->p.shade <= 127u && p->p.fracture <= 127u && p->p.drift <= 127u && p->p.bloom <= 127u
        && p->memory <= 127u && p->attack <= 127u && p->release <= 127u && p->lfo_on <= 1u
        && p->lfo_shape <= TK_LFO_RAMP && p->lfo_rate <= 127u && p->lfo_depth <= 127u
        && p->instrument <= TK_INSTRUMENT_FM && p->instrument == PRESETS[p->preset].instrument;
}
static int settings_valid(const tk_settings_t *s)
{
    uint32_t track;
    if (s->swing > 100u || s->schema != TK_SCHEMA || s->octave < -2 || s->octave > 2 || s->preset >= TK_PRESET_COUNT || s->fx > 1u
        || s->p.shade > 127u || s->p.fracture > 127u || s->p.drift > 127u || s->p.bloom > 127u
        || s->memory > 127u || s->tempo < 40u || s->tempo > 180u || s->lfo_rate > 127u
        || s->lfo_depth > 127u || s->lfo_shape > TK_LFO_RAMP || s->lfo_on > 1u
        || s->arp_mode >= TK_ARP_CONFIG_LIMIT || s->attack > 127u || s->release > 127u
        || s->track_mute >= (1u << TK_TRACKS) || s->ghost_mask > 127u
        || s->graft_target >= TK_PRESET_COUNT || s->graft_amount > 127u
        || s->midi_bend_range > 24u || s->mono > 1u || s->panel_response > TK_ENCODER_ADAPTIVE
        || s->metro_enabled > 1u || s->metro_level > 127u
        || s->metro_beats < 1u || s->metro_beats > 12u
        || (s->metro_unit != 4u && s->metro_unit != 8u)) return 0;
    for (track = 0; track < 6u; track++) if (!tk_fm_valid(&s->fm[track])) return 0;
    for (track = 0; track < TK_MIDI_BINDINGS; track++) {
        const tk_midi_binding_t *b = &s->midi_bind[track];
        if ((b->cc > 127u && b->cc != 255u) || b->port > 2u || b->channel > 16u
            || (b->cc != 255u && midi_reserved_cc(b->cc))) return 0;
    }
    {
        const tk_track_patch_t *a = &s->graft_base;
        if (a->preset != s->preset || a->instrument != PRESETS[s->preset].instrument
            || PRESETS[s->graft_target].instrument != a->instrument
            || (a->instrument == TK_INSTRUMENT_DRUM_KIT && s->graft_amount)
            || a->p.shade > 127u || a->p.fracture > 127u || a->p.drift > 127u || a->p.bloom > 127u
            || a->fx > 1u || a->memory > 127u || a->attack > 127u || a->release > 127u
            || a->lfo_on > 1u || a->lfo_shape > TK_LFO_RAMP
            || a->lfo_rate > 127u || a->lfo_depth > 127u) return 0;
    }
    for (track = 0; track < TK_TRACKS; track++) {
        const tk_track_patch_t *p = &s->track[track];
        if ((s->length[track] != 16u && s->length[track] != 32u && s->length[track] != 64u)
            || !pattern_valid(&s->pattern[track]) || p->preset >= TK_PRESET_COUNT || p->fx > 1u
            || p->p.shade > 127u || p->p.fracture > 127u || p->p.drift > 127u || p->p.bloom > 127u
            || p->memory > 127u || p->attack > 127u || p->release > 127u || p->lfo_on > 1u
            || p->lfo_shape > TK_LFO_RAMP || p->lfo_rate > 127u || p->lfo_depth > 127u
            || p->instrument > TK_INSTRUMENT_FM
            || p->instrument != PRESETS[p->preset].instrument) return 0;
    }
    return 1;
}
static int settings_upgrade_v7(tk_settings_t *s, const tk_settings_v7_t *old)
{
    uint32_t i;
    if (old->schema != 7u || old->preset >= TK_FM_PRESET_FIRST) return 0;
    for (i=0; i<TK_TRACKS; i++) if (old->track[i].preset >= TK_FM_PRESET_FIRST ||
        old->track[i].instrument > TK_INSTRUMENT_DRUM_KIT) return 0;
    memset(s, 0, sizeof *s);
    memcpy(s, old, sizeof *old);
    s->schema = TK_SCHEMA;
    settings_legacy_finish(s);
    return settings_valid(s);
}
static int settings_upgrade_v8(tk_settings_t *s, const tk_settings_v8_t *old)
{
    uint32_t i;
    if (old->schema != 8u || old->preset >= TK_FM_PRESET_FIRST) return 0;
    for (i=0; i<TK_TRACKS; i++) if (old->track[i].preset >= TK_FM_PRESET_FIRST ||
        old->track[i].instrument > TK_INSTRUMENT_DRUM_KIT) return 0;
    memset(s, 0, sizeof *s);
    memcpy(s, old, sizeof *old);
    s->schema = TK_SCHEMA;
    /* Old tail padding overlaps new fields; initialize all new bytes. */
    midi_mapping_defaults(s);
    settings_fm_defaults(s);
    settings_metro_defaults(s);
    return settings_valid(s);
}
static int settings_upgrade_v9(tk_settings_t *s, const tk_settings_v9_t *old)
{
    uint32_t i;
    if (old->schema != 9u || old->preset >= TK_FM_PRESET_FIRST) return 0;
    for (i = 0; i < TK_TRACKS; i++)
        if (old->track[i].preset >= TK_FM_PRESET_FIRST || old->track[i].instrument > TK_INSTRUMENT_DRUM_KIT) return 0;
    memset(s, 0, sizeof *s);
    memcpy(s, old, sizeof *old);
    s->schema = TK_SCHEMA;
    settings_fm_defaults(s);
    settings_metro_defaults(s);
    return settings_valid(s);
}
static int settings_upgrade_v10(tk_settings_t *s, const tk_settings_v10_t *old)
{
    if (old->schema != 10u) return 0;
    memset(s, 0, sizeof *s);
    memcpy(s, old, sizeof *old);
    s->schema = TK_SCHEMA;
    /* Old tail padding overlaps the appended fields; initialize all four. */
    settings_metro_defaults(s);
    s->panel_response = TK_ENCODER_LINEAR;
    return settings_valid(s);
}
static int settings_upgrade_v12(tk_settings_t *s, const void *old)
{
    uint32_t schema; memcpy(&schema, old, sizeof schema);
    if (schema != 12u) return 0;
    memset(s, 0, sizeof *s); memcpy(s, old, TK_SETTINGS_V12_BYTES);
    s->schema = TK_SCHEMA; return settings_valid(s);
}
static int settings_upgrade_v11(tk_settings_t *s, const tk_settings_v11_t *old)
{
    if (old->schema != 11u) return 0;
    memset(s, 0, sizeof *s); memcpy(s, old, sizeof *old);
    s->schema = TK_SCHEMA;
    /* Schema 11's tail padding is not a stored cosmetic choice or mute mask. */
    s->home_visual = 0;
    s->section_mutes = 0;
    return settings_valid(s);
}
static int settings_upgrade_v4(tk_settings_t *s, const tk_settings_v4_t *old)
{
    uint32_t track;
    if (old->schema != 4u || old->track_mute >= 8u || old->preset >= TK_FM_PRESET_FIRST) return 0;
    for (track=0; track<TK_LEGACY_TRACKS; track++) if (old->track[track].preset >= TK_FM_PRESET_FIRST ||
        old->track[track].instrument > TK_INSTRUMENT_DRUM_KIT) return 0;
    settings_defaults(s);
    s->p = old->p; s->octave = old->octave;
    s->preset = old->preset; s->fx = old->fx; s->memory = old->memory;
    s->tempo = old->tempo; s->lfo_rate = old->lfo_rate;
    s->lfo_depth = old->lfo_depth; s->lfo_shape = old->lfo_shape;
    s->lfo_on = old->lfo_on; s->arp_mode = old->arp_mode;
    s->attack = old->attack; s->release = old->release;
    s->track_mute = old->track_mute;
    for (track = 0; track < TK_LEGACY_TRACKS; track++) {
        if (!pattern_upgrade(&s->pattern[track], &old->pattern[track])) return 0;
        s->track[track] = old->track[track];
    }
    settings_legacy_finish(s);
    return settings_valid(s);
}
static int settings_upgrade_v56(tk_settings_t *s, const tk_settings_v6_t *old)
{
    uint32_t track, step, i;
    /* Schemas 5 and 6 share all offsets; only schema 6 owns the final byte. */
    if ((old->schema != 5u && old->schema != 6u) || old->track_mute >= 8u || old->preset >= TK_FM_PRESET_FIRST) return 0;
    for (track=0; track<TK_LEGACY_TRACKS; track++) if (old->track[track].preset >= TK_FM_PRESET_FIRST ||
        old->track[track].instrument > TK_INSTRUMENT_DRUM_KIT) return 0;
    settings_defaults(s);
    s->p = old->p; s->octave = old->octave;
    s->preset = old->preset; s->fx = old->fx; s->memory = old->memory;
    s->tempo = old->tempo; s->lfo_rate = old->lfo_rate;
    s->lfo_depth = old->lfo_depth; s->lfo_shape = old->lfo_shape;
    s->lfo_on = old->lfo_on; s->arp_mode = old->arp_mode;
    s->attack = old->attack; s->release = old->release;
    s->track_mute = old->track_mute;
    for (track = 0; track < TK_LEGACY_TRACKS; track++) {
        tk_pattern_clear(&s->pattern[track]);
        for (step = 0; step < 16u; step++) {
            const tk_pattern_step_v5_t *part = &old->pattern[track].step[step];
            if (part->count > TK_PATTERN_NOTES) return 0;
            s->pattern[track].step[step].count = part->count;
            for (i = 0; i < part->count; i++) {
                if (part->note[i].duration_q4 > 64u) return 0;
                s->pattern[track].step[step].note[i].pitch = part->note[i].pitch;
                s->pattern[track].step[step].note[i].velocity = part->note[i].velocity;
                s->pattern[track].step[step].note[i].duration_q4 = part->note[i].duration_q4;
            }
        }
        s->track[track] = old->track[track];
    }
    s->ghost_mask = old->schema == 6u ? old->ghost_mask : 0u;
    settings_legacy_finish(s);
    return settings_valid(s);
}

static void publish_params(const tk_settings_t *s, uint8_t live_shade)
{
    uint32_t kit = PRESETS[s->preset].instrument == TK_INSTRUMENT_DRUM_KIT;
    tk_parameter_word = (uint32_t)(kit ? s->p.shade : live_shade)
        | ((uint32_t)(kit || s->fx ? s->p.fracture : 0u) << 8)
        | ((uint32_t)s->p.drift << 16)
        | ((uint32_t)(kit && !s->fx ? 64u : s->p.bloom) << 24);
    tk_set_memory(s->memory);
    tk_set_ghost_mask(s->ghost_mask);
    tk_set_fx(s->fx);
    tk_set_envelope(s->attack, s->release);
    tk_set_instrument(PRESETS[s->preset].instrument);
    tk_set_source_instrument(TK_SOURCE_ARP, PRESETS[s->preset].instrument);
    tk_clear_source_patch(TK_SOURCE_ARP);
    tk_set_live_fm_patch(&s->fm[0]);
    tk_set_source_fm_patch(TK_SOURCE_ARP, &s->fm[0]);
    tk_set_mono(s->mono);
}
static void publish_track(const tk_settings_t *s, uint32_t track)
{
    tk_params_t p = s->track[track].p;
    tk_arr_apply_params(track, &p);
    if (!s->track[track].fx) {
        if (s->track[track].instrument == TK_INSTRUMENT_DRUM_KIT) p.bloom = 64u;
        else p.fracture = 0;
    }
    tk_set_source_instrument(TRACK_SOURCE[track], s->track[track].instrument);
    tk_set_source_patch(TRACK_SOURCE[track], &p,
        s->track[track].attack, s->track[track].release);
    tk_set_source_memory(TRACK_SOURCE[track], s->track[track].memory);
    tk_set_source_fm_patch(TRACK_SOURCE[track], &s->fm[track + 1u]);
}
/* Reuse the existing source mailboxes and FM bank. Main mirrors complete
 * DRUM commands before pad note allocation; audio still owns the same six
 * voices. Explicit macro following and the track LFO follow their source,
 * without borrowing source 34's notes, pedals, gain or pitch bend. */
static void publish_live_drum(void)
{
    if (!midi_live_drum_enabled) return;
    tk_source_instrument_control[TK_SOURCE_LIVE_DRUM] = tk_source_instrument_control[TK_SOURCE_DRUM];
    tk_source_patch_word[TK_SOURCE_LIVE_DRUM] = tk_source_patch_word[TK_SOURCE_DRUM];
    tk_source_macro_control[TK_SOURCE_LIVE_DRUM] = tk_source_macro_control[TK_SOURCE_DRUM];
    tk_source_shade_control[TK_SOURCE_LIVE_DRUM] = tk_source_shade_control[TK_SOURCE_DRUM];
    tk_source_memory_control[TK_SOURCE_LIVE_DRUM] = tk_source_memory_control[TK_SOURCE_DRUM];
    tk_source_patch_timing[TK_SOURCE_LIVE_DRUM] = tk_source_patch_timing[TK_SOURCE_DRUM];
}
static void publish_fire_tracks(void)
{
    uint32_t slot;
    for (slot = 0; slot < 4u; slot++) if (midi_fire_mask & (1u << slot)) {
        uint32_t source = TK_SOURCE_FIRE0 + slot, track = TRACK_SOURCE[tk_fire_track[slot]];
        tk_source_instrument_control[source] = tk_source_instrument_control[track];
        tk_source_patch_word[source] = tk_source_patch_word[track];
        tk_source_macro_control[source] = tk_source_macro_control[track];
        tk_source_shade_control[source] = tk_source_shade_control[track];
        tk_source_memory_control[source] = tk_source_memory_control[track];
        tk_source_patch_timing[source] = tk_source_patch_timing[track];
    }
    for (slot = 0; slot < 2u; slot++) if (midi_keyboard_mask & (1u << slot)) {
        uint32_t source = TK_SOURCE_KEYBOARD0 + slot, track = TRACK_SOURCE[tk_keyboard_track[slot]];
        tk_source_instrument_control[source] = tk_source_instrument_control[track];
        tk_source_patch_word[source] = tk_source_patch_word[track];
        tk_source_macro_control[source] = tk_source_macro_control[track];
        tk_source_shade_control[source] = tk_source_shade_control[track];
        tk_source_memory_control[source] = tk_source_memory_control[track];
        tk_source_patch_timing[source] = tk_source_patch_timing[track];
    }
}
/* Main-owned panel references keep an octave change from letting two physical
 * keys with the same resulting pitch stop one another. MIDI emits one pair
 * per pitch while the engine owns the separate panel source. */
static uint8_t panel_note[27], panel_down[27], panel_refs[128], panel_arpeggiated;
static void panel_release(uint32_t key)
{
    uint32_t note;
    if (!panel_down[key]) return;
    panel_down[key] = 0;
    note = panel_note[key];
    if (panel_refs[note] && !--panel_refs[note] && !panel_arpeggiated) {
        tk_note_off(note, 0); midi_send(0x80, note, 0);
    }
}
static void panel_press(uint32_t key, uint32_t note)
{
    panel_release(key);
    panel_note[key] = (uint8_t)note; panel_down[key] = 1;
    if (!panel_refs[note]++ && !panel_arpeggiated) {
        tk_note_on(note, 100, 0); midi_send(0x90, note, 100);
    }
}
/* ARP uses physical panel refs as its pitch list, but the four audio voices
 * should play the pulse rather than a held chord that steals every pulse. */
static void panel_set_arp(uint8_t on)
{
    uint32_t i;
    if (panel_arpeggiated == on) return;
    panel_arpeggiated = on;
    for (i = 0; i < 128u; i++) if (panel_refs[i]) {
        if (on) { tk_note_off(i, 0); midi_send(0x80, i, 0); }
        else { tk_note_on(i, 100, 0); midi_send(0x90, i, 100); }
    }
    if (on) {
        /* HOLD must not leave the old chord under the arpeggiator. */
        tk_all_sound_off(0);
        midi_send(0xB0, 120, 0);
    }
}
static void panel_panic(void)
{
    uint32_t i;
    for (i = 0; i < 128u; i++) {
        if (panel_refs[i] && !panel_arpeggiated) midi_send(0x80, i, 0);
        panel_refs[i] = 0;
    }
    for (i = 0; i < 27u; i++) panel_down[i] = 0;
    midi_send(0xB0, 64, 0); midi_send(0xB0, 120, 0);
    tk_panic();
}
#include "terkinstein_swing.h"
typedef struct {
    uint8_t active, source, pitch, step;
    uint8_t swing, parity;
    uint32_t phase_ms;
    uint32_t start_ms, period, accumulated_q8;
} tk_capture_note_t;
typedef struct {
    uint8_t active, part, pitch;
    uint32_t due_ms, started_ms;
} tk_playing_note_t;
typedef struct {
    tk_arp_t arp;
    uint8_t mode, running, recording, editing;
    uint8_t solo_track; /* Zero means off, 1..TK_TRACKS identify a part. Never saved. */
    uint8_t next_step, shown_step, edit_step, selected, record_length;
    tk_playing_note_t playing[TK_VOICES];
    tk_capture_note_t capture[TK_PATTERN_STEPS * TK_PATTERN_NOTES];
    tk_pattern_t *record_pattern;
    uint32_t next_ms, gate_end_ms, last_step_ms;
    uint32_t solo_seen_ms;
    uint8_t phase_origin[TK_TRACKS], bank_changed;
    uint8_t swing_pair, swing_pending, swing_clock_seen;
    uint32_t swing_clock_ms, swing_clock_period;
} tk_transport_t;
static uint8_t tk_transport_local_step(const tk_transport_t *t,
                                       uint32_t track, uint32_t global, uint32_t length);
static uint32_t transport_capture_segment_q8(const tk_capture_note_t *held,
                                              uint32_t now)
{
    uint32_t elapsed = now - held->start_ms;
    if (elapsed > held->period * TK_PATTERN_STEPS) elapsed = held->period * TK_PATTERN_STEPS;
    if (!held->swing) return (elapsed * 1024u + held->period / 2u) / held->period;
    return tk_swing_position_q8(held->phase_ms + elapsed, held->period, held->swing, held->parity)
        - tk_swing_position_q8(held->phase_ms, held->period, held->swing, held->parity);
}
static void transport_capture_end(tk_transport_t *t, uint32_t source,
                                  uint32_t pitch, uint32_t now)
{
    uint32_t i;
    if (!t->record_pattern) return;
    for (i = 0; i < TK_PATTERN_STEPS * TK_PATTERN_NOTES; i++) {
        tk_capture_note_t *held = &t->capture[i];
        tk_pattern_note_t *note;
        uint32_t total_q8, q4;
        if (!held->active || held->source != source || held->pitch != pitch) continue;
        note = tk_pattern_find(t->record_pattern, held->step, pitch);
        total_q8 = held->accumulated_q8 + transport_capture_segment_q8(held, now);
        if (total_q8 > (uint32_t)t->record_length * 4u * 256u)
            total_q8 = (uint32_t)t->record_length * 4u * 256u;
        q4 = (total_q8 + 128u) / 256u;
        if (!q4) q4 = 1u;
        if (q4 > TK_PATTERN_GATE_Q4) q4 = TK_PATTERN_GATE_Q4;
        if (note && tk_pattern_gate_q4(note) < q4)
            note->duration_q4 = (uint8_t)(q4 >= 255u ? 255u : q4);
        held->active = 0;
    }
}
static void transport_capture_finish(tk_transport_t *t, uint32_t now)
{
    uint32_t i;
    if (!t->record_pattern) return;
    for (i = 0; i < TK_PATTERN_STEPS * TK_PATTERN_NOTES; i++) {
        tk_capture_note_t *held = &t->capture[i];
        if (held->active)
            transport_capture_end(t, held->source, held->pitch, now);
    }
    t->record_pattern = 0;
}
static void transport_capture_retime(tk_transport_t *t, uint32_t new_period,
                                     uint32_t now)
{
    uint32_t i;
    if (!t->record_pattern) return;
    for (i = 0; i < TK_PATTERN_STEPS * TK_PATTERN_NOTES; i++) {
        tk_capture_note_t *held = &t->capture[i];
        if (!held->active) continue;
        held->accumulated_q8 += transport_capture_segment_q8(held, now);
        if (held->accumulated_q8 > TK_PATTERN_GATE_Q4 * 256u)
            held->accumulated_q8 = TK_PATTERN_GATE_Q4 * 256u;
        if (held->swing) held->phase_ms =
            ((held->phase_ms + now - held->start_ms) % (held->period * 2u)) * new_period / held->period;
        held->start_ms = now;
        held->period = new_period;
    }
}
static int transport_capture_start(tk_transport_t *t, uint32_t source,
                                   uint32_t pitch, uint32_t velocity, uint32_t now,
                                   uint32_t period)
{
    tk_pattern_note_t *note;
    uint32_t i;
    if (!t->recording || !t->record_pattern || !period || !t->record_length) return 0;
    transport_capture_end(t, source, pitch, now); /* repeated MIDI note-on */
    if (!tk_pattern_record(t->record_pattern,
        tk_transport_local_step(t, t->selected, t->shown_step, t->record_length), pitch, velocity)) return 0;
    note = tk_pattern_find(t->record_pattern,
        tk_transport_local_step(t, t->selected, t->shown_step, t->record_length), pitch);
    if (t->selected == TK_TRACK_DRUM) return 1; /* one-shot kit stays percussive */
    if (note) note->duration_q4 = 1u;
    for (i = 0; i < TK_PATTERN_STEPS * TK_PATTERN_NOTES; i++) {
        tk_capture_note_t *held = &t->capture[i];
        if (held->active) continue;
        held->active = 1;
        held->source = (uint8_t)source;
        held->pitch = (uint8_t)pitch;
        held->step = tk_transport_local_step(t, t->selected, t->shown_step, t->record_length);
        held->start_ms = now;
        held->period = period;
        held->swing = t->mode == TK_MODE_SEQ ? t->swing_pair : 0u;
        held->parity = t->shown_step & 1u;
        held->phase_ms = now - t->last_step_ms;
        held->accumulated_q8 = 0;
        break;
    }
    return 1;
}
static void transport_release_one(tk_transport_t *t, uint32_t slot)
{
    tk_playing_note_t *held = &t->playing[slot];
    if (!held->active) return;
    tk_note_off(held->pitch, TRACK_SOURCE[held->part]);
    midi_send(TRACK_MIDI_OFF[held->part], held->pitch, 0);
    held->active = 0;
}
static void transport_release_part(tk_transport_t *t, uint32_t part)
{
    uint32_t i;
    for (i = 0; i < TK_VOICES; i++)
        if (t->playing[i].active && t->playing[i].part == part)
            transport_release_one(t, i);
}
static void transport_release(tk_transport_t *t)
{
    uint32_t part;
    for (part = 0; part < TK_TRACKS + 1u; part++) transport_release_part(t, part);
}
/* A remote-controller audition lease. Saved mute flags are never modified. */
#define TK_SOLO_LEASE_MS 3000u
static int transport_solo_set(tk_transport_t *t, const tk_settings_t *s,
                              uint32_t target, uint32_t now)
{
    uint32_t track;
    if (target > TK_TRACKS) return 0;
    t->solo_seen_ms = now;
    if (t->solo_track == (target < TK_TRACKS ? target + 1u : 0u)) return 0;
    for (track = 0; track < TK_TRACKS; track++) {
        int before = t->solo_track ? t->solo_track == track + 1u :
                     !(s->track_mute & (1u << track));
        int after = target < TK_TRACKS ? target == track :
                    !(s->track_mute & (1u << track));
        if (before && !after) {
            transport_release_part(t, track);
            tk_all_sound_off(TRACK_SOURCE[track]);
        }
    }
    t->solo_track = target < TK_TRACKS ? (uint8_t)(target + 1u) : 0u;
    return 1;
}
static int transport_solo_expire(tk_transport_t *t, const tk_settings_t *s, uint32_t now)
{
    return t->solo_track && (uint32_t)(now - t->solo_seen_ms) >= TK_SOLO_LEASE_MS ?
           transport_solo_set(t, s, TK_TRACKS, now) : 0;
}
#include "terkinstein_banks.h"
#include "terkinstein_persistence.h"
#include "terkinstein_arrangement.h"
/* Section muting silences only this track's generated ownership. Live notes,
 * ARP and all other tracks retain their gates and audio state. */
static int transport_section_mute_set(tk_transport_t *t, tk_settings_t *s,
                                      uint32_t track, uint32_t section, uint32_t muted)
{
    int changed = tk_section_mutes_set(&s->section_mutes, track, section, muted);
    if (changed == 1 && muted && t->mode == TK_MODE_SEQ &&
        tk_transport_local_step(t, track, t->shown_step, s->length[track]) / TK_SECTION_STEPS == section) {
        transport_release_part(t, track);
        tk_all_sound_off(TRACK_SOURCE[track]);
    }
    return changed;
}
static int transport_tick(tk_transport_t *t, tk_settings_t *s, uint32_t now);
static void transport_stop(tk_transport_t *t)
{
    /* A Clock immediately followed by Stop may share one USB read batch. Count
     * that last step before stopping so Continue retains the same position on
     * every unit, independent of main-loop/USB delivery timing. Never flush a
     * fault/backlog; expired clock disables itself before the nested stop. */
    if (tk_midi_clock.enabled && tk_midi_clock.due && !tk_midi_clock.fault &&
        t->running && t->mode == TK_MODE_SEQ && tk_banks.settings)
        transport_tick(t, tk_banks.settings, fm1_ms);
    if (t->swing_pending && tk_midi_clock.enabled)
        t->next_step = (uint8_t)((t->next_step + 1u) % TK_PATTERN_STEPS);
    t->swing_pending = 0u;
    t->swing_clock_seen = 0u; /* Continue reuses the last rate, never the stopped gap */
    tk_arr_transport_reset();
    if (tk_banks.settings) {
        uint32_t track;
        for (track = 0; track < TK_TRACKS; track++) { publish_track(tk_banks.settings, track); tk_follow_source_macros(TRACK_SOURCE[track]); }
    }
    transport_capture_finish(t, fm1_ms);
    transport_release(t);
    t->running = t->recording = 0;
    tk_metronome_cancel();
    if (tk_banks_stopped(t)) t->bank_changed = 1;
}
static void transport_start(tk_transport_t *t, uint32_t now)
{
    tk_arr_transport_reset();
    uint32_t track;
    transport_release(t);
    for (track = 0; track < TK_TRACKS; track++) {
        t->phase_origin[track] = 0; tk_banks.head_pending[track] = 0;
    }
    t->running = 1;
    t->next_step = t->shown_step = 0;
    tk_metronome_clock_reset(&metro_clock);
    t->next_ms = t->last_step_ms = now;
    t->swing_pair = t->swing_pending = t->swing_clock_seen = 0u;
    t->swing_clock_period = 0u;
}
static void transport_mode(tk_transport_t *t, uint8_t mode, uint32_t now)
{
    transport_stop(t);
    panel_set_arp(mode == TK_MODE_ARP);
    t->mode = mode;
    t->editing = 0;
    tk_arp_init(&t->arp);
    if (mode != TK_MODE_OFF) transport_start(t, now);
}
static void transport_refresh_arp(tk_transport_t *t)
{
    uint8_t notes[27];
    uint32_t i, n = 0;
    for (i = 0; i < 128u; i++)
        if (panel_refs[i] && n < 27u) notes[n++] = (uint8_t)i;
    tk_arp_set_held(&t->arp, notes, n);
    if (t->mode == TK_MODE_ARP && !n) transport_release_part(t, TK_TRACKS);
}
static int transport_note(tk_transport_t *t, uint32_t part, uint32_t pitch, uint32_t velocity)
{
    uint32_t i;
    if (part > TK_TRACKS) return 0;
    for (i = 0; i < TK_VOICES; i++)
        if (t->playing[i].active && t->playing[i].part == part &&
            t->playing[i].pitch == pitch) transport_release_one(t, i);
    for (i = 0; i < TK_VOICES && t->playing[i].active; i++) {}
    if (i == TK_VOICES) return 0;
    if (!tk_note_on(pitch, velocity, TRACK_SOURCE[part])) return 0;
    t->playing[i].active = 1;
    t->playing[i].part = (uint8_t)part;
    t->playing[i].pitch = (uint8_t)pitch;
    midi_send(TRACK_MIDI_ON[part], pitch, velocity);
    return 1;
}
static void transport_release_due(tk_transport_t *t, uint32_t now)
{
    uint32_t i;
    for (i = 0; i < TK_VOICES; i++) {
        tk_playing_note_t *held = &t->playing[i];
        if (held->active && held->part < TK_TRACKS &&
            ((int32_t)(now - held->due_ms) >= 0 ||
             !tk_note_is_held(held->pitch, TRACK_SOURCE[held->part])))
            transport_release_one(t, i);
    }
}
static uint32_t transport_playing_count(const tk_transport_t *t)
{
    uint32_t i, count = 0;
    for (i = 0; i < TK_VOICES; i++)
        if (t->playing[i].active) count++;
    return count;
}
/* A long chord must not starve every later drum hit. When the voice pool is
 * full, the first new onset in each track may replace one generated note,
 * never a live key. Prefer a track holding several voices, then the target
 * track, then the note due to end soonest. */
static int transport_preempt(tk_transport_t *t, uint32_t target, uint32_t now)
{
    uint32_t i, j, best = TK_VOICES, best_count = 0u, best_same = 0u;
    int32_t best_remaining = 0;
    for (i = 0; i < TK_VOICES; i++) {
        uint32_t count = 0u, same;
        int32_t remaining;
        if (!t->playing[i].active || t->playing[i].part >= TK_TRACKS ||
            t->playing[i].started_ms == now) continue;
        for (j = 0; j < TK_VOICES; j++)
            if (t->playing[j].active && t->playing[j].part == t->playing[i].part)
                count++;
        same = t->playing[i].part == target;
        remaining = (int32_t)(t->playing[i].due_ms - now);
        if (best == TK_VOICES || count > best_count ||
            (count == best_count && same > best_same) ||
            (count == best_count && same == best_same && remaining < best_remaining)) {
            best = i;
            best_count = count;
            best_same = same;
            best_remaining = remaining;
        }
    }
    if (best == TK_VOICES) return 0;
    transport_release_one(t, best);
    return 1;
}
static int transport_has_playing(const tk_transport_t *t, uint32_t part,
                                 uint32_t pitch)
{
    uint32_t i;
    for (i = 0; i < TK_VOICES; i++)
        if (t->playing[i].active && t->playing[i].part == part &&
            t->playing[i].pitch == pitch) return 1;
    return 0;
}
static int transport_part_active(const tk_transport_t *t, uint32_t part)
{
    uint32_t i;
    for (i = 0; i < TK_VOICES; i++)
        if (t->playing[i].active && t->playing[i].part == part) return 1;
    return 0;
}
static void transport_set_deadline(tk_transport_t *t, uint32_t part,
                                   uint32_t pitch, uint32_t now, uint32_t due_ms)
{
    uint32_t i;
    for (i = 0; i < TK_VOICES; i++)
        if (t->playing[i].active && t->playing[i].part == part &&
            t->playing[i].pitch == pitch) {
            t->playing[i].due_ms = due_ms;
            t->playing[i].started_ms = now;
            return;
        }
}
static uint32_t transport_period(const tk_transport_t *t, const tk_settings_t *s)
{
    uint32_t units = 15000u; /* the sequencer always uses sixteenth notes */
    if (t->mode == TK_MODE_ARP) {
        uint8_t division = arp_division_of(s->arp_mode);
        units = division == 0u ? 30000u : division == 2u ? 7500u : 15000u;
    }
    return units / s->tempo; /* settings validation keeps tempo at 40..180 */
}
/* A live BPM/rate change applies relative to the last pulse rather than
 * waiting out a previously scheduled, possibly much slower subdivision. */
static void transport_retime(tk_transport_t *t, const tk_settings_t *s, uint32_t now)
{
    uint32_t period;
    if (!t->running || t->mode != TK_MODE_ARP) return;
    period = transport_period(t, s);
    t->next_ms = (uint32_t)(now - t->last_step_ms) >= period ? now : t->last_step_ms + period;
}
/* Keep already-held sequencer gates in musical step time when BPM changes.
 * The next step and each pending note-off preserve their current phase. */
static uint32_t transport_scale_deadline(uint32_t due, uint32_t now,
                                         uint32_t old_period, uint32_t new_period)
{
    uint32_t remaining;
    if ((int32_t)(now - due) >= 0) return now;
    remaining = due - now;
    return now + (remaining * new_period + old_period / 2u) / old_period;
}
static void transport_retime_seq(tk_transport_t *t, uint32_t old_period,
                                 uint32_t new_period, uint32_t now)
{
    uint32_t i;
    if (!t->running || t->mode != TK_MODE_SEQ || old_period == new_period) return;
    if (tk_midi_clock.enabled && t->swing_pair) return; /* external grid owns its period */
    transport_capture_retime(t, new_period, now);
    t->next_ms = transport_scale_deadline(t->next_ms, now, old_period, new_period);
    for (i = 0; i < TK_VOICES; i++)
        if (t->playing[i].active && t->playing[i].part < TK_TRACKS)
            t->playing[i].due_ms = transport_scale_deadline(
                t->playing[i].due_ms, now, old_period, new_period);
}
/* Millisecond clock belongs to main; the audio callback sees only atomic note
 * commands. Late display work skips a step instead of firing a burst of notes. */
static int transport_tick(tk_transport_t *t, tk_settings_t *s, uint32_t now)
{
    uint32_t period, i, step, interval;
    uint8_t pitch;
    if (tk_clock_expired(now)) { transport_stop(t); return 0; }
    if (!t->running) return 0;
    if (t->mode == TK_MODE_ARP) {
        if ((int32_t)(now - t->gate_end_ms) >= 0)
            transport_release_part(t, TK_TRACKS);
    } else transport_release_due(t, now);
    if (tk_midi_clock.enabled && t->mode == TK_MODE_SEQ) {
        if (!tk_midi_clock.running) return 0;
        if (tk_midi_clock.due) {
            /* An unplayed delayed offbeat cannot cross the next grid boundary.
             * Stop on a stalled main loop instead of releasing a catch-up burst. */
            if (t->swing_pending) { tk_clock_disable(); tk_midi_clock.fault = 1u; transport_stop(t); return 0; }
            tk_midi_clock.due = 0u;
            if (t->swing_clock_seen) t->swing_clock_period = now - t->swing_clock_ms;
            t->swing_clock_seen = 1u; t->swing_clock_ms = now;
            period = t->swing_clock_period ? t->swing_clock_period : transport_period(t, s);
            if (!(t->next_step & 1u)) t->swing_pair = (uint8_t)s->swing;
            t->next_ms = now + ((t->next_step & 1u) ? tk_swing_shift(period, t->swing_pair) : 0u);
            t->swing_pending = 1u;
        }
        if (!t->swing_pending || (int32_t)(now - t->next_ms) < 0) return 0;
        t->swing_pending = 0u;
    } else if ((int32_t)(now - t->next_ms) < 0) return 0;
    if (t->mode == TK_MODE_ARP) transport_release_part(t, TK_TRACKS);
    period = tk_midi_clock.enabled && t->mode == TK_MODE_SEQ && t->swing_pair && t->swing_clock_period ?
        t->swing_clock_period : transport_period(t, s);
    t->last_step_ms = now;
    step = t->next_step;
    if (t->mode == TK_MODE_SEQ && !(step & 1u)) t->swing_pair = (uint8_t)s->swing;
    interval = t->mode == TK_MODE_SEQ ? tk_swing_interval(period, t->swing_pair, step) : period;
    if (t->mode == TK_MODE_SEQ && t->record_pattern) {
        /* Carry REC durations through the same warped grid, with bounded main work. */
        for (i = 0; i < TK_PATTERN_STEPS * TK_PATTERN_NOTES; i++) if (t->capture[i].active &&
            (t->capture[i].swing || t->swing_pair)) {
            t->capture[i].accumulated_q8 += transport_capture_segment_q8(&t->capture[i], now);
            if (t->capture[i].accumulated_q8 > TK_PATTERN_GATE_Q4 * 256u)
                t->capture[i].accumulated_q8 = TK_PATTERN_GATE_Q4 * 256u;
            t->capture[i].start_ms = now; t->capture[i].period = period;
            t->capture[i].swing = t->swing_pair; t->capture[i].parity = step & 1u;
            t->capture[i].phase_ms = 0u;
        }
    }
    t->shown_step = (uint8_t)step;
    t->next_step = (uint8_t)((step + 1u) % TK_PATTERN_STEPS);
    if (t->mode == TK_MODE_ARP) {
        if (tk_arp_next(&t->arp, arp_mode_of(s->arp_mode), &pitch))
            transport_note(t, TK_TRACKS, pitch, 92u);
        t->gate_end_ms = now + period * ARP_GATE_PERCENT[arp_gate_of(s->arp_mode)] / 100u;
    } else if (t->mode == TK_MODE_SEQ) {
        const tk_pattern_step_t *part[TK_TRACKS];
        if (tk_arr_scene_boundary(s, t, step)) {
            t->bank_changed = 1;
            if (!tk_midi_clock.enabled) period = transport_period(t, s);
            interval = tk_swing_interval(period, t->swing_pair, step);
        }
        tk_metronome_clock_configure(&metro_clock, s->metro_beats, s->metro_unit);
        tk_metronome_clock_step(&metro_clock, s->metro_enabled, s->metro_level);
        uint8_t new_admitted[TK_TRACKS] = {0};
        uint32_t track;
        uint32_t live, used, budget;
        if (tk_banks_boundary(s, t, step)) t->bank_changed = 1;
        for (track = 0; track < TK_TRACKS; track++) {
            uint32_t local = tk_transport_local_step(t, track, step, s->length[track]);
            int section_muted = tk_section_muted(s->section_mutes, track, local);
            int condition_passed = tk_arr_step_begin(s, t, track, local);
            if (section_muted && !(local % TK_SECTION_STEPS)) {
                /* A held gate or release tail cannot leak into a silent section.
                 * The engine consumes this source-only fade command atomically. */
                transport_release_part(t, track);
                tk_all_sound_off(TRACK_SOURCE[track]);
            }
            part[track] = !condition_passed || section_muted || (t->solo_track ? t->solo_track != track + 1u :
                           !!(s->track_mute & (1u << track))) ? 0 :
                tk_pattern_read(&s->pattern[track], local);
        }
        live = tk_live_voice_count();
        used = live + transport_playing_count(t);
        budget = used < TK_VOICES ? TK_VOICES - used : 0u;
        /* Share scarce voices across tracks. The first note of each active
         * pattern gets a chance before any track takes a second voice. */
        for (i = 0; i < TK_PATTERN_NOTES; i++) {
            for (track = 0; track < TK_TRACKS; track++) {
                const tk_pattern_note_t *note;
                uint32_t duplicate, gate;
                if (!part[track] || i >= part[track]->count) continue;
                note = &part[track]->note[i];
                duplicate = transport_has_playing(t, track, note->pitch);
                if (!budget && !duplicate) {
                    if (new_admitted[track] || !transport_preempt(t, track, now)) continue;
                    budget++;
                }
                if (transport_note(t, track, note->pitch, note->velocity)) {
                    gate = note->duration_q4 ? tk_swing_gate(period, t->swing_pair, step,
                        tk_pattern_gate_q4(note)) : interval * 75u / 100u;
                    if (!gate) gate = 1u;
                    transport_set_deadline(t, track, note->pitch, now, now + gate);
                    if (!duplicate) {
                        budget--;
                        new_admitted[track] = 1;
                    }
                }
            }
        }
        for (track = 0; track < TK_TRACKS; track++) tk_banks.head_pending[track] = 0;
        if (tk_arr.fill) tk_arr.fill--;
    }
    t->next_ms += interval;
    if ((int32_t)(now - t->next_ms) >= 0) t->next_ms = now + interval;
    return 1;
}
static int transport_visual_slack(const tk_transport_t *t, uint32_t now)
{
    uint32_t i;
    if (!t->running) return 1;
    /* One tile batch is <=512 pixels (~0.7 ms wire time). Reserve enough
     * space for synchronization and USB polling before a generated event. */
    if ((int32_t)(t->next_ms - now) < 3) return 0;
    if (t->mode == TK_MODE_ARP) {
        if (transport_part_active(t, TK_TRACKS) && (int32_t)(t->gate_end_ms - now) < 3) return 0;
    } else for (i = 0; i < TK_VOICES; i++)
        if (t->playing[i].active && (int32_t)(t->playing[i].due_ms - now) < 3) return 0;
    return 1;
}
static uint8_t macro_get(const tk_params_t *p, uint32_t i)
{
    switch (i) {
    case 0: return p->shade;
    case 1: return p->fracture;
    case 2: return p->drift;
    default: return p->bloom;
    }
}
static void macro_set(tk_params_t *p, uint32_t i, uint8_t v)
{
    switch (i) {
    case 0: p->shade = v; break;
    case 1: p->fracture = v; break;
    case 2: p->drift = v; break;
    default: p->bloom = v; break;
    }
}
/* FX CHAMBER edits the existing live patch. No new DSP or persistent record
 * is introduced; frozen tracks retain their captured sound and FX ownership. */
static uint8_t adjust_u8(uint8_t value, int32_t delta, uint32_t lo, uint32_t hi);
static uint8_t *tk_fx_field(tk_settings_t *s, uint32_t knob)
{
    uint32_t instrument = PRESETS[s->preset].instrument;
    if (knob >= 4u) return 0;
    if (instrument == TK_INSTRUMENT_DRUM_KIT)
        return knob == 0u ? &s->p.bloom : knob == 1u ? &s->p.fracture :
            knob == 2u ? &s->p.drift : &s->p.shade;
    if (knob == 0u) return &s->p.fracture;
    if (knob == 1u) return &s->p.shade;
    if (knob == 3u) return &s->lfo_depth;
    return instrument == TK_INSTRUMENT_PAD ? &s->memory :
        instrument == TK_INSTRUMENT_METAL ? &s->p.drift : &s->fm[0].feedback;
}
static int tk_fx_knob(tk_settings_t *s, uint32_t knob, int32_t delta)
{
    uint8_t *field = tk_fx_field(s, knob), next;
    if (!field) return 1;
    next = adjust_u8(*field, delta, 0, 127);
    if (next == *field) return 1;
    graft_bake(s); *field = next;
    if (!knob) s->fx = 1; /* CORROSION/BITE edits follow the existing macro rule. */
    if (knob == 3u && PRESETS[s->preset].instrument != TK_INSTRUMENT_DRUM_KIT && next)
        s->lfo_on = 1;
    return 3;
}
static int tk_fx_encoder(tk_settings_t *s, uint32_t encoder, int32_t delta)
{
    uint8_t next;
    if (PRESETS[s->preset].instrument == TK_INSTRUMENT_DRUM_KIT || encoder > 1u) return 1;
    if (!encoder) next = adjust_u8(s->lfo_rate, delta, 0, 127);
    else next = (uint8_t)(((int32_t)s->lfo_shape + delta % 3 + 3) % 3);
    if (next == (!encoder ? s->lfo_rate : s->lfo_shape)) return 1;
    graft_bake(s);
    if (!encoder) s->lfo_rate = next; else s->lfo_shape = next;
    return 3;
}
static void draw_number(char *s, uint32_t v)
{
    s[0] = (char)('0' + v / 100u);
    s[1] = (char)('0' + v / 10u % 10u);
    s[2] = (char)('0' + v % 10u);
    s[3] = 0;
}
/* Main-only ornament. These small strips share the existing canvas; none
 * allocate a framebuffer or inspect audio-owned timbre history. */
static uint32_t ui_accent_page = ~0u, ui_accent_signature = ~0u, ui_accent_ms;
/* Main-owned display snapshots. Timbre changes still publish immediately,
 * but do not retransmit an unchanged sequencer or diagnostics page. */
typedef struct {
    uint16_t occupied;
    uint8_t length, active, pending, selected, muted, group, editing, edit_step, playing;
} tk_seq_row_view_t;
static struct {
    tk_seq_row_view_t row[TK_TRACKS];
    uint8_t row_valid[TK_TRACKS], controls_valid, status_valid, help_valid, head_valid;
    uint8_t tempo, mask, status, help, global_step, preset, context, swing;
} seq_drawn;
static uint32_t diagnostics_valid, diagnostics_value[5];
typedef struct {
    uint8_t preset, instrument, fx, value[4], rate, shape, motion;
} tk_fx_screen_t;
static tk_fx_screen_t tk_fx_drawn;
static uint32_t tk_fx_drawn_valid;
static uint8_t metro_drawn[5], metro_drawn_valid;
static void ui_thorn_rule(uint32_t y)
{
    uint32_t i;
    cv_begin(224, 12, C_BLACK);
    cv_line(0, 5, 223, 5, C_BAR);
    for (i = 0; i < 7u; i++) {
        int32_t x = (int32_t)i * 32;
        cv_line(x, 5, x + 9, 5, C_ACC);
        cv_line(x + 9, 5, x + 15, 1, C_ACC);
        cv_line(x + 15, 1, x + 19, 10, TK_DIM);
        cv_line(x + 19, 10, x + 25, 5, C_ACC);
        if (!(i & 1u)) cv_line(x + 9, 7, x + 14, 3, TK_GREEN);
    }
    cv_blit(8, y);
}
static void ui_meter(uint32_t y, uint32_t value)
{
    uint32_t i;
    cv_begin(192, 12, C_BLACK);
    for (i = 0; i < 24u; i++) {
        uint16_t c = value && i * 127u / 23u <= value ? C_ACC : C_BAR;
        cv_rect((int32_t)i * 8, 3, 6, 6, c);
        if (c == C_ACC) cv_rect((int32_t)i * 8, 2, 2, 2, TK_GREEN);
    }
    cv_blit(24, y);
}
static void draw_header(uint32_t page)
{
    static const char *const PAGE_NAME[] = {
        "HOME", "MEMORY", "DIAGNOSTICS", "ENVELOPE", "FILTER LFO", "ARPEGGIATOR", "SEQUENCER", "SOUND EDIT", "MIDI CONTROL",
        "FM OPERATOR", "PITCH", "SYNTH GLOBAL", "VOICE MODE", "FX CHAMBER", "METRONOME", "CONTROL FEEL",
        "ARRANGE", "STEP EDIT", "SCENES", "PIANO ROLL"
    };
    /* The startup art identifies the instrument. A compact page label leaves
     * the rest of the display for live controls and their values. */
    draw_text_box(8, 8, 144, &FONT_S,
        tk_panel_edit_track < TK_TRACKS && page == TK_PAGE_PAD ? "TRACK SOUND" :
        PAGE_NAME[page < 20u ? page : 0u], C_ACC, 0);
    if (tk_panel_edit_track < TK_TRACKS || page == TK_PAGE_PAD || page == TK_PAGE_GLO)
        draw_text_box(144, 8, 88, &FONT_S,
            tk_panel_edit_track < TK_TRACKS ?
            TK_SOUND_CONTEXT[tk_panel_edit_track] : "v" APP_VER, TK_DIM, 2);
    ui_thorn_rule(32);
    ui_accent_page = ~0u;
    diagnostics_valid = 0;
    tk_fx_drawn_valid = 0;
    metro_drawn_valid = 0;
    memset(&seq_drawn, 0, sizeof seq_drawn);
}
static void draw_splash(void)
{
    uint16_t ramp[16];
    uint32_t i, y;
    for (i = 0; i < 16u; i++) {
        uint32_t gray = i * 17u;
        ramp[i] = swap16(RGB(gray, gray, gray));
    }
    /* The bitmap stays in flash; LCD DMA must read RAM. Reuse the existing
     * canvas in short strips, synchronizing before each buffer rewrite. */
    for (y = 0; y < TK_SPLASH_HEIGHT; y += 16u) {
        uint32_t h = TK_SPLASH_HEIGHT - y, n;
        if (h > 16u) h = 16u;
        cv_begin(TK_SPLASH_WIDTH, h, C_BLACK);
        n = TK_SPLASH_WIDTH * h;
        for (i = 0; i < n; i++) {
            uint32_t packed = tk_splash_pixels[(y * TK_SPLASH_WIDTH + i) / 2u];
            cv_px[i] = ramp[(i & 1u) ? packed & 15u : packed >> 4];
        }
        cv_blit(0, y);
        platform_poll();
    }
    lcd_sync();
}
static void draw_memory(const tk_settings_t *s)
{
    char value[4];
    uint32_t i;
    if (PRESETS[s->preset].instrument != TK_INSTRUMENT_PAD) {
        draw_text_box(8, 66, 224, &FONT_S, "AFTERIMAGES: PAD ONLY", TK_GREEN, 0);
        draw_text_box(8, 96, 224, &FONT_S, "CHOOSE A PAD WITH PRESETS", TK_DIM, 0);
        draw_text_box(8, 126, 224, &FONT_S, "NO MEMORY FOR THIS ENGINE", TK_DIM, 0);
        draw_text_box(8, 219, 224, &FONT_S, "SEL OR HOME: BACK", TK_DIM, 0);
        return;
    }
    draw_number(value, s->memory);
    draw_text_box(8, 50, 150, &FONT_S, "K1 MEMORY", TK_DIM, 0);
    draw_text_box(168, 50, 64, &FONT_S, value, TK_GREEN, 2);
    draw_number(value, s->p.shade);
    draw_text_box(8, 76, 150, &FONT_S, "K2 DARK FILTER", TK_DIM, 0);
    draw_text_box(168, 76, 64, &FONT_S, value, TK_GREEN, 2);
    draw_number(value, s->p.fracture);
    draw_text_box(8, 102, 150, &FONT_S, "K3 CORROSION", TK_DIM, 0);
    draw_text_box(168, 102, 64, &FONT_S, value, TK_GREEN, 2);
    draw_number(value, s->p.bloom);
    draw_text_box(8, 128, 150, &FONT_S, "K4 BLOOM", TK_DIM, 0);
    draw_text_box(168, 128, 64, &FONT_S, value, TK_GREEN, 2);
    /* Four symbolic timbre cells show the MEMORY amount, not occupancy of
     * the interrupt-owned history. No diagnostic claims are made by this art. */
    cv_begin(224, 38, C_BLACK);
    for (i = 0; i < 4u; i++) {
        int32_t x = 26 + (int32_t)i * 56, reach = 8 + (int32_t)s->memory * 10 / 127;
        cv_line(x - 22, 19, x + 22, 19, C_BAR);
        cv_line(x - reach, 19, x, 3, C_ACC);
        cv_line(x, 3, x + reach, 19, C_ACC);
        cv_line(x + reach, 19, x, 35, TK_DIM);
        cv_line(x, 35, x - reach, 19, TK_DIM);
        cv_line(x - 4, 22, x + 4, 15, s->memory ? TK_GREEN : C_BAR);
        cv_line(x + 2, 12, x + 8, 6, C_BAR);
    }
    cv_blit(8, 155);
    ui_meter(195, s->memory);
    draw_text_box(8, 202, 224, &FONT_S, "EDIT: FORGET OLD TIMBRE", TK_DIM, 0);
    draw_text_box(8, 219, 224, &FONT_S, "SEL: BACK TO HOME", TK_DIM, 0);
}
static void draw_envelope(const tk_settings_t *s, uint32_t hold)
{
    char number[4];
    int32_t peak, end;
    uint32_t i, instrument = PRESETS[s->preset].instrument;
    static const char *const pitched[] = {"K1 ATTACK", "K2 RELEASE", "K3 BLOOM", "K4 PANEL HOLD"};
    static const char *const metal[] = {"K1 ATTACK", "K2 TAIL", "K3 ALLOY", "K4 PANEL HOLD"};
    static const char *const kit[] = {"K1 LEVEL", "K2 DECAY", "K3 TUNE", "K4 PAN"};
    if (instrument == TK_INSTRUMENT_DRUM_KIT)
        draw_text_box(8, 46, 224, &FONT_S, TK_DRUM_NAMES[tk_drum_focus], C_ACC, 0);
    for (i = 0; i < 4u; i++) {
        const char *label = instrument == TK_INSTRUMENT_DRUM_KIT ? kit[i] :
            instrument == TK_INSTRUMENT_METAL ? metal[i] : pitched[i];
        uint32_t value = instrument == TK_INSTRUMENT_DRUM_KIT ?
            ((const uint8_t *)&tk_drum_mix[tk_drum_focus])[i] :
            i == 0u ? s->attack : instrument == TK_INSTRUMENT_METAL ?
            (i == 1u ? s->p.bloom : s->p.drift) : i == 1u ? s->release : s->p.bloom;
        draw_text_box(8, (instrument == TK_INSTRUMENT_DRUM_KIT ? 72u : 50u) + i * 26u, 152, &FONT_S, label, TK_DIM, 0);
        draw_number(number, value);
        draw_text_box(168, (instrument == TK_INSTRUMENT_DRUM_KIT ? 72u : 50u) + i * 26u, 64, &FONT_S,
            i == 3u && instrument != TK_INSTRUMENT_DRUM_KIT ? (hold ? "ON" : "OFF") : number,
            TK_GREEN, 2);
    }
    if (instrument == TK_INSTRUMENT_DRUM_KIT) {
        draw_text_box(8, 181, 224, &FONT_S, "SELECT: CHOOSE DRUM", C_ACC, 0);
        draw_text_box(8, 200, 224, &FONT_S, "64: UNITY / NEUTRAL", TK_DIM, 0);
        draw_text_box(8, 219, 224, &FONT_S, "ENV OR HOME: BACK", TK_DIM, 0);
        return;
    }
    cv_begin(224, 46, C_BLACK);
    peak = 10 + (int32_t)s->attack * 58 / 127;
    end = 148 + (int32_t)(instrument == TK_INSTRUMENT_METAL ? s->p.bloom : s->release) * 68 / 127;
    cv_line(3, 42, 220, 42, C_BAR);
    cv_line(3, 4, 3, 42, C_BAR);
    cv_line(3, 42, peak, 8, TK_GREEN);
    cv_line(peak, 8, 133, 8, TK_GREEN);
    cv_line(133, 8, end, 42, C_ACC);
    cv_line(peak, 12, peak + 4, 19, C_ACC);
    cv_line(128, 8, 135, 1, C_ACC);
    cv_line(135, 1, 138, 15, TK_DIM);
    cv_line(end - 7, 36, end, 42, TK_GREEN);
    if (hold) {
        cv_line(82, 5, 82, 16, C_ACC);
        cv_line(86, 5, 86, 16, C_ACC);
    }
    cv_blit(8, 152);
    draw_text_box(8, 201, 224, &FONT_S, "EDIT: TOGGLE PANEL HOLD", TK_DIM, 0);
    draw_text_box(8, 219, 224, &FONT_S, "ENV OR HOME: BACK", TK_DIM, 0);
}
static void draw_lfo(const tk_settings_t *s, uint8_t live_shade)
{
    char number[4];
    uint32_t i;
    int32_t previous = 0;
    if (PRESETS[s->preset].instrument == TK_INSTRUMENT_DRUM_KIT) {
        draw_text_box(8, 66, 224, &FONT_S, "KIT HAS NO FILTER LFO", TK_GREEN, 0);
        draw_text_box(8, 96, 224, &FONT_S, "ENV: FOUR DRUM CONTROLS", TK_DIM, 0);
        draw_text_box(8, 126, 224, &FONT_S, "CHOOSE A PITCHED PRESET", TK_DIM, 0);
        draw_text_box(8, 219, 224, &FONT_S, "ENV DRUMS / HOME BACK", TK_DIM, 0);
        return;
    }
    draw_text_box(8, 50, 224, &FONT_S, s->lfo_on ? "FILTER MOTION: ON" : "FILTER MOTION: OFF", s->lfo_on ? TK_GREEN : TK_DIM, 0);
    draw_text_box(8, 76, 152, &FONT_S, "K1 RATE", TK_DIM, 0);
    draw_number(number, s->lfo_rate);
    draw_text_box(184, 76, 48, &FONT_S, number, TK_GREEN, 2);
    draw_text_box(8, 102, 152, &FONT_S, "K2 DEPTH", TK_DIM, 0);
    draw_number(number, s->lfo_depth);
    draw_text_box(184, 102, 48, &FONT_S, number, TK_GREEN, 2);
    draw_text_box(8, 128, 112, &FONT_S, "K3 SHAPE", TK_DIM, 0);
    draw_text_box(120, 128, 112, &FONT_S, LFO_NAME[s->lfo_shape], C_ACC, 2);
    draw_text_box(8, 154, 152, &FONT_S, "K4 BASE FILTER", TK_DIM, 0);
    draw_number(number, s->p.shade);
    draw_text_box(184, 154, 48, &FONT_S, number, TK_GREEN, 2);
    cv_begin(224, 35, C_BLACK);
    cv_line(0, 17, 223, 17, C_BAR);
    for (i = 0; i < 56u; i++) {
        uint32_t phase = i % 28u;
        int32_t wave = s->lfo_shape == TK_LFO_PULSE ? (phase < 14u ? 12 : -12) :
            s->lfo_shape == TK_LFO_RAMP ? (int32_t)phase - 14 :
            phase < 7u ? (int32_t)phase * 2 : phase < 21u ? 28 - (int32_t)phase * 2 : (int32_t)phase * 2 - 56;
        int32_t y = 17 - wave * (int32_t)s->lfo_depth / 127;
        if (i) cv_line((int32_t)(i - 1u) * 4, previous, (int32_t)i * 4, y, s->lfo_on ? TK_GREEN : TK_DIM);
        if (!(i % 7u)) cv_line((int32_t)i * 4, 30, (int32_t)i * 4 + 3, 26, C_ACC);
        previous = y;
    }
    cv_blit(8, 176);
    draw_number(number, live_shade);
    draw_text_box(8, 219, 168, &FONT_S,
        tk_panel_edit_track < TK_TRACKS ? "TRACK FILTER" : "LIVE FILTER", TK_DIM, 0);
    draw_text_box(184, 219, 48, &FONT_S, number, TK_GREEN, 2);
}
enum {
    TK_ARP_UI_STATUS = 1u, TK_ARP_UI_ORDER = 2u, TK_ARP_UI_BPM = 4u,
    TK_ARP_UI_RATE = 8u, TK_ARP_UI_GATE = 16u, TK_ARP_UI_HELD = 32u,
    TK_ARP_UI_ALL = 63u
};
typedef struct {
    uint8_t running, order, division, gate, tempo;
    uint16_t held;
} tk_arp_screen_t;
static tk_arp_screen_t arp_screen_state(const tk_settings_t *s, const tk_transport_t *t)
{
    tk_arp_screen_t view;
    view.running = t->running && t->mode == TK_MODE_ARP;
    view.order = arp_mode_of(s->arp_mode);
    view.division = arp_division_of(s->arp_mode);
    view.gate = arp_gate_of(s->arp_mode);
    view.tempo = s->tempo;
    view.held = t->arp.count;
    return view;
}
static uint32_t arp_screen_diff(const tk_arp_screen_t *old, const tk_arp_screen_t *now)
{
    uint32_t fields = 0;
    if (old->running != now->running) fields |= TK_ARP_UI_STATUS;
    if (old->order != now->order) fields |= TK_ARP_UI_ORDER;
    if (old->tempo != now->tempo) fields |= TK_ARP_UI_BPM;
    if (old->division != now->division) fields |= TK_ARP_UI_RATE;
    if (old->gate != now->gate) fields |= TK_ARP_UI_GATE;
    if (old->held != now->held) fields |= TK_ARP_UI_HELD;
    return fields;
}
static void arp_screen_accept(tk_arp_screen_t *old, const tk_arp_screen_t *now, uint32_t fields)
{
    if (fields & TK_ARP_UI_STATUS) old->running = now->running;
    if (fields & TK_ARP_UI_ORDER) old->order = now->order;
    if (fields & TK_ARP_UI_BPM) old->tempo = now->tempo;
    if (fields & TK_ARP_UI_RATE) old->division = now->division;
    if (fields & TK_ARP_UI_GATE) old->gate = now->gate;
    if (fields & TK_ARP_UI_HELD) old->held = now->held;
}
static void draw_arp_values(const tk_arp_screen_t *view, uint32_t fields)
{
    static const char *const DIVISION_NAME[] = { "1/8", "1/16", "1/32" };
    static const char *const GATE_NAME[] = { "25%", "50%", "65%", "75%" };
    char number[4];
    if (fields & TK_ARP_UI_STATUS)
        draw_text_box(8, 46, 224, &FONT_S, view->running ? "PLAYING HELD NOTES" : "STOPPED", view->running ? TK_GREEN : TK_DIM, 0);
    if (fields & TK_ARP_UI_ORDER) {
        uint32_t i;
        draw_text_box(8, 68, 176, &FONT_L, ARP_NAME[view->order], TK_GREEN, 0);
        /* A small note-order emblem sits outside the text update rectangle. */
        cv_begin(44, 28, C_BLACK);
        for (i = 0; i < 6u; i++) {
            uint32_t rank = view->order == TK_ARP_DOWN ? 5u - i :
                view->order == TK_ARP_PINGPONG ? (i < 3u ? i * 2u : 10u - i * 2u) :
                view->order == TK_ARP_OUTSIDE_IN ? (i & 1u ? 5u - i / 2u : i / 2u) :
                view->order == TK_ARP_RANDOM ? (i * 5u + 2u) % 6u : i;
            int32_t x = 2 + (int32_t)i * 7, y = 23 - (int32_t)rank * 4;
            cv_line(x, 25, x, y, C_BAR);
            cv_line(x, y + 3, x + 4, y, C_ACC);
            cv_rect(x + 1, y, 3, 2, TK_GREEN);
        }
        cv_blit(188, 70);
    }
    if (fields & TK_ARP_UI_BPM) {
        draw_number(number, view->tempo);
        draw_text_box(168, 109, 64, &FONT_S, number, TK_GREEN, 2);
    }
    if (fields & TK_ARP_UI_RATE)
        draw_text_box(168, 132, 64, &FONT_S, DIVISION_NAME[view->division], TK_GREEN, 2);
    if (fields & TK_ARP_UI_GATE)
        draw_text_box(168, 155, 64, &FONT_S, GATE_NAME[view->gate], TK_GREEN, 2);
    if (fields & TK_ARP_UI_HELD) {
        draw_number(number, view->held);
        draw_text_box(168, 178, 64, &FONT_S, number, TK_GREEN, 2);
    }
}
static void draw_arp(const tk_arp_screen_t *view)
{
    uint32_t i;
    for (i = 0; i < 4u; i++) {
        uint32_t y = 109u + i * 23u;
        lcd_fill(8, y + 19u, 224, 1, C_BAR);
        lcd_fill(152, y + 3u, 2, 9, C_ACC);
    }
    draw_text_box(8, 109, 140, &FONT_S, "K1 BPM", TK_DIM, 0);
    draw_text_box(8, 132, 140, &FONT_S, "K3 RATE", TK_DIM, 0);
    draw_text_box(8, 155, 140, &FONT_S, "K4 GATE", TK_DIM, 0);
    draw_text_box(8, 178, 140, &FONT_S, "HELD KEYS", TK_DIM, 0);
    draw_text_box(8, 202, 224, &FONT_S, "K2: NOTE ORDER", TK_DIM, 0);
    draw_text_box(8, 222, 224, &FONT_S, "ARP ON/OFF / HOME BACK", TK_DIM, 0);
    draw_arp_values(view, TK_ARP_UI_ALL);
}
static void draw_seq_controls(const tk_settings_t *s)
{
    char number[4];
    if (!seq_drawn.controls_valid) {
        draw_text_box(8, 190, 56, &FONT_S, "K1 BPM", TK_DIM, 0);
        draw_text_box(126, 190, 56, &FONT_S, "K4 MASK", TK_DIM, 0);
    }
    if (!seq_drawn.controls_valid || seq_drawn.tempo != s->tempo) {
        draw_number(number, s->tempo);
        draw_text_box(64, 190, 48, &FONT_S, number, TK_GREEN, 0);
    }
    if (!seq_drawn.controls_valid || seq_drawn.mask != s->ghost_mask) {
        draw_number(number, s->ghost_mask);
        draw_text_box(184, 190, 48, &FONT_S, s->ghost_mask ? number : "OFF", TK_GREEN, 2);
    }
    seq_drawn.tempo = s->tempo; seq_drawn.mask = s->ghost_mask;
    seq_drawn.controls_valid = 1;
}
static uint32_t seq_visible_step(const tk_settings_t *s, const tk_transport_t *t, uint32_t track)
{
    return t->editing && track == t->selected ? t->edit_step : tk_banks.head_pending[track] ? 0u :
        tk_transport_local_step(t, track, t->shown_step, s->length[track]);
}
static void seq_bank_text(char *text, uint32_t step, uint32_t length)
{
    text[0] = 'P'; text[1] = (char)('1' + step / TK_PATTERN_VIEW_STEPS);
    text[2] = '/'; text[3] = (char)('0' + length / TK_PATTERN_VIEW_STEPS); text[4] = 0;
}
static tk_seq_row_view_t seq_row_view(const tk_settings_t *s, const tk_transport_t *t,
                                      uint32_t track)
{
    tk_seq_row_view_t row;
    uint32_t i, view = seq_visible_step(s, t, track);
    memset(&row, 0, sizeof row);
    row.length = s->length[track]; row.active = tk_banks.active[track];
    row.pending = tk_banks.pending[track]; row.selected = track == t->selected;
    row.muted = (s->track_mute >> track) & 1u;
    row.group = (uint8_t)(view / TK_PATTERN_VIEW_STEPS);
    row.editing = t->editing && track == t->selected;
    row.edit_step = row.editing ? t->edit_step : 0u;
    row.playing = t->mode == TK_MODE_SEQ && t->running;
    for (i = 0; i < TK_PATTERN_VIEW_STEPS; i++)
        if (s->pattern[track].step[row.group * TK_PATTERN_VIEW_STEPS + i].count)
            row.occupied |= (uint16_t)(1u << i);
    return row;
}
static void seq_row_accept(const tk_settings_t *s, const tk_transport_t *t, uint32_t track)
{
    seq_drawn.row[track] = seq_row_view(s, t, track);
    seq_drawn.row_valid[track] = 1;
}
static void draw_seq_track(const tk_settings_t *s, const tk_transport_t *t, uint32_t track)
{
    static const char *const NAME[TK_TRACKS] = {"DRUM", "MELODY", "ATMOS", "BASS"};
    uint32_t i, view = seq_visible_step(s, t, track), bank = view / TK_PATTERN_VIEW_STEPS;
    char number[4], page[5], label[12];
    uint32_t n = 0;
    while (NAME[track][n]) { label[n] = NAME[track][n]; n++; }
    label[n++] = ' '; label[n++] = (char)('A' + tk_banks.active[track]);
    if (tk_banks.pending[track] < TK_BANK_COUNT) {
        label[n++] = '>'; label[n++] = (char)('A' + tk_banks.pending[track]);
    }
    label[n] = 0;
    cv_begin(240, 30, C_BLACK);
    cv_text(8, 0, &FONT_S, label, track == t->selected ? TK_GREEN : TK_DIM);
    draw_number(number, s->length[track]);
    cv_text(89, 0, &FONT_S, number, C_ACC);
    seq_bank_text(page, view, s->length[track]);
    cv_text(132, 0, &FONT_S, page, TK_DIM);
    if (s->track_mute & (1u << track)) cv_text(173, 0, &FONT_S, "MUTED", TK_DIM);
    for (i = 0; i < TK_PATTERN_VIEW_STEPS; i++) {
        uint32_t x = 8u + i * 14u, step = bank * TK_PATTERN_VIEW_STEPS + i;
        cv_rect(x, 18, 11, 8, s->pattern[track].step[step].count ?
            (s->track_mute & (1u << track) ? TK_DIM : C_ACC) : C_BAR);
        if (t->mode == TK_MODE_SEQ && t->running && step ==
            tk_transport_local_step(t, track, t->shown_step, s->length[track])) cv_rect(x, 16, 11, 2, TK_GREEN);
        if (t->editing && track == t->selected && step == t->edit_step)
            cv_rect(x, 27, 11, 2, TK_GREEN);
    }
    /* Lane-specific scar ends sit below the cells, outside partial updates. */
    cv_line(8, 29, 218, 29, C_BAR);
    cv_line(218, 29, 225, 25, track == t->selected ? TK_GREEN : C_ACC);
    cv_blit(0, 70u + track * 30u);
    seq_row_accept(s, t, track);
}
static void draw_seq_playhead(const tk_settings_t *s, const tk_transport_t *t,
                              uint32_t old_step, uint32_t new_step);
static void draw_seq(const tk_settings_t *s, const tk_transport_t *t)
{
    uint32_t track, playing = t->mode == TK_MODE_SEQ && t->running;
    uint32_t status = t->mode | (t->running ? 4u : 0u) |
        (t->recording ? 8u : 0u) | (t->editing ? 16u : 0u);
    uint32_t help = tk_banks.importing ? 4u : t->editing ? 1u : t->recording ? 2u :
        !playing && !(t->mode == TK_MODE_ARP && t->running) ? 3u : 0u;
    uint32_t context = t->editing ? t->selected : TK_TRACKS;
    if (!seq_drawn.status_valid || seq_drawn.context != context) {
        /* Only this small label changes on SELECT; no full-page repaint. */
        draw_text_box(144, 8, 88, &FONT_S,
            context < TK_TRACKS ? TK_SOUND_CONTEXT[context] : "", TK_DIM, 2);
        seq_drawn.context = (uint8_t)context;
    }
    if (!seq_drawn.status_valid || seq_drawn.status != status ||
        (t->editing && seq_drawn.preset != s->track[t->selected].preset)) {
        char edit_name[32] = "EDIT: ";
        uint32_t n = 6u, i = 0u;
        const char *name = PRESETS[s->track[t->selected].preset].name;
        while (name[i] && n < sizeof edit_name - 1u) edit_name[n++] = name[i++];
        edit_name[n] = 0;
        draw_text_box(8, 48, 224, &FONT_S,
            t->recording ? "RECORDING SELECTED" : t->editing ? edit_name :
            playing ? "FOUR LOOPS PLAYING" : t->mode == TK_MODE_ARP && t->running ?
            "ARP ON / PLAY: START SEQ" : "STOPPED / PLAY TO START", playing ? TK_GREEN : C_ACC, 0);
        seq_drawn.status = (uint8_t)status; seq_drawn.status_valid = 1;
        seq_drawn.preset = s->track[t->selected].preset;
    }
    for (track = 0; track < TK_TRACKS; track++) {
        tk_seq_row_view_t row = seq_row_view(s, t, track);
        if (!seq_drawn.row_valid[track] || memcmp(&seq_drawn.row[track], &row, sizeof row))
            draw_seq_track(s, t, track);
    }
    /* A timbre edit can coincide with a transport tick. The existing main
     * path leaves that tick for draw_seq, so advance only the small strips
     * after accepting any changed row rather than omitting its playhead. */
    if (playing && seq_drawn.head_valid && seq_drawn.global_step != t->shown_step)
        draw_seq_playhead(s, t, seq_drawn.global_step, t->shown_step);
    seq_drawn.global_step = t->shown_step; seq_drawn.head_valid = 1;
    draw_seq_controls(s);
    if (!seq_drawn.help_valid || seq_drawn.help != help || seq_drawn.swing != s->swing) {
        draw_text_box(8, 208, 224, &FONT_S, help == 4u ? "SONG IMPORT: CONTROLS LOCKED" :
            help == 2u ? "SELECT / ALG: REC LOCK" : "SELECT TRACK / ALG BANK", TK_DIM, 0);
        draw_text_box(8, 224, 224, &FONT_S, help == 1u ? "K2 LEN K3 STEP REC ERASE" :
            help == 2u ? "K2 LEN / K3 STEP: REC LOCK" : help == 4u ? "WAIT FOR LOAD TO FINISH" :
            "K2 LEN  K3 SWING", TK_DIM, 0);
        if (help == 0u || help == 3u) {
            char value[4]; draw_number(value, s->swing);
            draw_text_box(184, 224, 48, &FONT_S, value, TK_GREEN, 2);
        }
        seq_drawn.swing = (uint8_t)s->swing;
        seq_drawn.help = (uint8_t)help; seq_drawn.help_valid = 1;
    }
}
static void draw_seq_playhead(const tk_settings_t *s, const tk_transport_t *t,
                              uint32_t old_step, uint32_t new_step)
{
    uint32_t track;
    if (t->mode != TK_MODE_SEQ || !t->running || old_step == new_step) return;
    for (track = 0; track < TK_TRACKS; track++) {
        uint32_t y = 86u + track * 30u;
        uint32_t old_local = tk_transport_local_step(t, track, old_step, s->length[track]);
        uint32_t new_local = tk_transport_local_step(t, track, new_step, s->length[track]);
        uint32_t bank = seq_visible_step(s, t, track) / TK_PATTERN_VIEW_STEPS;
        if (old_local / TK_PATTERN_VIEW_STEPS != new_local / TK_PATTERN_VIEW_STEPS &&
            !(t->editing && track == t->selected)) {
            uint32_t i;
            char page[5];
            /* Only the cells and bank label change once per 16 steps. */
            lcd_fill(8, y, 224, 2, C_BLACK);
            for (i = 0; i < TK_PATTERN_VIEW_STEPS; i++) {
                uint32_t step = bank * TK_PATTERN_VIEW_STEPS + i;
                uint16_t color = s->pattern[track].step[step].count ?
                    (s->track_mute & (1u << track) ? TK_DIM : C_ACC) : C_BAR;
                lcd_fill(8u + i * 14u, y + 2u, 11, 8, color);
            }
            /* The last cell shares one pixel with the lane scar tip. Restore
             * it after a partial group paint, matching draw_seq_track. */
            lcd_fill(225u, y + 9u, 1u, 1u, track == t->selected ? TK_GREEN : C_ACC);
            seq_bank_text(page, new_local, s->length[track]);
            draw_text_box(132, y - 16u, 40, &FONT_S, page, TK_DIM, 0);
        } else if (old_local / TK_PATTERN_VIEW_STEPS == bank)
            lcd_fill(8u + (old_local % TK_PATTERN_VIEW_STEPS) * 14u, y, 11u, 2u, C_BLACK);
        if (new_local / TK_PATTERN_VIEW_STEPS == bank)
            lcd_fill(8u + (new_local % TK_PATTERN_VIEW_STEPS) * 14u, y, 11u, 2u, TK_GREEN);
        if (seq_drawn.row_valid[track]) seq_row_accept(s, t, track);
    }
    seq_drawn.global_step = t->shown_step; seq_drawn.head_valid = 1;
}
static void draw_seq_cell(const tk_settings_t *s, const tk_transport_t *t, uint32_t track, uint32_t step)
{
    if (step / TK_PATTERN_VIEW_STEPS != seq_visible_step(s, t, track) / TK_PATTERN_VIEW_STEPS) return;
    uint16_t color = s->pattern[track].step[step].count ?
        (s->track_mute & (1u << track) ? TK_DIM : C_ACC) : C_BAR;
    lcd_fill(8u + (step % TK_PATTERN_VIEW_STEPS) * 14u, 88u + track * 30u, 11u, 8u, color);
    if (step % TK_PATTERN_VIEW_STEPS == TK_PATTERN_VIEW_STEPS - 1u)
        lcd_fill(225u, 95u + track * 30u, 1u, 1u, track == t->selected ? TK_GREEN : C_ACC);
    if (seq_drawn.row_valid[track]) seq_row_accept(s, t, track);
}
static void draw_counter_value(uint32_t y, uint32_t value)
{
    char number[11];
    uint32_t end = 10u, start = end;
    number[end] = 0;
    do {
        number[--start] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value);
    draw_text_box(128, y, 80, &FONT_S, number + start, TK_GREEN, 2);
}
static void draw_diagnostics(void)
{
    static const char *const label[5] = {"LAST RENDER", "MAX RENDER", "LATE BLOCKS", "BLOCK BUDGET", "VOICES + TAILS"};
    uint32_t i, value[5], voices = 0, repaint_voices;
    for (i = 0; i < TK_VOICES; i++)
        if ((tk_voice[i].command & TK_GATE) || tk_voice[i].tail) voices++;
#ifndef TERKINSTEIN_HOST
    value[0] = tk_render_last_ticks / FM1_TICKS_PER_US;
    value[1] = tk_render_max_ticks / FM1_TICKS_PER_US;
    value[2] = tk_render_late_blocks;
#else
    value[0] = value[1] = value[2] = 0;
    if (!diagnostics_valid) {
        draw_text_box(8, 50, 224, &FONT_S, "LAST RENDER   HOST N/A", TK_DIM, 0);
        draw_text_box(8, 76, 224, &FONT_S, "MAX RENDER    HOST N/A", TK_DIM, 0);
        draw_text_box(8, 102, 224, &FONT_S, "LATE BLOCKS   HOST N/A", TK_DIM, 0);
    }
#endif
    value[3] = TK_RENDER_BUDGET_US; value[4] = voices;
    repaint_voices = !diagnostics_valid || diagnostics_value[4] != voices;
    for (i = 0; i < 5u; i++) {
#ifdef TERKINSTEIN_HOST
        if (i < 3u) continue;
#endif
        if (!diagnostics_valid) {
            draw_text_box(8, 50u + i * 26u, 120, &FONT_S, label[i], TK_DIM, 0);
            draw_text_box(212, 50u + i * 26u, 20, &FONT_S, i == 2u || i == 4u ? "" : "us", TK_DIM, 0);
        }
        if (!diagnostics_valid || diagnostics_value[i] != value[i])
            draw_counter_value(50u + i * 26u, value[i]);
    }
    if (repaint_voices) {
        cv_begin(224, 24, C_BLACK);
        for (i = 0; i < TK_VOICES; i++) {
            int32_t x = 4 + (int32_t)i * (224 / TK_VOICES);
            uint16_t c = i < voices ? TK_GREEN : C_BAR;
            cv_line(x, 18, x + 10, 2, C_ACC);
            cv_line(x + 10, 2, x + 19, 18, TK_DIM);
            cv_line(x + 4, 14, x + 14, 14, c);
            cv_rect(x + 9, 8, 3, 9, c);
        }
        cv_blit(8, 175);
    }
    if (!diagnostics_valid) {
        draw_text_box(8, 200, 224, &FONT_S, "APP ONLY: USB/DAC EXCLUDED", TK_DIM, 0);
        draw_text_box(8, 219, 224, &FONT_S, "SEL: FEEL  EDIT: MIDI", TK_DIM, 0);
    }
    memcpy(diagnostics_value, value, sizeof value); diagnostics_valid = 1;
}
static tk_fx_screen_t tk_fx_screen(const tk_settings_t *s)
{
    tk_fx_screen_t view;
    uint32_t kit;
    memset(&view, 0, sizeof view);
    view.preset = s->preset; view.instrument = PRESETS[s->preset].instrument;
    view.fx = s->fx; kit = view.instrument == TK_INSTRUMENT_DRUM_KIT;
    view.value[0] = kit ? s->p.bloom : s->p.fracture;
    view.value[1] = kit ? s->p.fracture : s->p.shade;
    view.value[2] = kit || view.instrument == TK_INSTRUMENT_METAL ? s->p.drift :
        view.instrument == TK_INSTRUMENT_PAD ? s->memory : s->fm[0].feedback;
    view.value[3] = kit ? s->p.shade : s->lfo_depth;
    view.rate = s->lfo_rate; view.shape = s->lfo_shape; view.motion = s->lfo_on;
    return view;
}
static void tk_fx_meter(uint32_t y, uint32_t value, uint32_t kit, uint32_t enabled)
{
    uint32_t i;
    cv_begin(224, 5, C_BLACK);
    for (i = 0; i < 24u; i++) {
        uint16_t color = value && i * 127u / 23u <= value ?
            enabled ? C_ACC : TK_DIM : C_BAR;
        cv_rect((int32_t)i * 9, 1, 7, 3, color);
    }
    if (kit) cv_line(108, 0, 108, 4, TK_GREEN); /* the neutral-center scar */
    cv_blit(8, y);
}
static void draw_fx(const tk_settings_t *s)
{
    static const char *const label[4][4] = {
        {"K1 CORROSION", "K2 DARK FILTER", "K3 AFTERIMAGE", "K4 FILTER MOTION"},
        {"K1 CORROSION", "K2 DARK FILTER", "K3 ALLOY", "K4 FILTER MOTION"},
        {"K1 BITE", "K2 WIRE", "K3 DECAY", "K4 BODY"},
        {"K1 CORROSION", "K2 DARK FILTER", "K3 FEEDBACK", "K4 FILTER MOTION"}
    };
    tk_fx_screen_t now = tk_fx_screen(s);
    uint32_t i, kit = now.instrument == TK_INSTRUMENT_DRUM_KIT;
    uint32_t labels = !tk_fx_drawn_valid || now.instrument != tk_fx_drawn.instrument;
    char number[4];
    if (!tk_fx_drawn_valid || now.preset != tk_fx_drawn.preset) {
        char title[32];
        const char *context = TK_SOUND_CONTEXT[tk_panel_edit_track];
        uint32_t n = 0u, at = 0;
        while (context[n]) { title[n] = context[n]; n++; }
        title[n++] = ':'; title[n++] = ' ';
        while (PRESETS[now.preset].name[at] && n + 1u < sizeof title)
            title[n++] = PRESETS[now.preset].name[at++];
        title[n] = 0;
        draw_text_box(8, 44, 224, &FONT_S, title, C_ACC, 0);
    }
    if (labels || now.fx != tk_fx_drawn.fx ||
        ((!kit && !now.value[0] != !tk_fx_drawn.value[0]) ||
         (kit && ((now.value[0] < 64u) != (tk_fx_drawn.value[0] < 64u) ||
                  (now.value[0] == 64u) != (tk_fx_drawn.value[0] == 64u))))) {
        const char *status = kit ? !now.fx ? "BITE BYPASS" : now.value[0] == 64u ?
            "BITE ON / NEUTRAL" : now.value[0] < 64u ? "BITE ON / SAND" : "BITE ON / BURN" :
            !now.fx ? "CORROSION BYPASS" : now.value[0] ? "CORROSION ON" : "CORROSION ON / CLEAN";
        draw_text_box(8, 64, 224, &FONT_S, status, now.fx ? TK_GREEN : TK_DIM, 0);
    }
    for (i = 0; i < 4u; i++) {
        uint32_t y = 84u + i * 24u;
        if (labels) draw_text_box(8, y, 168, &FONT_S, label[now.instrument][i], TK_DIM, 0);
        if (labels || now.value[i] != tk_fx_drawn.value[i] ||
            (!i && now.fx != tk_fx_drawn.fx)) {
            draw_number(number, now.value[i]);
            draw_text_box(184, y, 48, &FONT_S, number, !i && !now.fx ? TK_DIM : TK_GREEN, 2);
            tk_fx_meter(y + 18u, now.value[i], kit, i || now.fx);
        }
    }
    if (labels || (!kit && (now.rate != tk_fx_drawn.rate || now.shape != tk_fx_drawn.shape ||
        now.motion != tk_fx_drawn.motion || !now.value[3] != !tk_fx_drawn.value[3]))) {
        char motion[24] = "RATE 0.00HZ TRI OFF";
        static const char *const shape[3] = {"TRI", "PUL", "RMP"};
        uint32_t period = tk_lfo_period_ms(now.rate);
        uint32_t hz100 = (100000u + period / 2u) / period;
        motion[5] = (char)('0' + hz100 / 100u);
        motion[7] = (char)('0' + hz100 / 10u % 10u);
        motion[8] = (char)('0' + hz100 % 10u);
        for (i = 0; i < 3u; i++) motion[12u + i] = shape[now.shape][i];
        if (now.motion) {
            if (now.value[3]) { motion[16] = 'O'; motion[17] = 'N'; motion[18] = 0; }
            else { motion[16] = 'Z'; motion[17] = 'E'; motion[18] = 'R'; motion[19] = 'O'; motion[20] = 0; }
        }
        draw_text_box(8, 184, 224, &FONT_S, kit ? "64 = NEUTRAL SHAPING" : motion, TK_GREEN, 0);
    }
    if (labels) {
        draw_text_box(8, 204, 224, &FONT_S, kit ? "BITE: SAND <64 >64 BURN" : "SELECT RATE / ALGO SHAPE", TK_DIM, 0);
        draw_text_box(8, 223, 224, &FONT_S, kit ? "FX/SEL: BITE  HOME: BACK" : "EDIT MOTION / HOME BACK", C_ACC, 0);
    }
    tk_fx_drawn = now; tk_fx_drawn_valid = 1;
}
static void draw_patch_keyboard(uint32_t base, uint32_t lit)
{
    uint32_t i;
    cv_begin(240, 20, C_BLACK);
    for (i = 0; i < 27u; i++) {
        uint32_t note = base + i;
        int is_black = (0x54Au >> (note % 12u)) & 1u;
        cv_rect(8 + (int32_t)i * 8, is_black ? 2 : 0, 7, is_black ? 12 : 18,
            lit >> i & 1u ? C_ACC : is_black ? C_BAR : TK_DIM);
    }
    cv_blit(0, 177);
}
static void draw_preset_title(const tk_settings_t *s)
{
    uint32_t j = 0;
    const char *name = PRESETS[s->preset].name;
    char title[24];
    title[0] = (char)('0' + (factory_position(s->preset) + 1u) / 10u);
    title[1] = (char)('0' + (factory_position(s->preset) + 1u) % 10u);
    title[2] = ' ';
    while (j < sizeof title - 4u && name[j]) { title[3u + j] = name[j]; j++; }
    if (s->preset == 24u || s->preset == 51u) {
        const char *suffix = " OLD"; uint32_t k=0;
        while (suffix[k] && j < sizeof title - 4u) title[3u+j++] = suffix[k++];
    }
    title[3u + j] = '\0';
    draw_text_box(8, 48, 176, &FONT_S, title, C_ACC, 0);
}
static uint32_t home_preset_drawn = ~0u, home_modified_drawn = ~0u, home_status_drawn = ~0u;
static uint32_t home_graft_drawn = ~0u;
static void draw_home_reset(uint32_t ready)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(8, 16, 224, &FONT_S, "RESET DEFAULTS?", C_ACC, 2);
    draw_text_box(8, 66, 224, &FONT_S, "REPLACES SONG + SOUNDS", TK_DIM, 1);
    draw_text_box(8, 88, 224, &FONT_S, "RESETS LOCAL SETUP", TK_DIM, 1);
    draw_text_box(8, 126, 224, &FONT_S, "RAM ONLY UNTIL SAVE", TK_DIM, 1);
    draw_text_box(8, 168, 224, &FONT_S,
        ready ? "PRESS SEL TO RESET" : "RELEASE ALL BUTTONS", ready ? TK_GREEN : C_ACC, 1);
    draw_text_box(8, 204, 224, &FONT_S, "HOME: CANCEL", TK_GREEN, 1);
}
enum {
    TK_SAVE_NOTICE_NONE, TK_SAVE_NOTICE_WAIT_SOUND, TK_SAVE_NOTICE_WAIT_STOP,
    TK_SAVE_NOTICE_WAIT_IMPORT, TK_SAVE_NOTICE_SAVED, TK_SAVE_NOTICE_FAILED
};
static int tk_save_notice_finished(uint32_t notice)
{
    return notice == TK_SAVE_NOTICE_SAVED || notice == TK_SAVE_NOTICE_FAILED;
}
/* Main-only, drawn once per state. A successful flash return is the only
 * route to SAVED; waiting for notes/tails never looks like confirmation. */
static void draw_save_notice(uint32_t notice)
{
    int saved = notice == TK_SAVE_NOTICE_SAVED;
    int failed = notice == TK_SAVE_NOTICE_FAILED;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(8, 12, 224, &FONT_S, "SAVE", C_ACC, 1);
    lcd_fill(32, 42, 176, 2, saved ? TK_GREEN : C_ACC);
    draw_text_box(8, 76, 224, &FONT_L,
        saved ? "SAVED" : failed ? "NOT SAVED" : "WAITING",
        saved ? TK_GREEN : C_ACC, 1);
    draw_text_box(8, 133, 224, &FONT_S,
        saved ? "ALL 16 PATTERNS" : failed ? "WRITE DID NOT COMPLETE" :
        notice == TK_SAVE_NOTICE_WAIT_STOP ? "STOP PLAYBACK / RECORD" :
        notice == TK_SAVE_NOTICE_WAIT_IMPORT ? "FINISH SONG TRANSFER" : "RELEASE KEYS + PEDALS",
        C_WHITE, 1);
    draw_text_box(8, 156, 224, &FONT_S,
        saved ? "SOUNDS + SETTINGS" : failed ? "EDITS STILL IN MEMORY" :
        notice == TK_SAVE_NOTICE_WAIT_SOUND ? "WAIT FOR SOUND TO END" : "THEN WAIT FOR SILENCE",
        C_WHITE, 1);
    draw_text_box(8, 204, 224, &FONT_S,
        saved ? "KEY / BUTTON / KNOB:" : failed ? "PRESS SAVE TO RETRY" : "SAVE WILL FINISH",
        TK_DIM, 1);
    draw_text_box(8, 222, 224, &FONT_S,
        saved ? "CONTINUE" : failed ? "HOME: RETURN" : "WHEN FULLY SILENT", TK_DIM, 1);
}
static void draw_home(const tk_settings_t *s, uint32_t modified, uint32_t saving, uint32_t save_failed)
{
    uint32_t pending = tk_panel_edit_track != tk_panel_audition_track;
    uint32_t status = (saving ? 1u : save_failed ? 2u : 0u) | (pending << 2);
    if (home_preset_drawn != s->preset) {
        draw_preset_title(s); home_preset_drawn = s->preset;
    }
    if (home_modified_drawn != modified) {
        draw_text_box(192, 48, 40, &FONT_S, modified ? "*" : "", TK_GREEN, 2);
        home_modified_drawn = modified;
    }
    if (home_status_drawn != status) {
        draw_text_box(8, 219, 224, &FONT_S, saving ? "SAVE WAIT: RELEASE/HOLD" :
            save_failed ? "SAVE FAILED" : pending ? "NEW SOUND: NEXT CHORD" : tk_panel_edit_track < TK_TRACKS ?
            "EDIT: TRACK / HOME: LIVE" : "EDIT: SOUND CONTROLS", TK_DIM, 0);
        home_status_drawn = status;
    }
    if (home_graft_drawn != s->graft_amount) {
        lcd_fill(8, 67, 224, 2, C_BLACK);
        if (s->graft_amount) lcd_fill(8, 67, s->graft_amount * 224u / 127u, 2, TK_GREEN);
        home_graft_drawn = s->graft_amount;
    }
}
static void draw_sound_edit(const tk_settings_t *s, const tk_transport_t *t, uint32_t modified,
                            uint32_t saving, uint32_t save_failed, uint32_t base, uint32_t lit)
{
    uint32_t i, kit = PRESETS[s->preset].instrument == TK_INSTRUMENT_DRUM_KIT;
    char number[4], status[] = "OCT+0 MEM000 FX+ L- A- S-";
    char graft[29] = "GRAFT 000 ";
    draw_text_box(8, 8, 144, &FONT_S, kit ? "SCRAP FORGE" : "SOUND EDIT", C_ACC, 0);
    draw_preset_title(s);
    draw_text_box(192, 48, 40, &FONT_S, modified ? "*" : "", TK_GREEN, 2);
    lcd_fill(8, 67, 224, 1, C_BAR);
    lcd_fill(8, 67, kit ? 48u : 24u, 1, C_ACC);
    for (i = 0; i < 4u; i++) {
        uint32_t v = macro_get(&s->p, i), y = 76u + i * 20u;
        uint32_t segment;
        draw_text_box(8, y, 100, &FONT_S, kit ? FORGE_NAME[i] : MACRO_NAME[i], TK_DIM, 0);
        draw_number(number, v);
        draw_text_box(200, y, 32, &FONT_S, number, TK_GREEN, 2);
        cv_begin(80, 8, C_BLACK);
        for (segment = 0; segment < 16u; segment++) {
            uint16_t color = v && segment * 127u / 15u <= v ? C_ACC : C_BAR;
            cv_rect((int32_t)segment * 5, 2, 4, 5, color);
        }
        cv_line((int32_t)v * 77 / 127, 0, (int32_t)v * 77 / 127 + 2, 2, TK_GREEN);
        if (kit) cv_line(39, 0, 39, 7, TK_DIM); /* original sound at 64 */
        cv_blit(112, y + 4u);
    }
    if (kit) {
        draw_text_box(8, 158, 224, &FONT_S, "64:CENTER FX:BITE BYPASS", TK_GREEN, 0);
    } else {
        uint32_t j = 0;
        const char *name = PRESETS[s->graft_target].name;
        draw_number(number, s->graft_amount);
        graft[6] = number[0]; graft[7] = number[1]; graft[8] = number[2];
        while (j < sizeof graft - 11u && name[j]) { graft[10u + j] = name[j]; j++; }
        graft[10u + j] = '\0';
        draw_text_box(8, 158, 224, &FONT_S, graft, TK_GREEN, 0);
    }
    lcd_fill(8, 175, 224, 1, C_BAR);
    if (!kit && s->graft_amount)
        lcd_fill(8, 175, s->graft_amount * 224u / 127u, 1, TK_GREEN);
    draw_patch_keyboard(base, lit);
    draw_text_box(8, 200, 224, &FONT_S, kit ? "EDIT: BACK TO SOUND" :
        "SELECT:MIX ALGO:TARGET", TK_DIM, 0);
    status[3] = s->octave < 0 ? '-' : '+';
    status[4] = (char)('0' + (s->octave < 0 ? -s->octave : s->octave));
    draw_number(number, s->memory);
    status[9] = number[0]; status[10] = number[1]; status[11] = number[2];
    status[15] = s->fx ? '+' : '-'; status[18] = s->lfo_on ? '+' : '-';
    status[21] = t->mode == TK_MODE_ARP ? (t->running ? '+' : '~') : '-';
    status[24] = t->mode == TK_MODE_SEQ ? (t->running ? '+' : '~') : '-';
    draw_text_box(8, 219, 224, &FONT_S, saving ? "SAVE WAIT: RELEASE/HOLD" : save_failed ? "SAVE FAILED" : status, TK_DIM, 0);
}

/* A tiny moving stitch at the header's torn edge. Main calls this only after
 * a page is drawn and while transport_visual_slack permits an LCD transfer.
 * Four updates/sec, 192 pixels/update; inactive pages settle after one draw.
 * ARP/SEQ playheads keep their existing geometry and remain authoritative. */
static void ui_accent_tick(uint32_t page, const tk_settings_t *s,
                           const tk_transport_t *t, uint32_t now_ms,
                           uint32_t lit, uint8_t live_shade)
{
    uint32_t active = !!lit || t->running;
    uint32_t phase, signature, strength;
    int32_t x;
    if (page == TK_PAGE_PAD) return;
    if (page == TK_PAGE_LFO) active = s->lfo_on;
    if (page == TK_PAGE_ARP) active = t->running && t->arp.count;
    if (page == TK_PAGE_SEQ) active = t->running;
    if (page == TK_PAGE_MEMORY) active = !!lit && s->memory;
    if (ui_accent_page == page && now_ms - ui_accent_ms < 250u) return;
    phase = active ? (now_ms / 250u) % 6u : 0u;
    strength = page == TK_PAGE_LFO ? live_shade : page == TK_PAGE_MEMORY ? s->memory :
        page == TK_PAGE_SOUND_EDIT ? s->graft_amount : active ? 127u : 0u;
    signature = phase | (active << 4) | (strength << 8);
    if (ui_accent_page == page && ui_accent_signature == signature) return;
    cv_begin(24, 8, C_BLACK);
    cv_line(0, 4, 23, 4, C_BAR);
    cv_line(2, 4, 6, 1, C_ACC);
    cv_line(6, 1, 9, 6, TK_DIM);
    cv_line(9, 6, 14, 3, C_ACC);
    cv_line(14, 3, 20, 5, TK_DIM);
    x = 1 + (int32_t)phase * 4;
    cv_line(x, 6, x + 2, 2, active ? TK_GREEN : C_ACC);
    if (strength > 64u) cv_pset(x + 1, 1, TK_GREEN);
    cv_blit(208, 34);
    ui_accent_page = page; ui_accent_signature = signature; ui_accent_ms = now_ms;
}

static uint8_t adjust_u8(uint8_t value, int32_t delta, uint32_t lo, uint32_t hi)
{
    int32_t next = (int32_t)value + (delta > 127 ? 127 : delta < -127 ? -127 : delta);
    return (uint8_t)(next < (int32_t)lo ? lo : next > (int32_t)hi ? hi : next);
}

/* SEQ gives each encoder one role: SELECT navigates tracks, ALGORITHM chooses
 * A-D patterns. Step entry follows the selected capture; overview keeps LIVE.
 * Navigation never starts play.
 * Bank changes use the same track-boundary rules as remote controllers. */
static int tk_seq_encoder(tk_settings_t *s, tk_transport_t *t,
                          uint32_t encoder, int32_t delta)
{
    uint32_t track = t->selected, current;
    uint8_t next;
    if (encoder > 1u || !delta || tk_banks.importing || t->recording) return 1;
    if (!encoder) {
        next = (uint8_t)(((int32_t)track + delta % TK_TRACKS + TK_TRACKS) % TK_TRACKS);
        if (next == track) return 1;
        t->selected = next; t->edit_step %= s->length[next];
        return 5;
    }
    current = tk_banks.pending[track] < TK_BANK_COUNT ?
        tk_banks.pending[track] : tk_banks.active[track];
    next = (uint8_t)(((int32_t)current + delta % TK_BANK_COUNT + TK_BANK_COUNT) % TK_BANK_COUNT);
    if (next == current) return 1;
    if (next == tk_banks.active[track]) {
        tk_banks.pending[track] = TK_BANK_NONE;
        tk_banks.revision = (uint16_t)((tk_banks.revision + 1u) & 16383u);
        return 5;
    }
    if (t->running && t->mode == TK_MODE_SEQ) {
        tk_banks.pending[track] = next;
        tk_banks.revision = (uint16_t)((tk_banks.revision + 1u) & 16383u);
        return 5;
    }
    return tk_bank_activate(s, t, track, next, t->next_step) ? 7 : 1;
}

static int tk_encoder_alias_inactive(uint32_t page)
{
    return page == TK_PAGE_MEMORY || page == TK_PAGE_ENV || page == TK_PAGE_LFO
        || page == TK_PAGE_ARP || page == TK_PAGE_MIDI || page == TK_PAGE_PITCH
        || page == TK_PAGE_GLOBAL || page == TK_PAGE_VOICE || page == TK_PAGE_GLO
        || page == TK_PAGE_METRONOME || page == TK_PAGE_CONTROLS;
}

static void draw_control_feel(const tk_settings_t *s)
{
    uint32_t accelerated = s->panel_response == TK_ENCODER_ADAPTIVE;
    draw_text_box(8, 49, 224, &FONT_S, "FM-1 CONTROL OPTIONS", C_ACC, 0);
    draw_text_box(8, 78, 128, &FONT_S, "K1 RESPONSE", TK_DIM, 0);
    draw_text_box(136, 78, 96, &FONT_S, accelerated ? "ACCEL" : "LINEAR", TK_GREEN, 2);
    cv_begin(224, 66, C_BLACK);
    cv_line(12, 55, 213, 55, TK_DIM);
    cv_line(12, 7, 12, 55, TK_DIM);
    if (accelerated) {
        cv_line(13, 43, 78, 43, C_ACC);
        cv_line(78, 43, 78, 31, C_ACC);
        cv_line(78, 31, 142, 31, C_ACC);
        cv_line(142, 31, 142, 7, TK_GREEN);
        cv_line(142, 7, 213, 7, TK_GREEN);
    } else cv_line(13, 43, 213, 43, TK_GREEN);
    cv_blit(8, 99);
    draw_text_box(8, 171, 224, &FONT_S, accelerated ? "SLOW 1X / QUICK 2X-4X" : "ONE CLICK = ONE UNIT", TK_GREEN, 0);
    draw_text_box(8, 189, 112, &FONT_S, "K2 HOME", TK_DIM, 0);
    draw_text_box(120, 189, 112, &FONT_S,
        tk_home_visual_is_rift(s->home_visual) ? "THORN RIFT" : "HOMUNCULUS", TK_GREEN, 2);
    draw_text_box(8, 207, 224, &FONT_S, "SEL: GLO / SAVE: REMEMBER", TK_DIM, 0);
    draw_text_box(8, 223, 224, &FONT_S,
        "K3-K4 UNUSED / HOME BACK",
        TK_DIM, 0);
}

static void draw_metronome(const tk_settings_t *s)
{
    static const char *const LABEL[] = {"K1 BPM", "K2 CLICK LEVEL", "K3 BEATS", "K4 BEAT UNIT"};
    uint8_t now[5] = {s->tempo, s->metro_level, s->metro_beats, s->metro_unit, s->metro_enabled};
    char number[4], meter[6];
    uint32_t i, at = 0;
    if (!metro_drawn_valid || memcmp(now + 2, metro_drawn + 2, 3u)) {
        if (s->metro_beats >= 10u) meter[at++] = '1';
        meter[at++] = (char)('0' + s->metro_beats % 10u);
        meter[at++] = '/'; meter[at++] = (char)('0' + s->metro_unit); meter[at] = 0;
        draw_text_box(8, 52, 144, &FONT_S, s->metro_enabled ? "CLICK: ON" : "CLICK: OFF", s->metro_enabled ? TK_GREEN : TK_DIM, 0);
        draw_text_box(168, 52, 64, &FONT_S, meter, C_ACC, 2);
    }
    for (i = 0; i < 4u; i++) {
        uint32_t y = 82u + i * 27u;
        if (!metro_drawn_valid) draw_text_box(8, y, 160, &FONT_S, LABEL[i], TK_DIM, 0);
        if (!metro_drawn_valid || now[i] != metro_drawn[i]) {
            draw_number(number, now[i]);
            draw_text_box(184, y, 48, &FONT_S, number, TK_GREEN, 2);
        }
    }
    if (!metro_drawn_valid) {
        draw_text_box(8, 198, 224, &FONT_S, "SEL ON/OFF / HOME BACK", TK_DIM, 0);
        draw_text_box(8, 219, 224, &FONT_S, "FOLLOWS SEQ PLAY / REC", TK_DIM, 0);
    }
    memcpy(metro_drawn, now, sizeof now); metro_drawn_valid = 1;
}

#include "terkinstein_arrangement_ui.h"
#include "terkinstein_piano.h"
#include "terkinstein_piano_ui.h"
static void draw_midi_controls(const tk_settings_t *s)
{
    char number[4], route[] = "PORT BOTH  CHANNEL ALL";
    const tk_midi_binding_t *binding = &s->midi_bind[midi_focus < TK_MIDI_BINDINGS ? midi_focus : 0u];
    draw_text_box(8, 52, 224, &FONT_S, MIDI_TARGET[midi_focus], C_ACC, 0);
    if (midi_focus == TK_MIDI_BINDINGS) {
        draw_number(number, s->midi_bend_range);
        draw_text_box(8, 82, 224, &FONT_L, number, TK_GREEN, 1);
        draw_text_box(8, 126, 224, &FONT_S, "SEMITONES: DEFAULT FOR ALL", TK_DIM, 0);
        draw_text_box(8, 151, 224, &FONT_S, "RPN CAN SET EACH CHANNEL", TK_GREEN, 0);
    } else {
        draw_number(number, binding->cc == 255u ? 0u : binding->cc);
        draw_text_box(8, 82, 224, &FONT_L, binding->cc == 255u ? "OFF" : number, TK_GREEN, 1);
        if (binding->port != 2u) {
            const char *name = binding->port ? "TRS " : "USB ";
            memcpy(route + 5, name, 4u);
        }
        if (binding->channel != 16u) {
            draw_number(number, binding->channel + 1u);
            memcpy(route + 19, number, 3u);
        }
        draw_text_box(8, 126, 224, &FONT_S, route, TK_DIM, 0);
        draw_text_box(8, 151, 224, &FONT_S, midi_learning ? "MOVE A CC CONTROL TO LEARN" :
            "EDIT LEARN / SEL CLEAR", midi_learning ? TK_GREEN : TK_DIM, 0);
    }
    draw_text_box(8, 176, 224, &FONT_S, "K1 TARGET / K2 VALUE", TK_DIM, 0);
    draw_text_box(8, 197, 224, &FONT_S, midi_focus == TK_MIDI_BINDINGS ?
        "K3 / K4: NOT USED FOR BEND" : "K3 PORT / K4 CHANNEL", TK_DIM, 0);
    draw_text_box(8, 219, 224, &FONT_S, "GLO OR HOME: BACK", TK_DIM, 0);
}

#include "terkinstein_panel.h"

/* Page-local primary controls. Bit 0 consumes the turn, bit 1 marks a saved
 * edit, bit 2 requests a value redraw. Navigation and HOLD are not saved edits.
 * Keep unsupported processors and diagnostics out of the hidden macro path. */
static int tk_page_knob(tk_settings_t *s, tk_transport_t *t, uint32_t page,
                       uint32_t knob, int32_t delta, uint32_t *hold,
                       uint8_t *live_shade, tk_lfo_t *lfo)
{
    uint32_t instrument = PRESETS[s->preset].instrument;
    uint8_t *field = 0, next, old;
    (void)live_shade; (void)lfo;
    if (knob > 3u) return 1;
    { int action = tk_arr_page_knob(s, t, page, knob, delta); if (action) return action; }
    if (page == TK_PAGE_CONTROLS) {
        if (tk_banks.importing) return 1;
        if (knob == 1u) {
            uint32_t current = tk_home_visual_is_rift(s->home_visual);
            next = adjust_u8((uint8_t)current, delta, 0, 1);
            if (next == current) return 1;
            s->home_visual = next ? TK_HOME_VISUAL_RIFT : 0u;
            return 7;
        }
        if (knob) return 1;
        next = adjust_u8(s->panel_response, delta, TK_ENCODER_LINEAR, TK_ENCODER_ADAPTIVE);
        if (next == s->panel_response) return 1;
        s->panel_response = next;
        return 7;
    }
    if (page == TK_PAGE_METRONOME) {
        if (tk_banks.importing) return 1;
        if (!knob) {
            uint32_t period = transport_period(t, s);
            next = adjust_u8(s->tempo, delta, 40, 180);
            if (next == s->tempo) return 1;
            s->tempo = next;
            if (t->mode == TK_MODE_ARP) transport_retime(t, s, fm1_ms);
            else transport_retime_seq(t, period, transport_period(t, s), fm1_ms);
        } else {
            field = knob == 1u ? &s->metro_level : knob == 2u ? &s->metro_beats : &s->metro_unit;
            next = knob == 3u ? (uint8_t)(4u << adjust_u8(*field == 8u, delta, 0, 1)) :
                adjust_u8(*field, delta, knob == 2u ? 1u : 0u, knob == 2u ? 12u : 127u);
            if (next == *field) return 1;
            *field = next;
            tk_metronome_clock_configure(&metro_clock, s->metro_beats, s->metro_unit);
            if (!s->metro_enabled || !s->metro_level) tk_metronome_cancel();
        }
        return 7;
    }
    if (page == TK_PAGE_GLO) return 1;
    if (page == TK_PAGE_MEMORY) {
        if (instrument != TK_INSTRUMENT_PAD) return 1;
        field = knob == 0u ? &s->memory : knob == 1u ? &s->p.shade :
            knob == 2u ? &s->p.fracture : &s->p.bloom;
    } else if (page == TK_PAGE_ENV) {
        if (instrument == TK_INSTRUMENT_DRUM_KIT) {
            if (tk_banks.importing) return 1;
            field = (uint8_t *)&tk_drum_mix[tk_drum_focus] + knob;
            next = adjust_u8(*field, delta, 0, 127);
            if (next == *field) return 1;
            *field = next; tk_set_drum_mix(tk_drum_focus, &tk_drum_mix[tk_drum_focus]); tk_arr_changed(); return 7;
        }
        else if (knob == 3u) {
            next = adjust_u8((uint8_t)*hold, delta, 0, 1);
            if (next == *hold) return 1;
            *hold = next; tk_sustain(0, next ? 127u : 0u);
            midi_send(0xB0, 64, next ? 127u : 0u);
            return 5;
        } else if (instrument == TK_INSTRUMENT_METAL)
            field = knob == 0u ? &s->attack : knob == 1u ? &s->p.bloom : &s->p.drift;
        else field = knob == 0u ? &s->attack : knob == 1u ? &s->release : &s->p.bloom;
    } else if (page == TK_PAGE_LFO) {
        if (instrument == TK_INSTRUMENT_DRUM_KIT) return 1;
        if (knob == 2u) {
            next = (uint8_t)(((int32_t)s->lfo_shape + delta % 3 + 3) % 3);
            if (next == s->lfo_shape) return 1;
            graft_bake(s); s->lfo_shape = next; return 7;
        }
        field = knob == 0u ? &s->lfo_rate : knob == 1u ? &s->lfo_depth : &s->p.shade;
    } else if (page == TK_PAGE_ARP) {
        uint8_t mode = arp_mode_of(s->arp_mode), division = arp_division_of(s->arp_mode);
        uint8_t gate = arp_gate_of(s->arp_mode), packed;
        uint32_t count;
        if (!knob) {
            next = adjust_u8(s->tempo, delta, 40, 180);
            if (next == s->tempo) return 1;
            s->tempo = next; transport_retime(t, s, fm1_ms); return 7;
        }
        count = knob == 1u ? TK_ARP_MODE_COUNT : knob == 2u ? TK_ARP_DIV_COUNT : TK_ARP_GATE_COUNT;
        old = knob == 1u ? mode : knob == 2u ? division : gate;
        next = (uint8_t)(((int32_t)old + delta % (int32_t)count + (int32_t)count) % (int32_t)count);
        if (next == old) return 1;
        packed = arp_pack(knob == 1u ? next : mode, knob == 2u ? next : division,
                          knob == 3u ? next : gate);
        s->arp_mode = packed;
        if (knob == 2u) transport_retime(t, s, fm1_ms);
        return 7;
    } else if (page == TK_PAGE_SEQ) {
        uint32_t track = t->selected, period;
        if (tk_banks.importing) return 1;
        if (!knob) {
            next = adjust_u8(s->tempo, delta, 40, 180);
            if (next == s->tempo) return 1;
            period = transport_period(t, s); s->tempo = next;
            if (t->mode == TK_MODE_ARP) transport_retime(t, s, fm1_ms);
            else transport_retime_seq(t, period, transport_period(t, s), fm1_ms);
            return 7;
        }
        if (knob == 1u) {
            uint8_t index = s->length[track] == 64u ? 2u : s->length[track] == 32u ? 1u : 0u;
            next = (uint8_t)(16u << adjust_u8(index, delta, 0, 2));
            if (t->recording || next == s->length[track]) return 1;
            transport_release_part(t, track); tk_all_sound_off(TRACK_SOURCE[track]);
            s->length[track] = next; t->edit_step %= next; return 7;
        }
        if (knob == 2u) {
            if (!t->editing) {
                if (t->recording) return 1;
                next = adjust_u8((uint8_t)s->swing, delta, 0u, 100u);
                if (next == s->swing) return 1;
                s->swing = next; return 7;
            }
            if (t->recording) return 1;
            next = (uint8_t)(((int32_t)t->edit_step + delta % s->length[track]
                + s->length[track]) % s->length[track]);
            if (next == t->edit_step) return 1;
            t->edit_step = next; return 5;
        }
        next = adjust_u8(s->ghost_mask, delta, 0, 127);
        if (next == s->ghost_mask) return 1;
        s->ghost_mask = next; return 7;
    } else if (page == TK_PAGE_MIDI) {
        tk_midi_binding_t *binding;
        if (!knob) {
            next = adjust_u8(midi_focus, delta, 0, TK_MIDI_BINDINGS);
            if (next == midi_focus) return 1;
            midi_focus = next; midi_learning = 0; return 5;
        }
        if (midi_focus == TK_MIDI_BINDINGS) {
            if (knob != 1u) return 1;
            next = adjust_u8(s->midi_bend_range, delta, 0, 24);
            if (next == s->midi_bend_range) return 1;
            tk_default_bend(s, next); return 7;
        }
        binding = &s->midi_bind[midi_focus];
        old = knob == 1u ? binding->cc : knob == 2u ? binding->port : binding->channel;
        next = knob == 1u ? midi_next_cc(old, delta) : adjust_u8(old, delta, 0, knob == 2u ? 2u : 16u);
        if (next == old) return 1;
        if (knob == 1u) binding->cc = next;
        else if (knob == 2u) binding->port = next;
        else binding->channel = next;
        midi_binding_unique(s, midi_focus); midi_learning = midi_latch = 0;
        return 7;
    } else return 0;
    next = adjust_u8(*field, delta, 0, 127);
    if (next == *field) return 1;
    graft_bake(s); *field = next;
    if (page == TK_PAGE_MEMORY && knob == 2u) s->fx = 1;
    if (page == TK_PAGE_ENV && instrument == TK_INSTRUMENT_DRUM_KIT && knob == 3u) s->fx = 1;
    if (page == TK_PAGE_LFO && knob == 1u && next) s->lfo_on = 1;
    return 7;
}

/* Only continuous sound fields receive acceleration. Navigation, clocks and
 * enumerated choices stay one detent per existing increment. Fire/USB/TRS
 * commands never pass through this physical-panel helper. */
static const uint8_t *tk_knob_continuous_field(const tk_settings_t *s,
    uint32_t page, uint32_t knob)
{
    uint32_t instrument = PRESETS[s->preset].instrument;
    if (knob > 3u) return 0;
    if (page == TK_PAGE_PAD || page == TK_PAGE_SOUND_EDIT)
        return knob == 0u ? &s->p.shade : knob == 1u ? &s->p.fracture :
            knob == 2u ? &s->p.drift : &s->p.bloom;
    if (page == TK_PAGE_MEMORY)
        return instrument != TK_INSTRUMENT_PAD ? 0 :
            knob == 0u ? &s->memory : knob == 1u ? &s->p.shade :
            knob == 2u ? &s->p.fracture : &s->p.bloom;
    if (page == TK_PAGE_ENV) {
        if (instrument == TK_INSTRUMENT_DRUM_KIT)
            return knob == 0u ? &s->p.drift : knob == 1u ? &s->p.shade :
                knob == 2u ? &s->p.fracture : &s->p.bloom;
        if (knob == 3u) return 0;
        return knob == 0u ? &s->attack : knob == 2u ?
            (instrument == TK_INSTRUMENT_METAL ? &s->p.drift : &s->p.bloom) :
            (instrument == TK_INSTRUMENT_METAL ? &s->p.bloom : &s->release);
    }
    if (page == TK_PAGE_LFO)
        return instrument == TK_INSTRUMENT_DRUM_KIT || knob == 2u ? 0 :
            knob == 0u ? &s->lfo_rate : knob == 1u ? &s->lfo_depth : &s->p.shade;
    if (page == TK_PAGE_FX) {
        if (instrument == TK_INSTRUMENT_DRUM_KIT)
            return knob == 0u ? &s->p.bloom : knob == 1u ? &s->p.fracture :
                knob == 2u ? &s->p.drift : &s->p.shade;
        return knob == 0u ? &s->p.fracture : knob == 1u ? &s->p.shade :
            knob == 3u ? &s->lfo_depth : instrument == TK_INSTRUMENT_PAD ? &s->memory :
            instrument == TK_INSTRUMENT_METAL ? &s->p.drift : &s->fm[0].feedback;
    }
    if (page == TK_PAGE_OPERATOR)
        return instrument != TK_INSTRUMENT_FM || !knob ? 0 :
            knob == 1u ? &s->fm[0].op[tk_operator_focus].level :
            knob == 2u ? &s->fm[0].op[tk_operator_focus].attack : &s->fm[0].op[tk_operator_focus].decay;
    if (page == TK_PAGE_PITCH)
        return instrument == TK_INSTRUMENT_FM && knob == 1u ? &s->fm[0].pitch_time : 0;
    if (page == TK_PAGE_GLOBAL)
        return instrument == TK_INSTRUMENT_FM && !knob ? &s->fm[0].feedback : 0;
    if (page == TK_PAGE_VOICE && knob == 3u)
        return instrument == TK_INSTRUMENT_METAL ? &s->p.bloom :
            instrument == TK_INSTRUMENT_DRUM_KIT ? &s->p.drift : &s->release;
    if (page == TK_PAGE_SEQ && knob == 3u) return &s->ghost_mask;
    if (page == TK_PAGE_METRONOME && knob == 1u) return &s->metro_level;
    return 0;
}
static int32_t tk_physical_knob(tk_encoder_response_t *state, const tk_settings_t *s,
    uint32_t page, uint32_t knob, int32_t delta, uint32_t now)
{
    const uint8_t *field = tk_knob_continuous_field(s, page, knob);
    uint32_t context = page | ((uint32_t)s->preset << 8) |
        ((uint32_t)tk_operator_focus << 16) | ((uint32_t)s->panel_response << 24);
    if (state->context != context || !field || tk_banks.importing ||
        (field && (*field == 0u || *field == 127u))) state->valid = 0;
    state->context = context;
    if (!field || tk_banks.importing) return delta;
    return tk_encoder_response(state, context, s->panel_response, delta, now);
}

static void capture_midi_release(tk_transport_t *t, uint32_t source,
                                 uint32_t pitch, uint32_t now)
{
    uint32_t i, capture_source = source + 26u;
    if (!tk_note_is_held(pitch, source)) {
        transport_capture_end(t, capture_source, pitch, now);
        return;
    }
    for (i = 0; i < TK_PATTERN_STEPS * TK_PATTERN_NOTES; i++)
        if (t->capture[i].active && t->capture[i].source == capture_source
            && t->capture[i].pitch == pitch) t->capture[i].active = 2u;
}
static uint32_t midi_capture_allowed(const tk_transport_t *t, uint32_t source)
{
    if (source >= TK_SOURCE_KEYBOARD0 && source < TK_SOURCE_KEYBOARD0 + 2u)
        return t->selected == tk_keyboard_track[source - TK_SOURCE_KEYBOARD0];
    if (source >= TK_SOURCE_FIRE0 && source < TK_SOURCE_FIRE0 + 4u)
        return t->selected == tk_fire_track[source - TK_SOURCE_FIRE0];
    return !midi_live_drum_enabled ||
        (source == TK_SOURCE_LIVE_DRUM ? t->selected == TK_TRACK_DRUM : t->selected != TK_TRACK_DRUM);
}
static void midi_live_drum_reset(tk_transport_t *t, const tk_settings_t *s)
{
    uint32_t i;
    tk_all_sound_off(TK_SOURCE_LIVE_DRUM); tk_reset_controllers(TK_SOURCE_LIVE_DRUM);
    tk_volume(TK_SOURCE_LIVE_DRUM, 127u); tk_brightness(TK_SOURCE_LIVE_DRUM, 64u);
    tk_texture(TK_SOURCE_LIVE_DRUM, 64u); tk_set_bend_range(TK_SOURCE_LIVE_DRUM, s->midi_bend_range);
    midi_rpn_msb[TK_SOURCE_LIVE_DRUM] = midi_rpn_lsb[TK_SOURCE_LIVE_DRUM] = 127u;
    for (i = 0; i < TK_PATTERN_STEPS * TK_PATTERN_NOTES; ++i)
        if (t->capture[i].active && t->capture[i].source == TK_SOURCE_LIVE_DRUM + 26u)
            transport_capture_end(t, TK_SOURCE_LIVE_DRUM + 26u, t->capture[i].pitch, fm1_ms);
}
static void midi_live_drum_set(tk_transport_t *t, const tk_settings_t *s, uint32_t enabled)
{
    enabled = !!enabled;
    if (enabled) midi_live_drum_lease_ms = fm1_ms;
    if (midi_live_drum_enabled == enabled) return;
    /* A route change cannot carry old channel-16 notes or pedal captures into
     * the other sound. Sequencer DRUM and all other live owners stay intact. */
    if (enabled) {
        tk_all_sound_off(16u); tk_reset_controllers(16u);
        midi_rpn_msb[16u] = midi_rpn_lsb[16u] = 127u;
    }
    midi_live_drum_reset(t, s);
    midi_live_drum_enabled = (uint8_t)enabled;
    publish_live_drum();
}
static void midi_live_drum_expire(tk_transport_t *t, const tk_settings_t *s, uint32_t now)
{
    if (midi_live_drum_enabled && now - midi_live_drum_lease_ms >= TK_PAD_ROUTE_LEASE_MS)
        midi_live_drum_set(t, s, 0u);
}
static void midi_fire_set(tk_transport_t *t, const tk_settings_t *s, uint32_t mask, const uint8_t *tracks)
{
    uint32_t slot, i;
    if (mask) midi_fire_lease_ms = fm1_ms;
    for (slot = 0; slot < 4u; slot++) {
        uint32_t source = TK_SOURCE_FIRE0 + slot, bit = 1u << slot;
        if ((midi_fire_mask & bit) == (mask & bit) && tk_fire_track[slot] == tracks[slot]) continue;
        tk_all_sound_off(source); tk_reset_controllers(source);
        tk_volume(source,127u); tk_brightness(source,64u); tk_texture(source,64u);
        tk_set_bend_range(source,s->midi_bend_range);
        for (i = 0; i < TK_PATTERN_STEPS * TK_PATTERN_NOTES; i++)
            if (t->capture[i].active && t->capture[i].source == source + 26u)
                transport_capture_end(t,source + 26u,t->capture[i].pitch,fm1_ms);
        if ((mask & bit) && !(midi_fire_mask & bit)) {
            tk_all_sound_off(12u+slot); tk_reset_controllers(12u+slot);
        }
        tk_fire_track[slot] = tracks[slot];
    }
    midi_fire_mask = (uint8_t)mask;
    publish_fire_tracks();
}
static void midi_fire_disable(tk_transport_t *t, const tk_settings_t *s)
{
    uint8_t tracks[4]; uint32_t slot;
    for (slot = 0; slot < 4u; slot++) tracks[slot] = (uint8_t)tk_fire_track[slot];
    midi_fire_set(t,s,0u,tracks);
}
static void midi_fire_expire(tk_transport_t *t, const tk_settings_t *s, uint32_t now)
{
    if (midi_fire_mask && now - midi_fire_lease_ms >= TK_PAD_ROUTE_LEASE_MS) midi_fire_disable(t,s);
}
static void midi_keyboard_set(tk_transport_t *t, const tk_settings_t *s, uint32_t mask, const uint8_t *routes)
{
    uint32_t slot, i;
    if (mask) midi_keyboard_lease_ms = fm1_ms;
    for (slot = 0; slot < 2u; slot++) {
        uint32_t source = TK_SOURCE_KEYBOARD0 + slot, bit = 1u << slot;
        uint32_t channel = routes[slot * 2u] - 1u, track = routes[slot * 2u + 1u];
        if ((midi_keyboard_mask & bit) == (mask & bit) &&
            midi_keyboard_channel[slot] == channel && tk_keyboard_track[slot] == track) continue;
        tk_all_sound_off(source); tk_reset_controllers(source);
        tk_volume(source,127u); tk_brightness(source,64u); tk_texture(source,64u);
        tk_set_bend_range(source,s->midi_bend_range);
        midi_rpn_msb[source] = midi_rpn_lsb[source] = 127u;
        for (i = 0; i < TK_PATTERN_STEPS * TK_PATTERN_NOTES; i++)
            if (t->capture[i].active && t->capture[i].source == source + 26u)
                transport_capture_end(t,source + 26u,t->capture[i].pitch,fm1_ms);
        if ((mask & bit) && (!(midi_keyboard_mask & bit) || midi_keyboard_channel[slot] != channel)) {
            tk_all_sound_off(channel + 1u); tk_reset_controllers(channel + 1u);
            midi_rpn_msb[channel + 1u] = midi_rpn_lsb[channel + 1u] = 127u;
        }
        midi_keyboard_channel[slot] = (uint8_t)channel;
        tk_keyboard_track[slot] = track;
    }
    midi_keyboard_mask = (uint8_t)mask;
    publish_fire_tracks();
}
static void midi_keyboard_disable(tk_transport_t *t, const tk_settings_t *s)
{
    uint8_t routes[4]; uint32_t slot;
    for (slot = 0; slot < 2u; slot++) {
        routes[slot * 2u] = midi_keyboard_channel[slot] + 1u;
        routes[slot * 2u + 1u] = (uint8_t)tk_keyboard_track[slot];
    }
    midi_keyboard_set(t,s,0u,routes);
}
static void midi_keyboard_expire(tk_transport_t *t, const tk_settings_t *s, uint32_t now)
{
    if (midi_keyboard_mask && now - midi_keyboard_lease_ms >= TK_PAD_ROUTE_LEASE_MS)
        midi_keyboard_disable(t,s);
}
static void capture_midi_pedals(tk_transport_t *t, uint32_t now)
{
    uint32_t i;
    if (!t->recording || !t->record_pattern) return;
    for (i = 0; i < TK_PATTERN_STEPS * TK_PATTERN_NOTES; i++) {
        const tk_capture_note_t *held = &t->capture[i];
        if (held->active == 2u && ((held->source >= 27u && held->source <= 58u)
            || held->source == TK_SOURCE_LIVE_DRUM + 26u
            || (held->source >= TK_SOURCE_FIRE0 + 26u && held->source < TK_SOURCE_KEYBOARD0 + 28u))
            && !tk_note_is_held(held->pitch, held->source - 26u))
            transport_capture_end(t, held->source, held->pitch, now);
    }
}
static void midi_port_reset(tk_transport_t *t, const tk_settings_t *s, uint32_t port)
{
    uint32_t channel, i;
    if (!port) {
        if (tk_midi_clock.enabled) { tk_clock_disable(); transport_stop(t); }
        midi_live_drum_set(t, s, 0u);
        midi_fire_disable(t,s);
        midi_keyboard_disable(t,s);
        transport_solo_set(t, s, TK_TRACKS, fm1_ms);
    }
    for (channel = 0; channel < 16u; channel++) {
        uint32_t source = 1u + port * 16u + channel;
        tk_all_sound_off(source); tk_reset_controllers(source);
        tk_volume(source, 127u); tk_brightness(source, 64u); tk_texture(source, 64u);
        tk_set_bend_range(source, s->midi_bend_range);
        midi_rpn_msb[source] = midi_rpn_lsb[source] = 127u;
        for (i = 0; i < TK_PATTERN_STEPS * TK_PATTERN_NOTES; i++)
            if (t->capture[i].active && t->capture[i].source == source + 26u)
                transport_capture_end(t, source + 26u, t->capture[i].pitch, fm1_ms);
    }
    for (i = 0; i < TK_MIDI_BINDINGS; i++)
        if (s->midi_bind[i].port == port || s->midi_bind[i].port == 2u)
            midi_latch &= (uint8_t)~(1u << i);
}
static int midi_control_cc(tk_settings_t *s, tk_transport_t *t, uint8_t port,
                          uint8_t st, uint8_t cc, uint8_t value, uint8_t *live_shade,
                          uint32_t *remote_buttons)
{
    uint32_t source = midi_source(port, st & 15u), i;
    if (source == TK_SOURCE_LIVE_DRUM || (source >= TK_SOURCE_FIRE0 && source < TK_SOURCE_FIRE0 + 4u)) {
        /* Pads have deliberate strike dynamics. Keyboard wheels, pedals,
         * mappings and RPN never spill into the captured percussion owner. */
        if (cc == 7u) tk_volume(source, value);
        else if (cc == 11u) tk_expression(source, value);
        else if (cc == 120u) tk_all_sound_off(source);
        else if (cc == 121u) tk_reset_controllers(source);
        else if (cc == 123u) tk_all_notes_off(source);
        return 0;
    }
    if (source < TK_SOURCE_FIRE0 && source != TK_SOURCE_LIVE_DRUM && midi_learning && !midi_reserved_cc(cc)) {
        tk_midi_binding_t *binding = &s->midi_bind[midi_focus];
        binding->cc = cc; binding->port = port; binding->channel = st & 15u;
        midi_binding_unique(s, midi_focus);
        midi_learning = 0;
        if (value >= 64u) midi_latch |= (uint8_t)(1u << midi_focus);
        else midi_latch &= (uint8_t)~(1u << midi_focus);
        return 1;
    }
    if (cc == 1u) tk_modulation(source, value);
    else if (cc == 7u) tk_volume(source, value);
    else if (cc == 11u) tk_expression(source, value);
    else if (cc == 64u) tk_sustain(source, value);
    else if (cc == 66u) tk_sostenuto(source, value);
    else if (cc == 67u) tk_soft_pedal(source, value);
    else if (cc == 71u) tk_texture(source, value);
    else if (cc == 74u) tk_brightness(source, value);
    else if (cc == 101u) midi_rpn_msb[source] = value;
    else if (cc == 100u) midi_rpn_lsb[source] = value;
    else if (cc == 98u || cc == 99u) midi_rpn_msb[source] = midi_rpn_lsb[source] = 127u;
    else if (cc == 6u && !midi_rpn_msb[source] && !midi_rpn_lsb[source])
        tk_set_bend_range(source, value);
    else if (cc == 120u) {
        tk_all_sound_off(source);
        for (i = 0; i < TK_PATTERN_STEPS * TK_PATTERN_NOTES; i++)
            if (t->capture[i].active && t->capture[i].source == source + 26u)
                t->capture[i].active = 2u;
    }
    else if (cc == 121u) {
        tk_reset_controllers(source);
        midi_rpn_msb[source] = midi_rpn_lsb[source] = 127u;
    } else if (cc == 123u) {
        tk_all_notes_off(source);
        for (i = 0; i < TK_PATTERN_STEPS * TK_PATTERN_NOTES; i++)
            if (t->capture[i].active && t->capture[i].source == source + 26u)
                t->capture[i].active = 2u;
    }
    if (source >= TK_SOURCE_KEYBOARD0) return 0; /* Captured keyboards never edit the global live patch. */
    {
        uint32_t best = TK_MIDI_BINDINGS, score = 0;
        for (i = 0; i < TK_MIDI_BINDINGS; i++) {
            const tk_midi_binding_t *binding = &s->midi_bind[i];
            uint32_t next_score;
            if (binding->cc != cc || (binding->port != 2u && binding->port != port)
                || (binding->channel != 16u && binding->channel != (st & 15u))) continue;
            next_score = (binding->port != 2u ? 2u : 0u) + (binding->channel != 16u ? 1u : 0u);
            if (best == TK_MIDI_BINDINGS || next_score > score) { best = i; score = next_score; }
        }
        if (best == TK_MIDI_BINDINGS) return 0;
        i = best;
    }
    {
        if (i < 4u) {
            graft_bake(s); macro_set(&s->p, i, value);
            if (PRESETS[s->preset].instrument == TK_INSTRUMENT_DRUM_KIT ? i == 3u : i == 1u) s->fx = 1;
        } else if (i == 4u) { graft_bake(s); s->memory = value; }
        else if (i == 5u && PRESETS[s->preset].instrument != TK_INSTRUMENT_DRUM_KIT) {
            s->graft_amount = value; graft_apply(s); *live_shade = s->p.shade;
        } else if (i == 6u) {
            if (tk_banks.importing) return 0;
            uint32_t period = transport_period(t, s);
            s->tempo = (uint8_t)(40u + (uint32_t)value * 140u / 127u);
            if (t->mode == TK_MODE_ARP) transport_retime(t, s, fm1_ms);
            else transport_retime_seq(t, period, transport_period(t, s), fm1_ms);
        } else if (i == 7u) {
            if (value >= 64u && !(midi_latch & (1u << i))) *remote_buttons |= 1u << BTN_PLAY;
            if (value >= 64u) midi_latch |= (uint8_t)(1u << i);
            else midi_latch &= (uint8_t)~(1u << i);
            return 0;
        }
        return 1;
    }
}

#include "terkinstein_remote.h"
#include "terkinstein_panel_edit.h"

/* One main-owned mask drives both the real lettering and mirror telemetry. */
static uint16_t tk_panel_button_lamps(const tk_settings_t *s, const tk_transport_t *t,
                                    uint32_t page, uint32_t hold, uint32_t edit_pending,
                                    uint32_t save_pending)
{
    uint32_t mask = 0u;
    if (s->fx) mask |= 1u << BTN_FX;
    if (page == TK_PAGE_GLO || page == TK_PAGE_SCENES) mask |= 1u << BTN_GLO;
    if (page == TK_PAGE_SEQ ? !(s->track_mute & (1u << t->selected)) :
        page == TK_PAGE_METRONOME ? s->metro_enabled : page == TK_PAGE_MEMORY) mask |= 1u << BTN_SEL;
    if (page == TK_PAGE_ENV || hold) mask |= 1u << BTN_ENV;
    if (s->lfo_on) mask |= 1u << BTN_LFO;
    if (page == TK_PAGE_SOUND_EDIT || (page >= TK_PAGE_OPERATOR && page <= TK_PAGE_VOICE) ||
        (page == TK_PAGE_FX && PRESETS[s->preset].instrument != TK_INSTRUMENT_DRUM_KIT && s->lfo_on) ||
        t->editing || edit_pending || page == TK_PAGE_PIANO) mask |= 1u << BTN_EDIT;
    if (page == TK_PAGE_PAD) mask |= 1u << BTN_HOME;
    if (t->mode == TK_MODE_ARP || tk_arr.fill) mask |= 1u << BTN_ARP;
    if (page == TK_PAGE_SEQ || page == TK_PAGE_PIANO || t->mode == TK_MODE_SEQ) mask |= 1u << BTN_SEQ;
    if (t->running) mask |= 1u << BTN_PLAY;
    if (t->recording || tk_arr.auto_armed) mask |= 1u << BTN_REC;
    if (save_pending) mask |= 1u << BTN_SAVE;
    if (s->octave < 0) mask |= 1u << BTN_OCT_DOWN;
    if (s->octave > 0) mask |= 1u << BTN_OCT_UP;
    return (uint16_t)mask;
}

static void app_main(void)
{
    static tk_settings_t s;
    tk_settings_v2_t old;
    tk_settings_v3_t old3;
    tk_settings_v4_t old4;
    static tk_settings_v6_t old6;
    static tk_settings_v7_t old7;
    static tk_settings_v8_t old8;
    static tk_settings_v9_t old9;
    static tk_settings_v11_t old11;
    static tk_settings_v10_t old10;
    static tk_transport_t transport;
    static tk_visual_snapshot_t home_visual;
    tk_lfo_t lfo;
    tk_lfo_t track_lfo[TK_TRACKS];
    tk_arp_screen_t arp_drawn = {0};
    uint32_t prev = 0, dirty = 0, save_pending = 0, save_failed = 0, changed = 1, last_draw = 0, lit_drawn = ~0u;
    uint32_t save_notice = TK_SAVE_NOTICE_NONE, save_notice_drawn = TK_SAVE_NOTICE_NONE;
    uint32_t i, page = TK_PAGE_PAD, page_changed = 0, hold = 0;
    uint32_t splash_active = 1, arp_wait_for_page = 0, arp_partial_step_drawn = ~0u, last_lfo = 0;
    tk_intro_t intro = {0};
    uint32_t play_pressed = 0, play_long = 0;
    tk_home_reset_t home_reset = {0};
    tk_fx_button_t fx_button = {0}, lfo_button = {0}, arp_button = {0}, seq_button = {0}, sel_button = {0};
    uint32_t sel_page = TK_PAGE_SEQ;
    tk_encoder_response_t knob_response[4] = {{0}};
    uint32_t remote_buttons = 0;
    uint32_t edit_pending = 0, edit_used = 0, shortcut_keys = 0;
    uint8_t live_shade;
    uint8_t track_shade[TK_TRACKS];
    memset(&transport, 0, sizeof transport);
    settings_defaults(&s);
    if (settings_load(&s, sizeof s) || !settings_valid(&s)) {
        settings_defaults(&s);
        if (!settings_load(&old11, sizeof old11) && settings_upgrade_v12(&s, &old11)) {
            /* Reuse the existing3700-byte scratch; preserve schema12 exactly, swing0. */
        } else if (!settings_load(&old11, sizeof old11) && settings_upgrade_v11(&s, &old11)) {
            /* Preserve all schema-11 settings; section mutes begin clear. */
        } else if (!settings_load(&old10, sizeof old10) && settings_upgrade_v10(&s, &old10)) {
            /* Preserve all schema-10 sounds and active patterns; metro starts off. */
        } else if (!settings_load(&old9, sizeof old9) && settings_upgrade_v9(&s, &old9)) {
            /* Keep 0.12 loops, GRAFT, forged drums and controller bindings. */
        } else if (!settings_load(&old8, sizeof old8) && settings_upgrade_v8(&s, &old8)) {
            /* Preserve the complete GRAFT base/target/amount and forged drums. */
        } else if (!settings_load(&old7, sizeof old7) && settings_upgrade_v7(&s, &old7)) {
            /* Preserve all four tracks and neutralize formerly ignored KIT controls. */
        } else if (!settings_load(&old6, sizeof old6) && settings_upgrade_v56(&s, &old6)) {
            /* Schemas 5/6 preserve three tracks; BASS starts empty, lengths 16. */
        } else if (!settings_load(&old4, sizeof old4) && settings_upgrade_v4(&s, &old4)) {
            /* Old pulses keep their original 75% gate (duration_q4 == 0). */
        } else {
            settings_defaults(&s);
            if (!settings_load(&old3, sizeof old3) && old3.schema == 3u && old3.preset < 11u) {
                s.p = old3.p; s.octave = old3.octave; s.preset = old3.preset;
                s.fx = old3.fx; s.memory = old3.memory;
                s.tempo = old3.tempo; s.lfo_rate = old3.lfo_rate;
                s.lfo_depth = old3.lfo_depth; s.lfo_shape = old3.lfo_shape;
                s.lfo_on = old3.lfo_on; s.arp_mode = old3.arp_mode;
                s.attack = old3.attack; s.release = old3.release;
                if (!pattern_upgrade(&s.pattern[TK_TRACK_MELODY], &old3.pattern))
                    settings_defaults(&s);
                track_patch_capture(&s.track[TK_TRACK_MELODY], &s);
                settings_legacy_finish(&s);
                if (!settings_valid(&s)) settings_defaults(&s);
            } else if (!settings_load(&old, sizeof old) && (old.schema == 1u || old.schema == 2u)
                && old.octave >= -2 && old.octave <= 2 && old.preset < 3u && old.fx <= 1u
                && old.p.shade <= 127u && old.p.fracture <= 127u && old.p.drift <= 127u && old.p.bloom <= 127u) {
                s.p = old.p; s.octave = old.octave; s.preset = old.preset; s.fx = old.fx;
                s.memory = old.schema == 2u && old.memory <= 127u ? old.memory : PRESETS[old.preset].memory;
                track_patch_capture(&s.track[TK_TRACK_MELODY], &s);
                settings_legacy_finish(&s);
            }
        }
    }
    tk_banks_init(&s, &transport);
    tk_init();
    tk_drums_defaults(tk_drum_mix);
    tk_visual_init();
    tk_metronome_init();
    tk_panel_activity_init(&tk_panel_activity);
    tk_metronome_clock_init(&metro_clock);
    tk_metronome_clock_configure(&metro_clock, s.metro_beats, s.metro_unit);
    tk_home_visual_reset();
    tk_arp_init(&transport.arp);
    transport.selected = TK_TRACK_MELODY;
    tk_persist_load(&s, &transport);
    publish_drum_mix();
    midi_performance_init(&s);
    tk_lfo_init(&lfo, fm1_ms);
    live_shade = s.p.shade;
    publish_params(&s, live_shade);
    for (i = 0; i < TK_TRACKS; i++) {
        tk_lfo_init(&track_lfo[i], fm1_ms);
        track_shade[i] = s.track[i].p.shade;
        publish_track(&s, i);
    }
#ifndef TERKINSTEIN_HOST
    RING_PUBLISH(); /* publish initialized DSP state before enabling app audio */
#endif
    tk_ready = 1;
    draw_splash();
    intro.logo_ms = fm1_ms;
    for (;;) {
        uint32_t keys, down, up, b, released, base, lit, screen_lit, physical_gesture, remote_flags;
        uint32_t save_activity, master_activity;
        uint32_t held_buttons, physical_pressed, old_virtual, canceled, reset_action, reset_was_open;
        uint8_t port, st, d1, d2;
        int32_t delta;
        platform_poll();
        lcd_cooperative_begin(); /* updater/recovery/fault drawing stays immediate */
        midi_live_drum_expire(&transport, &s, fm1_ms);
        midi_fire_expire(&transport,&s,fm1_ms);
        midi_keyboard_expire(&transport,&s,fm1_ms);
        if (transport_solo_expire(&transport, &s, fm1_ms)) changed = 1;
        tk_panel_activity_inputs(&tk_panel_activity, fm1_in.buttons, fm1_in.notes, fm1_ms);
        master_activity = (uint32_t)platform_master_activity();
        tk_panel_activity_encoder(&tk_panel_activity, 7u, master_activity, fm1_ms);
        remote_ui_splash = (uint8_t)!!splash_active;
        remote_flags = tk_remote_service(&s, &transport, &live_shade, &lfo, track_lfo, track_shade, page, dirty);
        if (remote_flags & 1u) changed = 1;
        if (remote_flags & 2u) dirty = 1;
        if (tk_banks.importing) tk_panel_input_cancel();
        if (tk_banks.importing && tk_panel_edit_track < TK_TRACKS)
            tk_panel_editor_select(&s, &transport, TK_TRACKS, &live_shade, &lfo, track_lfo, track_shade, &hold);
        tk_panel_editor_sequence(&s, &transport, page, &live_shade, &lfo, track_lfo, track_shade, &hold);
        tk_panel_editor_begin(&s, &live_shade, &lfo, track_lfo, track_shade);
        base = (uint32_t)(KEY0 + 12 * s.octave);
        keys = fm1_in.notes;
        down = (keys & ~prev) | fm1_input_note_edges();
        up = (prev | down) & ~keys;
        prev = keys;
        physical_pressed = b = fm1_input_edges(&released);
        tk_panel_activity_press(&tk_panel_activity, b, fm1_ms);
        old_virtual = (tk_panel_input.held | tk_panel_input.released | tk_panel_input.canceled) & ~tk_panel_input.pressed;
        tk_panel_input_expire(fm1_ms);
        held_buttons = fm1_in.buttons | tk_panel_input.held;
        b = (b & ~old_virtual) | (tk_panel_input.pressed & ~fm1_in.buttons);
        released = (released & ~tk_panel_input.held) | (tk_panel_input.released & ~fm1_in.buttons);
        canceled = tk_panel_input.canceled & ~fm1_in.buttons;
        released &= ~canceled;
        if (canceled & (1u << BTN_FX)) memset(&fx_button, 0, sizeof fx_button);
        if (canceled & (1u << BTN_LFO)) memset(&lfo_button, 0, sizeof lfo_button);
        if (canceled & (1u << BTN_ARP)) memset(&arp_button, 0, sizeof arp_button);
        if (canceled & (1u << BTN_SEQ)) memset(&seq_button, 0, sizeof seq_button);
        if (canceled & (1u << BTN_SEL)) memset(&sel_button, 0, sizeof sel_button);
        if (canceled & (1u << BTN_EDIT)) edit_pending = edit_used = 0u;
        tk_panel_input.pressed = tk_panel_input.released = tk_panel_input.canceled = 0u;
        physical_gesture = down | b;
        b |= remote_buttons; remote_buttons = 0;
        save_activity = down | b | master_activity | (remote_flags & 3u);
        reset_was_open = home_reset.prompt;
        reset_action = tk_home_reset_event(&home_reset, physical_pressed, fm1_in.buttons, b,
            down || master_activity || (remote_flags & 2u) || tk_banks.importing || save_pending,
            fm1_ms);
        if (reset_was_open) b &= ~((1u << BTN_HOME) | (1u << BTN_SEL));
        if (reset_action) { page_changed = changed = 1u; last_draw = fm1_ms - 250u; }
        if (tk_banks.importing) {
            b &= ~((1u << BTN_REC) | (1u << BTN_PLAY) | (1u << BTN_ARP) | (1u << BTN_SAVE));
            if (page == TK_PAGE_SEQ) b &= ~(1u << BTN_SEL);
        }
        if ((b & (1u << BTN_EDIT)) && (held_buttons & (1u << BTN_EDIT))) {
            edit_pending = 1; edit_used = 0; b &= ~(1u << BTN_EDIT);
        }
        for (i = 0; i < 27u; i++) {
            uint32_t bit = 1u << i;
            if ((down & bit) && (held_buttons & (1u << BTN_EDIT)) && !panel_down[i]) {
                int action = tk_label_action(&s, i, &page);
                if (action) {
                    shortcut_keys |= bit; edit_used = 1;
                    page_changed = changed = 1;
                    if (action & 2) dirty = 1;
                }
            }
            if (shortcut_keys & bit) { down &= ~bit; up &= ~bit; }
            if (down >> i & 1u) {
                tk_panel_editor_bind(&s, &transport, 1u);
                panel_press(i, base + i);
                if (page == TK_PAGE_PIANO) {
                    tk_piano.pitch = (uint8_t)(base + i);
                    tk_piano_read(&tk_piano, &s.pattern[transport.selected], transport.edit_step);
                    changed = 1;
                }
            }
            if (up >> i & 1u) panel_release(i);
        }
        shortcut_keys &= keys;
        if ((released & (1u << BTN_EDIT)) && edit_pending) {
            if (!edit_used) b |= 1u << BTN_EDIT;
            edit_pending = edit_used = 0;
        }
        if ((b & (1u << BTN_OCT_DOWN)) && s.octave > -2) { s.octave--; dirty = changed = 1; }
        if ((b & (1u << BTN_OCT_UP)) && s.octave < 2) { s.octave++; dirty = changed = 1; }
        {
            uint32_t fx_action = tk_fx_button_event(&fx_button, b & (1u << BTN_FX),
                released & (1u << BTN_FX), held_buttons & (1u << BTN_FX), fm1_ms);
            if (fx_action & 1u) { graft_bake(&s); s.fx ^= 1u; dirty = changed = 1; }
            if (fx_action & 2u) { page = TK_PAGE_FX; page_changed = changed = 1; }
        }
        if (b & (1u << BTN_GLO)) {
            page = page == TK_PAGE_ARRANGE || page == TK_PAGE_STEP_EDIT ? TK_PAGE_SCENES :
                page == TK_PAGE_SCENES ? TK_PAGE_ARRANGE : page == TK_PAGE_GLO ? TK_PAGE_PAD : TK_PAGE_GLO;
            page_changed = changed = 1;
        }
        if (sel_button.pending && !sel_button.long_press && page != sel_page)
            memset(&sel_button, 0, sizeof sel_button);
        if (page == TK_PAGE_SEQ || page == TK_PAGE_STEP_EDIT || page == TK_PAGE_PIANO || sel_button.pending) {
            if (b & (1u << BTN_SEL)) sel_page = page;
            uint32_t action = tk_fx_button_event(&sel_button, b & (1u << BTN_SEL),
                released & (1u << BTN_SEL), held_buttons & (1u << BTN_SEL), fm1_ms);
            b &= ~(1u << BTN_SEL);
            if (action & 1u) b |= 1u << BTN_SEL;
            if (action & 2u) {
                if (page == TK_PAGE_STEP_EDIT && !transport.recording) {
                    page = TK_PAGE_PIANO; transport.editing = 0u;
                    tk_piano_read(&tk_piano, &s.pattern[transport.selected], transport.edit_step);
                } else if (page == TK_PAGE_PIANO) page = TK_PAGE_STEP_EDIT;
                else { page = TK_PAGE_ARRANGE; tk_arr_lock_edit = 0; }
                page_changed = changed = 1;
            }
        }
        if (b & (1u << BTN_SEL)) {
            if (page == TK_PAGE_PIANO) {
                int action = tk_piano_button(&s, &transport, 1u);
                if (action & 2) dirty = 1;
                changed = 1;
            } else if (page == TK_PAGE_ARRANGE || page == TK_PAGE_STEP_EDIT) {
                page = page == TK_PAGE_ARRANGE ? TK_PAGE_STEP_EDIT : TK_PAGE_ARRANGE; page_changed = changed = 1;
            } else if (page == TK_PAGE_SCENES) { if (!tk_banks.importing) tk_scene_store_current(&s); changed = 1; }
            else if (page == TK_PAGE_FX) {
                graft_bake(&s); s.fx ^= 1u; dirty = changed = 1;
            } else if (page == TK_PAGE_CONTROLS) {
                page = TK_PAGE_GLO; page_changed = changed = 1;
            } else if (page == TK_PAGE_GLO) {
                page = TK_PAGE_CONTROLS; page_changed = changed = 1;
            } else if (page == TK_PAGE_METRONOME) {
                if (!tk_banks.importing) {
                    s.metro_enabled ^= 1u;
                    if (!s.metro_enabled) tk_metronome_cancel();
                    dirty = changed = 1;
                }
            } else if (page == TK_PAGE_SEQ) {
                uint32_t track = transport.selected;
                s.track_mute ^= (uint8_t)(1u << track);
                if (s.track_mute & (1u << track)) {
                    transport_release_part(&transport, track);
                    tk_all_sound_off(TRACK_SOURCE[track]);
                }
                dirty = changed = 1;
            } else if (page == TK_PAGE_MIDI) {
                if (midi_focus < TK_MIDI_BINDINGS) s.midi_bind[midi_focus].cc = 255u;
                else {
                    s.midi_bend_range = 2u;
                    for (i = 1u; i < TK_SOURCE_SEQUENCE; i++) tk_set_bend_range(i, 2u);
                }
                midi_learning = 0; midi_latch = 0;
                dirty = changed = 1;
            } else {
                page = page == TK_PAGE_MEMORY ? TK_PAGE_PAD : TK_PAGE_MEMORY;
                page_changed = changed = 1;
            }
        }
        if (b & (1u << BTN_ENV)) { page = page == TK_PAGE_ENV ? TK_PAGE_PAD : TK_PAGE_ENV; page_changed = changed = 1; }
        {
            uint32_t action = tk_fx_button_event(&lfo_button, b & (1u << BTN_LFO),
                released & (1u << BTN_LFO), held_buttons & (1u << BTN_LFO), fm1_ms);
            if ((action & 1u) && PRESETS[s.preset].instrument != TK_INSTRUMENT_DRUM_KIT) {
                graft_bake(&s); s.lfo_on ^= 1u; dirty = changed = 1;
            }
            if (action & 2u) { page = TK_PAGE_LFO; page_changed = changed = 1; }
        }
        if (b & (1u << BTN_EDIT)) {
            if (page == TK_PAGE_PIANO) { page = TK_PAGE_STEP_EDIT; page_changed = changed = 1; }
            else if (page == TK_PAGE_ARRANGE) { tk_arr_lock_edit ^= 1u; changed = 1; }
            else if (page == TK_PAGE_STEP_EDIT) {
                if (!tk_banks.importing) { tk_arr_song.step[transport.selected][tk_banks.active[transport.selected]][transport.edit_step].mask_condition &= 112u; tk_arr_changed(); }
                changed = 1;
            } else if (page == TK_PAGE_SCENES) {
                if (!tk_banks.importing) { tk_arr_song.scene[tk_scene_focus].enabled ^= 1u; tk_arr_changed(); }
                changed = 1;
            } else if (page == TK_PAGE_FX) {
                if (PRESETS[s.preset].instrument != TK_INSTRUMENT_DRUM_KIT) {
                    graft_bake(&s); s.lfo_on ^= 1u; dirty = 1;
                }
            } else if (page == TK_PAGE_CONTROLS) {
                page = TK_PAGE_GLO; page_changed = 1;
            } else if (page >= TK_PAGE_OPERATOR && page <= TK_PAGE_VOICE) {
                page = TK_PAGE_PAD; page_changed = 1;
            }
            else if (page == TK_PAGE_PAD || page == TK_PAGE_SOUND_EDIT) {
                page = page == TK_PAGE_PAD ? TK_PAGE_SOUND_EDIT : TK_PAGE_PAD;
                page_changed = 1;
            } else if (page == TK_PAGE_GLO) {
                page = TK_PAGE_MIDI; page_changed = 1;
            } else if (page == TK_PAGE_MIDI) {
                midi_learning = midi_focus < TK_MIDI_BINDINGS ? !midi_learning : 0;
            } else if (page == TK_PAGE_ENV) {
                if (PRESETS[s.preset].instrument != TK_INSTRUMENT_DRUM_KIT || hold) {
                    hold ^= 1u;
                    tk_sustain(0, hold ? 127u : 0u);
                    midi_send(0xB0, 64, hold ? 127u : 0u);
                }
            } else if (page == TK_PAGE_LFO) {
                if (PRESETS[s.preset].instrument != TK_INSTRUMENT_DRUM_KIT) {
                    graft_bake(&s);
                    s.lfo_shape = (uint8_t)((s.lfo_shape + 1u) % 3u);
                    dirty = 1;
                }
            } else if (page == TK_PAGE_MEMORY) tk_forget_memory();
            else {
                if (transport.recording) {
                    transport_capture_finish(&transport, fm1_ms);
                    transport.recording = 0;
                }
                if (transport.mode != TK_MODE_SEQ) {
                    transport_mode(&transport, TK_MODE_SEQ, fm1_ms);
                    transport_stop(&transport);
                }
                transport.editing ^= 1u;
                page = TK_PAGE_SEQ;
                page_changed = 1;
            }
            changed = 1;
        }
        if (b & (1u << BTN_HOME)) {
            if (!transport.recording && !tk_banks.importing)
                tk_panel_editor_select(&s, &transport,
                    page == TK_PAGE_SEQ || page == TK_PAGE_PIANO ? transport.selected :
                    page == TK_PAGE_PAD ? TK_TRACKS : tk_panel_edit_track,
                    &live_shade, &lfo, track_lfo, track_shade, &hold);
            page = TK_PAGE_PAD; transport.editing = 0;
            page_changed = changed = 1;
        }
        {
            uint32_t action = tk_fx_button_event(&arp_button, b & (1u << BTN_ARP),
                released & (1u << BTN_ARP), held_buttons & (1u << BTN_ARP), fm1_ms);
            if ((action & 1u) && !tk_banks.importing) {
                if (page == TK_PAGE_ARRANGE || page == TK_PAGE_STEP_EDIT) tk_arr.fill = (uint8_t)(16u - transport.next_step % 16u);
                else {
                    transport_mode(&transport, transport.mode == TK_MODE_ARP ? TK_MODE_OFF : TK_MODE_ARP, fm1_ms);
                    transport_refresh_arp(&transport);
                }
                arp_wait_for_page = 0; changed = 1;
            }
            if (action & 2u) { page = TK_PAGE_ARP; page_changed = changed = 1; }
        }
        {
            uint32_t action = tk_fx_button_event(&seq_button, b & (1u << BTN_SEQ),
                released & (1u << BTN_SEQ), held_buttons & (1u << BTN_SEQ), fm1_ms);
            if (action & 1u) {
                /* Navigation never starts playback, including active REC/ARP. */
                if (page != TK_PAGE_SEQ && !transport.recording)
                    tk_panel_editor_select(&s, &transport, TK_TRACKS, &live_shade, &lfo, track_lfo, track_shade, &hold);
                page = page == TK_PAGE_SEQ ? TK_PAGE_PAD : TK_PAGE_SEQ;
                page_changed = changed = 1;
            }
            if (action & 2u) { page = TK_PAGE_METRONOME; page_changed = changed = 1; }
        }
        if (b & (1u << BTN_PLAY)) {
            if (page == TK_PAGE_SCENES) { tk_arr_scene_launch(&s, &transport, tk_scene_focus); }
            else if (transport.mode == TK_MODE_OFF || ((page == TK_PAGE_SEQ || page == TK_PAGE_PIANO) && transport.mode != TK_MODE_SEQ)) {
                transport_mode(&transport, TK_MODE_SEQ, fm1_ms);
                arp_wait_for_page = 0;
                if (page != TK_PAGE_ARRANGE && page != TK_PAGE_STEP_EDIT && page != TK_PAGE_PIANO) { page = TK_PAGE_SEQ; page_changed = 1; }
            } else if (transport.running) transport_stop(&transport);
            else transport_start(&transport, fm1_ms);
            play_pressed = fm1_ms; play_long = 0; changed = 1;
        }
        if (b & (1u << BTN_REC)) {
            if (tk_panel_edit_track < TK_TRACKS && !transport.recording &&
                page != TK_PAGE_ARRANGE && page != TK_PAGE_STEP_EDIT && page != TK_PAGE_PIANO &&
                !(page == TK_PAGE_SEQ && transport.editing))
                transport.selected = tk_panel_edit_track;
            if (page != TK_PAGE_PIANO) tk_banks.pending[transport.selected] = TK_BANK_NONE;
            if (page == TK_PAGE_PIANO) {
                int action = tk_piano_button(&s, &transport, 0u);
                if (action & 2) dirty = 1;
                changed = 1;
            } else if (page == TK_PAGE_ARRANGE) {
                tk_arr.auto_armed ^= (uint8_t)(1u << transport.selected); tk_arr.auto_touched[transport.selected] = 0; changed = 1;
            } else if (page == TK_PAGE_STEP_EDIT && !transport.recording && !tk_banks.importing) {
                tk_pattern_clear_step(&s.pattern[transport.selected], transport.edit_step); tk_banks_observe(&s); dirty = changed = 1;
            } else if (page == TK_PAGE_SEQ && transport.editing) {
                tk_pattern_clear_step(&s.pattern[transport.selected], transport.edit_step);
                dirty = 1;
            } else if (transport.recording) {
                transport_capture_finish(&transport, fm1_ms);
                transport.recording = 0;
            }
            else {
                if (transport.mode != TK_MODE_SEQ) transport_mode(&transport, TK_MODE_SEQ, fm1_ms);
                transport_release_part(&transport, transport.selected);
                tk_all_sound_off(TRACK_SOURCE[transport.selected]);
                tk_pattern_clear(&s.pattern[transport.selected]);
                if (tk_panel_edit_track >= TK_TRACKS && !(midi_live_drum_enabled && transport.selected == TK_TRACK_DRUM)) {
                    if (transport.selected == TK_TRACK_DRUM &&
                        PRESETS[s.preset].instrument != TK_INSTRUMENT_DRUM_KIT) {
                        preset_apply(&s, TK_KIT_PRESET_INDEX);
                        tk_forget_live_memory();
                        tk_lfo_init(&lfo, fm1_ms);
                        live_shade = s.p.shade;
                    }
                    track_capture_apply(&s, transport.selected);
                    tk_lfo_init(&track_lfo[transport.selected], fm1_ms);
                }
                track_shade[transport.selected] = s.track[transport.selected].p.shade;
                publish_track(&s, transport.selected);
                s.track_mute &= (uint8_t)~(1u << transport.selected);
                if (!transport.running) transport_start(&transport, fm1_ms);
                transport.recording = 1;
                transport.record_pattern = &s.pattern[transport.selected];
                transport.record_length = s.length[transport.selected];
                transport.editing = 0;
                if (page != TK_PAGE_SEQ) page_changed = 1;
                page = TK_PAGE_SEQ; dirty = 1;
            }
            changed = 1;
        }
        if (b & (1u << BTN_SAVE)) {
            transport_mode(&transport, TK_MODE_OFF, fm1_ms);
            arp_wait_for_page = 0;
            save_pending = 1; save_failed = 0;
            save_notice = TK_SAVE_NOTICE_WAIT_SOUND;
            page = TK_PAGE_PAD; page_changed = changed = 1;
        }
        if (reset_action & TK_RESET_CONFIRM) {
            tk_panel_editor_select(&s, &transport, TK_TRACKS, &live_shade, &lfo, track_lfo, track_shade, &hold);
            tk_banks.importing = 0; tk_banks_cancel_pending();
            panel_panic(); transport_mode(&transport, TK_MODE_OFF, fm1_ms);
            transport_solo_set(&transport, &s, TK_TRACKS, fm1_ms);
            hold = 0; settings_defaults(&s);
            tk_banks_init(&s, &transport);
            midi_performance_init(&s); midi_learning = 0;
            transport.selected = TK_TRACK_MELODY;
            for (i = 0; i < TK_TRACKS; i++) {
                tk_lfo_init(&track_lfo[i], fm1_ms);
                track_shade[i] = s.track[i].p.shade;
                publish_track(&s, i);
            }
            page = TK_PAGE_PAD; page_changed = dirty = changed = 1;
        }
        if ((held_buttons & (1u << BTN_PLAY)) && !play_long && fm1_ms - play_pressed >= 700u) {
            tk_banks.importing = 0; tk_banks_cancel_pending();
            panel_panic(); transport_mode(&transport, TK_MODE_OFF, fm1_ms);
            transport_solo_set(&transport, &s, TK_TRACKS, fm1_ms);
            hold = 0;
            midi_performance_init(&s); midi_learning = 0;
            page_changed = changed = play_long = 1;
        }
        if (page != TK_PAGE_MIDI) midi_learning = 0;
        if (transport.mode == TK_MODE_ARP && (down || up)) transport_refresh_arp(&transport);
        if (transport.mode != TK_MODE_ARP) arp_wait_for_page = 0;
        {
            uint8_t old_step = transport.shown_step;
            if (!arp_wait_for_page && transport_tick(&transport, &s, fm1_ms)
                && (page == TK_PAGE_SEQ || page == TK_PAGE_PIANO)) {
                if (page == TK_PAGE_PIANO || changed || page_changed || transport.bank_changed) changed = 1;
                else if (!save_notice) draw_seq_playhead(&s, &transport, old_step, transport.shown_step);
            }
            if (transport.bank_changed) { dirty = changed = 1; transport.bank_changed = 0; }
        }
        if (!tk_banks.importing && transport.mode == TK_MODE_SEQ &&
            (!transport.recording || midi_capture_allowed(&transport, 0u)) &&
            (transport.recording || (transport.editing && page == TK_PAGE_SEQ))) {
            for (i = 0; i < 27u; i++) if (down >> i & 1u) {
                uint32_t step = transport.editing ? transport.edit_step :
                    tk_transport_local_step(&transport, transport.selected, transport.shown_step, s.length[transport.selected]);
                int accepted = transport.recording ?
                    transport_capture_start(&transport, i, panel_note[i], 100u,
                        fm1_ms, transport_period(&transport, &s)) :
                    tk_pattern_record(&s.pattern[transport.selected], step, panel_note[i], 100u);
                if (accepted) {
                    dirty = 1;
                    if (!splash_active && !save_notice && page == TK_PAGE_SEQ && !page_changed && !changed)
                        draw_seq_cell(&s, &transport, transport.selected, step);
                    else changed = 1;
                }
            }
        }
        if (transport.record_pattern)
            for (i = 0; i < 27u; i++)
                if (up >> i & 1u)
                    transport_capture_end(&transport, i, panel_note[i], fm1_ms);
        tk_panel_editor_sequence(&s, &transport, page, &live_shade, &lfo, track_lfo, track_shade, &hold);
        delta = fm1_enc_take(ENC_PRESETS);
        if (delta) save_activity = 1;
        tk_panel_activity_encoder(&tk_panel_activity, ENC_PRESETS, delta, fm1_ms);
        delta += tk_panel_input_encoder(ENC_PRESETS);
        if (delta) save_activity = 1;
        if (delta && home_reset.prompt) { tk_home_reset_cancel(&home_reset); page_changed = changed = 1u; }
        if (page == TK_PAGE_PIANO) delta = 0; /* No hidden sound replacement in a note editor. */
        if (delta) {
            int32_t next = (int32_t)factory_next(s.preset, delta);
            if (next != s.preset) {
                preset_apply(&s, (uint32_t)next);
                if (tk_panel_edit_track < TK_TRACKS) tk_forget_panel_memory();
                else tk_forget_live_memory();
                tk_lfo_init(&lfo, fm1_ms);
                live_shade = s.p.shade;
                dirty = changed = 1;
            }
        }
        delta = fm1_enc_take(ENC_SELECT);
        if (delta) save_activity = 1;
        tk_panel_activity_encoder(&tk_panel_activity, ENC_SELECT, delta, fm1_ms);
        delta += tk_panel_input_encoder(ENC_SELECT);
        if (delta) save_activity = 1;
        if (delta && home_reset.prompt) { tk_home_reset_cancel(&home_reset); page_changed = changed = 1u; }
        if (delta) {
            int action = page == TK_PAGE_PIANO ? tk_piano_encoder(&s, &transport, 0u, delta) :
                tk_arr_page_encoder(&s, &transport, page, 0u, delta);
            if (action) { if (action & 2) dirty = 1; if (action & 4) changed = 1; delta = 0; }
        }
        if (delta && page == TK_PAGE_ENV && PRESETS[s.preset].instrument == TK_INSTRUMENT_DRUM_KIT) {
            tk_drum_focus = (uint8_t)(((int32_t)tk_drum_focus + delta % 12 + 12) % 12);
            changed = 1; delta = 0;
        }
        if (tk_encoder_alias_inactive(page)) delta = 0;
        if (delta && page == TK_PAGE_SEQ) {
            int action = tk_seq_encoder(&s, &transport, 0u, delta);
            if (action & 2) dirty = 1;
            if (action & 4) changed = 1;
            delta = 0;
        }
        if (delta) {
            int panel_action = tk_panel_encoder(&s, page, 0u, delta);
            if (panel_action) { if (panel_action & 2) dirty = 1; }
            else if (page == TK_PAGE_FX) {
                int action = tk_fx_encoder(&s, 0u, delta);
                if (action & 2) dirty = 1;
            }
            else if (page == TK_PAGE_SOUND_EDIT) {
                if (PRESETS[s.preset].instrument != TK_INSTRUMENT_DRUM_KIT) {
                    uint8_t next = adjust_u8(s.graft_amount, delta, 0, 127);
                    if (next != s.graft_amount) {
                        s.graft_amount = next;
                        graft_apply(&s); live_shade = s.p.shade; dirty = 1;
                    }
                }
            }
            else if (page == TK_PAGE_PAD && PRESETS[s.preset].instrument == TK_INSTRUMENT_PAD) {
                uint8_t next = adjust_u8(s.memory, delta, 0, 127);
                if (next != s.memory) { graft_bake(&s); s.memory = next; dirty = 1; }
            }
            changed = 1;
        }
        delta = fm1_enc_take(ENC_ALGORITHM);
        if (delta) save_activity = 1;
        tk_panel_activity_encoder(&tk_panel_activity, ENC_ALGORITHM, delta, fm1_ms);
        delta += tk_panel_input_encoder(ENC_ALGORITHM);
        if (delta) save_activity = 1;
        if (delta && home_reset.prompt) { tk_home_reset_cancel(&home_reset); page_changed = changed = 1u; }
        if (delta) {
            int action = page == TK_PAGE_PIANO ? tk_piano_encoder(&s, &transport, 1u, delta) :
                tk_arr_page_encoder(&s, &transport, page, 1u, delta);
            if (action) { if (action & 2) dirty = 1; if (action & 4) changed = 1; delta = 0; }
        }
        if (tk_encoder_alias_inactive(page)) delta = 0;
        if (delta && page == TK_PAGE_SEQ) {
            int action = tk_seq_encoder(&s, &transport, 1u, delta);
            if (action & 2) dirty = 1;
            if (action & 4) changed = 1;
            delta = 0;
        }
        if (delta) {
            int panel_action = tk_panel_encoder(&s, page, 1u, delta);
            if (panel_action) { if (panel_action & 2) dirty = changed = 1; }
            else if (page == TK_PAGE_FX) {
                int action = tk_fx_encoder(&s, 1u, delta);
                if (action & 2) dirty = changed = 1;
            }
            else if (page == TK_PAGE_SOUND_EDIT) {
                if (PRESETS[s.preset].instrument != TK_INSTRUMENT_DRUM_KIT) {
                    uint8_t next = graft_next_target(&s, delta);
                    if (next != s.graft_target) {
                        graft_bake(&s); s.graft_target = next;
                        dirty = changed = 1;
                    }
                }
            }
        }
        for (i = 0; i < 4u; i++) {
            delta = fm1_enc_take(ENC_KNOB1 + i);
            if (delta) save_activity = 1;
            tk_panel_activity_encoder(&tk_panel_activity, ENC_KNOB1 + i, delta, fm1_ms);
            delta = tk_physical_knob(&knob_response[i], &s, page, i, delta, fm1_ms);
            delta += tk_panel_input_encoder(ENC_KNOB1 + i);
            if (delta) save_activity = 1;
            if (delta && home_reset.prompt) { tk_home_reset_cancel(&home_reset); page_changed = changed = 1u; }
            if (delta) {
                if (page == TK_PAGE_PIANO) {
                    int action = tk_piano_encoder(&s, &transport, i + 2u, delta);
                    if (action & 2) dirty = 1;
                    if (action & 4) changed = 1;
                    continue;
                }
                int panel_action = tk_panel_knob(&s, page, i, delta);
                if (panel_action) { if (panel_action & 2) dirty = changed = 1; continue; }
                panel_action = tk_page_knob(&s, &transport, page, i, delta, &hold, &live_shade, &lfo);
                if (panel_action) {
                    if (panel_action & 2) dirty = 1;
                    if (panel_action & 4) changed = 1;
                    continue;
                }
                if (page == TK_PAGE_FX) {
                    int action = tk_fx_knob(&s, i, delta);
                    if (action & 2) dirty = changed = 1;
                    continue;
                } else {
                    /* Encoders return bounded detents; cap before addition. */
                    uint8_t value = macro_get(&s.p, i);
                    uint8_t next = adjust_u8(value, delta, 0, 127);
                    if (next == value) continue;
                    graft_bake(&s);
                    macro_set(&s.p, i, next);
                    if (PRESETS[s.preset].instrument == TK_INSTRUMENT_DRUM_KIT ? i == 3u : i == 1u) s.fx = 1;
                }
                dirty = changed = 1;
            }
        }
        tk_panel_editor_sequence(&s, &transport, page, &live_shade, &lfo, track_lfo, track_shade, &hold);
        if (tk_panel_editor_end(&s, &transport, &live_shade, &lfo, track_lfo, track_shade)) dirty = changed = 1;
        {
            uint32_t lost = midi_take_overflow();
            if (lost & 1u) midi_port_reset(&transport, &s, 0u);
            if (lost & 2u) midi_port_reset(&transport, &s, 1u);
        }
        for (i = 0; i < 64u && midi_get_from(&port, &st, &d1, &d2); i++) {
            uint32_t source;
            if (port > 1u) continue;
            if (tk_midi_clock.enabled && port != 0u && st >= 0xF8u) continue;
            if (tk_midi_clock.enabled && port == 0u && st >= 0xF8u) {
                if (st == 0xFAu || st == 0xFBu) {
                    uint8_t keep_recording = transport.recording;
                    if (tk_banks.importing) continue;
                    if (st == 0xFAu || transport.mode != TK_MODE_SEQ)
                        transport_mode(&transport, TK_MODE_SEQ, fm1_ms);
                    else transport.running = 1u;
                    transport.recording = keep_recording;
                    arp_wait_for_page = 0; changed = 1;
                } else if (st == 0xFCu || st == 0xFFu) {
                    transport_stop(&transport); changed = 1;
                    if (st == 0xFFu) tk_clock_disable();
                }
                tk_clock_event(st, fm1_ms);
                if (st != 0xFFu) continue;
            }
            if (st == 0xFAu) {
                if (tk_banks.importing) continue;
                transport_mode(&transport, TK_MODE_SEQ, fm1_ms); arp_wait_for_page = 0; changed = 1;
                continue;
            } else if (st == 0xFBu) {
                if (tk_banks.importing) continue;
                if (transport.mode != TK_MODE_SEQ) transport_mode(&transport, TK_MODE_SEQ, fm1_ms);
                else if (!transport.running) { transport.running = 1; transport.next_ms = fm1_ms; }
                arp_wait_for_page = 0; changed = 1; continue;
            } else if (st == 0xFCu) { transport_stop(&transport); changed = 1; continue; }
            else if (st == 0xFFu) { midi_port_reset(&transport, &s, port); continue; }
            if (st < 0x80u || st >= 0xF0u || d1 > 127u || d2 > 127u) continue;
            save_activity = 1;
            source = midi_source(port, st & 15u);
            if ((st & 0xF0u) == 0x90u && d2) {
                if (source == TK_SOURCE_LIVE_DRUM) publish_live_drum();
                if (source >= TK_SOURCE_FIRE0 && source < TK_SOURCE_KEYBOARD0 + 2u) publish_fire_tracks();
                tk_note_on(d1, d2, source);
                if (transport.mode == TK_MODE_SEQ && transport.recording
                    && midi_capture_allowed(&transport, source)
                    && transport_capture_start(&transport, source + 26u, d1, d2,
                        fm1_ms, transport_period(&transport, &s))) {
                    dirty = 1;
                    if (!splash_active && !save_notice && page == TK_PAGE_SEQ && !page_changed && !changed)
                        draw_seq_cell(&s, &transport, transport.selected,
                            tk_transport_local_step(&transport, transport.selected, transport.shown_step, s.length[transport.selected]));
                    else changed = 1;
                }
            }
            else if ((st & 0xF0u) == 0x80u || (st & 0xF0u) == 0x90u) {
                tk_note_off(d1, source);
                capture_midi_release(&transport, source, d1, fm1_ms);
            }
            else if ((st & 0xF0u) == 0xE0u && source != TK_SOURCE_LIVE_DRUM &&
                     (source < TK_SOURCE_FIRE0 || source >= TK_SOURCE_KEYBOARD0))
                tk_pitch_bend(source, (uint32_t)d1 | ((uint32_t)d2 << 7));
            else if ((st & 0xF0u) == 0xD0u) tk_channel_pressure(source, d1);
            else if ((st & 0xF0u) == 0xA0u) tk_poly_pressure(d1, source, d2);
            else if ((st & 0xF0u) == 0xC0u && d1 < TK_PRESET_COUNT && source != TK_SOURCE_LIVE_DRUM && source < TK_SOURCE_FIRE0) {
                preset_apply(&s, d1); tk_forget_live_memory(); tk_lfo_init(&lfo, fm1_ms);
                live_shade = s.p.shade; dirty = changed = 1;
            }
            else if ((st & 0xF0u) == 0xB0u) {
                if (midi_control_cc(&s, &transport, port, st, d1, d2, &live_shade, &remote_buttons))
                    dirty = changed = 1;
            }
        }
        capture_midi_pedals(&transport, fm1_ms);
        if (fm1_ms - last_lfo >= 20u) {
            uint8_t next_shade = tk_lfo_eval(&lfo, fm1_ms, s.lfo_rate,
                s.lfo_on ? s.lfo_depth : 0u, s.lfo_shape, s.p.shade);
            last_lfo = fm1_ms;
            if (next_shade != live_shade) {
                live_shade = next_shade;
                if (page == TK_PAGE_LFO) changed = 1;
            }
            for (i = 0; i < TK_TRACKS; i++) {
                const tk_track_patch_t *track = &s.track[i];
                next_shade = tk_lfo_eval(&track_lfo[i], fm1_ms, track->lfo_rate,
                    track->lfo_on ? track->lfo_depth : 0u,
                    track->lfo_shape, (tk_arr.lock_mask[i] & 1u) ? tk_arr.value[i][0] : track->p.shade);
                if (next_shade != track_shade[i] || tk_source_shade_control[TRACK_SOURCE[i]] != next_shade) {
                    track_shade[i] = next_shade;
                    tk_set_source_shade(TRACK_SOURCE[i], next_shade);
                }
            }
        }
        publish_live_drum();
        publish_fire_tracks();
        if (!s.graft_amount) { track_patch_capture(&s.graft_base, &s); s.fm[5] = s.fm[0]; }
        publish_params(&s, live_shade);
        tk_panel_editor_bind(&s, &transport, 0u);
        tk_panel_editor_publish(&s);
        base = (uint32_t)(KEY0 + 12 * s.octave);
        lit = tk_active_notes(base);
        screen_lit = lit;
        if (transport.mode == TK_MODE_ARP) {
            /* Show the chord being arpeggiated instead of repainting the PAD
             * keyboard for every generated gate transition. The physical LEDs
             * below still follow the voices that are actually sounding. */
            screen_lit = 0;
            for (i = 0; i < 27u; i++)
                if (panel_refs[base + i]) screen_lit |= 1u << i;
        }
        for (i = 0; i < 27u; i++) fm1_led_key(KEY_LED(i), lit >> i & 1u);
        if (held_buttons & (1u << BTN_EDIT))
            for (i = 0; i < 10u; i++) fm1_led_key(KEY_LED(TK_LABEL_KEYS[i]), 1);
        tk_panel_editor_begin(&s, &live_shade, &lfo, track_lfo, track_shade);
        tk_panel_activity.leds = tk_panel_button_lamps(&s, &transport, page, hold, edit_pending, save_pending);
        tk_panel_editor_end(&s, &transport, &live_shade, &lfo, track_lfo, track_shade);
        for (i = 0; i < TK_PANEL_BUTTONS; i++) fm1_led_key(i, tk_panel_activity.leds >> i & 1u);
        if (tk_arr.dirty) { dirty = changed = 1; tk_arr.dirty = 0; }
        /* Dismiss only a previous result, not the SAVE press that creates it.
         * The gesture still performs its normal action; release edges and
         * read-only companion polling cannot erase the confirmation. */
        if (tk_save_notice_finished(save_notice) &&
            (save_activity || (save_notice == TK_SAVE_NOTICE_SAVED && dirty)))
            save_notice = TK_SAVE_NOTICE_NONE;
        if (save_pending && tk_persist_safe(&transport, keys)) {
            if (!tk_persist_save(&s, &transport, keys)) {
                for (i = 0; i < TK_TRACKS; i++) tk_banks.dirty[i] = 0;
                tk_arr.dirty = 0;
                dirty = save_pending = save_failed = 0; changed = 1;
                save_notice = TK_SAVE_NOTICE_SAVED;
            }
            else {
                save_pending = 0; save_failed = changed = 1;
                save_notice = TK_SAVE_NOTICE_FAILED;
            } /* no repeated flash writes on an error */
        }
        else if (save_pending)
            save_notice = transport.running || transport.recording ? TK_SAVE_NOTICE_WAIT_STOP :
                tk_banks.importing ? TK_SAVE_NOTICE_WAIT_IMPORT : TK_SAVE_NOTICE_WAIT_SOUND;
        /* Panel keys/buttons (including virtual panel buttons) skip the art.
         * Process the gesture above so the first note/button still acts. */
        if (splash_active && !physical_gesture) {
            tk_intro_tick(&intro);
        }
        if (splash_active && physical_gesture) {
            splash_active = 0;
            remote_ui_splash = 0;
            page_changed = changed = 1;
            last_draw = fm1_ms - 250u; /* redraw the selected page immediately */
        }
        if (!splash_active && save_notice && save_notice != save_notice_drawn) {
            draw_save_notice(save_notice);
            save_notice_drawn = save_notice;
        } else if (!save_notice && save_notice_drawn) {
            save_notice_drawn = TK_SAVE_NOTICE_NONE;
            page_changed = changed = 1;
            last_draw = fm1_ms - 250u;
        }
        tk_panel_editor_begin(&s, &live_shade, &lfo, track_lfo, track_shade);
        tk_visual_set_enabled(!splash_active && !save_notice && !home_reset.prompt && page == TK_PAGE_PAD);
        /* The keyboard belongs to SOUND EDIT. HOME uses the audio-driven visual. */
        if (!splash_active && !save_notice && page == TK_PAGE_SOUND_EDIT && !page_changed && !changed
            && screen_lit != lit_drawn) {
            draw_patch_keyboard(base, screen_lit);
            lit_drawn = screen_lit;
        }
        if (!splash_active && !save_notice && page == TK_PAGE_ARP && !page_changed) {
            tk_arp_screen_t now = arp_screen_state(&s, &transport);
            uint32_t fields = arp_screen_diff(&arp_drawn, &now);
            /* LCD transfers block this transport's main-loop clock. Draw
             * changed controls during the space between generated notes; at
             * fast rates, send at most one field in each off interval. */
            if (fields && transport.running && transport.arp.count) {
                if (transport_part_active(&transport, TK_TRACKS) ||
                    arp_partial_step_drawn == transport.last_step_ms)
                    fields = 0;
                else {
                    uint32_t one = fields & (0u - fields); /* lowest changed field */
                    uint32_t min_slack = one == TK_ARP_UI_ORDER ? 9u :
                        one == TK_ARP_UI_STATUS ? 6u : 3u;
                    if ((int32_t)(transport.next_ms - fm1_ms) < (int32_t)min_slack)
                        fields = 0;
                    else {
                        fields = one;
                        arp_partial_step_drawn = transport.last_step_ms;
                    }
                }
            }
            if (fields) {
                draw_arp_values(&now, fields);
                arp_screen_accept(&arp_drawn, &now, fields);
            }
            /* A change that is not shown on ARP (e.g. a timbre knob) must not
             * cause a blocking redraw while short pulses are playing. */
            changed = 0;
        }
        if (!splash_active && !save_notice && home_reset.prompt && changed) {
            draw_home_reset(home_reset.ready);
            page_changed = changed = 0u; last_draw = fm1_ms;
        }
        if (!splash_active && !save_notice && !home_reset.prompt && (page == TK_PAGE_GLO || changed)
            && fm1_ms - last_draw >= (page == TK_PAGE_GLO ? 250u : 50u)) {
            if (page_changed) {
                lcd_fill(0, 0, 240, 240, C_BLACK);
                draw_header(page);
                if (page == TK_PAGE_PAD) {
                    home_preset_drawn = home_modified_drawn = home_status_drawn = home_graft_drawn = ~0u;
                    memset(&home_visual, 0, sizeof home_visual);
                    tk_home_visual_reset();
                }
                page_changed = 0;
                if (page == TK_PAGE_PIANO) tk_piano_paint.valid = 0u;
            }
            if (page == TK_PAGE_GLO) draw_diagnostics();
            else if (page == TK_PAGE_MEMORY) draw_memory(&s);
            else if (page == TK_PAGE_ENV) draw_envelope(&s, hold);
            else if (page == TK_PAGE_LFO) draw_lfo(&s, live_shade);
            else if (page == TK_PAGE_ARP) {
                arp_drawn = arp_screen_state(&s, &transport);
                draw_arp(&arp_drawn);
            }
            else if (page == TK_PAGE_SEQ) draw_seq(&s, &transport);
            else if (page == TK_PAGE_PIANO) draw_piano(&s, &transport);
            else if (page == TK_PAGE_MIDI) draw_midi_controls(&s);
            else if (page == TK_PAGE_METRONOME) draw_metronome(&s);
            else if (page == TK_PAGE_CONTROLS) draw_control_feel(&s);
            else if (page == TK_PAGE_ARRANGE || page == TK_PAGE_STEP_EDIT || page == TK_PAGE_SCENES) draw_arrangement(&s, &transport, page);
            else if (page == TK_PAGE_FX) draw_fx(&s);
            else if (page == TK_PAGE_OPERATOR) tk_draw_operator(&s);
            else if (page == TK_PAGE_PITCH) tk_draw_pitch(&s);
            else if (page == TK_PAGE_GLOBAL) tk_draw_global(&s);
            else if (page == TK_PAGE_VOICE) tk_draw_voice(&s);
            else if (page == TK_PAGE_SOUND_EDIT)
                draw_sound_edit(&s, &transport, dirty, save_pending, save_failed, base, screen_lit);
            else draw_home(&s, dirty, save_pending, save_failed);
            last_draw = fm1_ms;
            if (page == TK_PAGE_SOUND_EDIT) lit_drawn = screen_lit;
            changed = 0;
            if (arp_wait_for_page && transport.mode == TK_MODE_ARP) {
                transport.next_ms = fm1_ms;
                arp_wait_for_page = 0;
            }
        }
        if (!splash_active && !save_notice && !home_reset.prompt && page == TK_PAGE_PAD && !page_changed &&
            transport_visual_slack(&transport, fm1_ms)) {
            if (tk_visual_bus.sequence != home_visual.generation)
                tk_visual_read(&home_visual); /* rejected/torn copies keep the last valid input */
            tk_home_visual_select(s.home_visual);
            tk_home_visual_prepare(&home_visual, fm1_ms, s.preset);
            if (tk_home_visual_pending()) tk_home_visual_upload();
            /* The next loop starts with platform_poll and services transport
             * before another short batch; no animation-wide LCD transfer. */
        }
        if (!splash_active && !save_notice && page != TK_PAGE_PAD && !page_changed && !changed
            && transport_visual_slack(&transport, fm1_ms))
            ui_accent_tick(page, &s, &transport, fm1_ms, screen_lit, live_shade);
        if (!splash_active && !save_notice && page == TK_PAGE_PIANO && !page_changed &&
            transport_visual_slack(&transport, fm1_ms)) tk_piano_upload();
        tk_panel_editor_end(&s, &transport, &live_shade, &lfo, track_lfo, track_shade);
        /* Only Thorn Rift uses the negotiated coarse HOME codec. The portrait
         * retains native pixels/colours. Deliver bounded physical tiles before
         * the mirror may snapshot them; audio never participates in drawing. */
        tk_screen_set_home(!splash_active && !save_notice && !home_reset.prompt && page == TK_PAGE_PAD && !page_changed
            && tk_home_visual_is_rift(s.home_visual));
        if (transport_visual_slack(&transport, fm1_ms)) lcd_cooperative_upload();
        tk_remote_screen_service(transport_visual_slack(&transport, fm1_ms));
    }
}
