/* SPDX-License-Identifier: GPL-3.0-only */
/* Main-owned application control for remote MIDI clients. Included after the
 * app's settings, patch and transport helpers, immediately before app_main. */
#ifndef TERKINSTEIN_REMOTE_H
#define TERKINSTEIN_REMOTE_H
#include "terkinstein_protocol.h"
#include "terkinstein_activity.h"
#define TK_REMOTE_CHANGED 1u
#define TK_REMOTE_DIRTY 2u
#define TK_REMOTE_STATE_BYTES 79u

static uint8_t remote_last_request[TK_REMOTE_DATA_MAX], remote_reply[TK_REMOTE_WIRE_MAX];
static uint8_t remote_cached_reply[TK_REMOTE_WIRE_MAX];
static uint32_t remote_last_length, remote_reply_length, remote_generation;
static uint32_t remote_cached_length;
static uint8_t remote_last_valid, remote_reply_pending, remote_observed_valid;
static uint8_t remote_ui_splash = 1u;
static uint16_t remote_revision, remote_pattern_revision;
static tk_settings_t remote_observed_settings;

static void tk_remote_observe(const tk_settings_t *s)
{
    /* Exact comparison catches physical panel edits and completed REC gates,
     * including a same-occupancy pitch edit. No hash collisions or ISR work. */
    if (!remote_observed_valid || memcmp(&remote_observed_settings, s, sizeof *s)) {
        if (!remote_observed_valid || memcmp(remote_observed_settings.pattern, s->pattern, sizeof s->pattern))
            remote_pattern_revision = (uint16_t)((remote_pattern_revision + 1u) & 16383u);
        memcpy(&remote_observed_settings, s, sizeof *s);
        remote_observed_valid = 1;
        remote_revision = (uint16_t)((remote_revision + 1u) & 16383u);
    }
}
static void tk_remote_fm_pack(const tk_fm_patch_t *fm, uint8_t *out)
{
    uint32_t i;
    out[0] = fm->algorithm; out[1] = fm->feedback;
    out[2] = fm->pitch_depth; out[3] = fm->pitch_time;
    for (i = 0; i < 6u; i++) {
        out[4u + i * 6u] = fm->op[i].ratio; out[5u + i * 6u] = fm->op[i].level;
        out[6u + i * 6u] = fm->op[i].attack; out[7u + i * 6u] = fm->op[i].decay;
        out[8u + i * 6u] = fm->op[i].sustain; out[9u + i * 6u] = fm->op[i].release;
    }
}
static void tk_remote_fm_unpack(tk_fm_patch_t *fm, const uint8_t *p)
{
    uint32_t i;
    fm->algorithm = p[0]; fm->feedback = p[1]; fm->pitch_depth = p[2]; fm->pitch_time = p[3];
    for (i = 0; i < 6u; i++) {
        fm->op[i].ratio = p[4u + i * 6u]; fm->op[i].level = p[5u + i * 6u];
        fm->op[i].attack = p[6u + i * 6u]; fm->op[i].decay = p[7u + i * 6u];
        fm->op[i].sustain = p[8u + i * 6u]; fm->op[i].release = p[9u + i * 6u];
    }
}
static int tk_remote_parse_step(tk_pattern_step_t *next, const uint8_t *p, uint32_t n)
{
    uint32_t i;
    if (!n || p[0] > TK_PATTERN_NOTES || n != 1u + p[0] * 4u) return 0;
    memset(next, 0, sizeof *next); next->count = p[0];
    for (i = 0; i < next->count; i++) {
        uint32_t at = 1u + i * 4u;
        if (!p[at + 1u] || p[at + 3u] > 1u || (i && p[at] <= next->note[i - 1u].pitch)) return 0;
        next->note[i].pitch = p[at]; next->note[i].velocity = p[at + 1u];
        next->note[i].duration_q4 = (uint8_t)(p[at + 2u] | p[at + 3u] << 7);
    }
    return 1;
}
static uint32_t tk_remote_bank_step(tk_settings_t *s, uint32_t track, uint32_t bank,
                                     uint32_t step, uint8_t *out)
{
    const tk_pattern_step_t *part = &tk_bank_pattern(s, track, bank)->step[step];
    uint32_t i;
    out[0] = TK_REMOTE_OK; out[1] = (uint8_t)track; out[2] = (uint8_t)bank;
    out[3] = (uint8_t)step; out[4] = part->count;
    for (i = 0; i < part->count; i++) {
        out[5u + i * 4u] = part->note[i].pitch; out[6u + i * 4u] = part->note[i].velocity;
        out[7u + i * 4u] = part->note[i].duration_q4 & 127u;
        out[8u + i * 4u] = part->note[i].duration_q4 >> 7;
    }
    return 5u + part->count * 4u;
}
static uint32_t tk_remote_banks(tk_settings_t *s, const tk_transport_t *t, uint8_t *out)
{
    uint32_t track, bank, step, revision;
    tk_banks_observe(s); tk_remote_observe(s);
    memset(out, 0, 73u); out[0] = TK_REMOTE_OK; out[1] = 1; out[2] = tk_banks.importing;
    revision = (tk_banks.revision + remote_revision) & 16383u;
    out[3] = (uint8_t)(revision & 127u); out[4] = (uint8_t)(revision >> 7);
    for (track = 0; track < TK_TRACKS; track++) {
        out[5u + track] = tk_banks.active[track]; out[9u + track] = tk_banks.pending[track];
        out[13u + track] = tk_banks.head_pending[track] ? 0u :
            tk_transport_local_step(t, track, t->shown_step, s->length[track]);
        out[69u + track] = tk_banks.dirty[track];
        for (bank = 0; bank < TK_BANK_COUNT; bank++) {
            uint32_t index = track * TK_BANK_COUNT + bank;
            const tk_pattern_t *pattern = tk_bank_pattern(s, track, bank);
            uint32_t length = tk_banks.length[track][bank], rev = tk_banks.bank_revision[track][bank];
            out[17u + index] = length == 64u ? 2u : length == 32u ? 1u : 0u;
            out[33u + index * 2u] = (uint8_t)(rev & 127u); out[34u + index * 2u] = (uint8_t)(rev >> 7);
            for (step = 0; step < TK_PATTERN_STEPS; step++)
                if (pattern->step[step].count) { out[65u + track] |= (uint8_t)(1u << bank); break; }
        }
    }
    return 73u;
}
static uint32_t tk_remote_state(const tk_settings_t *s, const tk_transport_t *t,
                                uint32_t page, uint32_t dirty, uint8_t *out)
{
    uint32_t track, step;
    tk_remote_observe(s);
    memset(out, 0, TK_REMOTE_STATE_BYTES);
    out[0] = TK_REMOTE_OK; out[1] = 1; out[2] = t->mode;
    out[3] = (uint8_t)((t->running ? 1u : 0u) | (t->recording ? 2u : 0u)
        | (t->editing ? 4u : 0u) | (dirty ? 8u : 0u));
    out[4] = t->selected; out[5] = t->shown_step; out[6] = t->edit_step;
    out[7] = s->tempo & 127u; out[8] = s->tempo >> 7;
    out[9] = s->track_mute; out[10] = s->preset;
    for (track = 0; track < TK_TRACKS; track++) {
        out[11u + track] = s->length[track] == 64u ? 2u : s->length[track] == 32u ? 1u : 0u;
        out[15u + track] = s->track[track].preset;
        for (step = 0; step < TK_PATTERN_STEPS; step++)
            if (s->pattern[track].step[step].count)
                out[32u + track * 10u + step / 7u] |= (uint8_t)(1u << (step % 7u));
    }
    out[19] = s->p.shade; out[20] = s->p.fracture; out[21] = s->p.drift; out[22] = s->p.bloom;
    out[23] = s->memory; out[24] = s->graft_target; out[25] = s->graft_amount;
    out[26] = s->ghost_mask; out[27] = s->fx; out[28] = s->attack; out[29] = s->release;
    out[30] = (uint8_t)(remote_revision & 127u); out[31] = (uint8_t)(remote_revision >> 7);
    out[72] = (uint8_t)page; out[73] = TK_VOICES; out[74] = (uint8_t)TK_PRESET_COUNT; out[75] = TK_PATTERN_STEPS;
    out[76] = s->mono;
    out[77] = (uint8_t)(remote_pattern_revision & 127u); out[78] = (uint8_t)(remote_pattern_revision >> 7);
    return TK_REMOTE_STATE_BYTES;
}
static uint32_t tk_remote_step(const tk_settings_t *s, uint32_t track, uint32_t step, uint8_t *out)
{
    const tk_pattern_step_t *part = &s->pattern[track].step[step];
    uint32_t i;
    out[0] = TK_REMOTE_OK; out[1] = (uint8_t)track; out[2] = (uint8_t)step; out[3] = part->count;
    for (i = 0; i < part->count; i++) {
        out[4u + i * 4u] = part->note[i].pitch;
        out[5u + i * 4u] = part->note[i].velocity;
        out[6u + i * 4u] = part->note[i].duration_q4 & 127u;
        out[7u + i * 4u] = part->note[i].duration_q4 >> 7;
    }
    return 4u + part->count * 4u;
}
static uint32_t tk_remote_track_macros(const tk_settings_t *s, uint32_t track, uint8_t *out)
{
    uint32_t i;
    out[0] = TK_REMOTE_OK; out[1] = (uint8_t)track;
    for (i = 0; i < 4u; i++) out[2u + i] = macro_get(&s->track[track].p, i);
    return 6u;
}
static void tk_remote_track_refresh(const tk_settings_t *s, tk_transport_t *t, uint32_t track,
                                    tk_lfo_t track_lfo[TK_TRACKS], uint8_t track_shade[TK_TRACKS])
{
    transport_release_part(t, track);
    tk_all_sound_off(TRACK_SOURCE[track]);
    tk_lfo_init(&track_lfo[track], fm1_ms);
    track_shade[track] = s->track[track].p.shade;
    publish_track(s, track);
}

/* All commands use absolute values. Response status is first in every payload.
 * Returning DIRTY always also returns CHANGED; query traffic returns neither. */
static uint32_t tk_remote_command(const tk_remote_message_t *request,
                                 tk_settings_t *s, tk_transport_t *t,
                                 uint8_t *live_shade, tk_lfo_t *lfo,
                                 tk_lfo_t track_lfo[TK_TRACKS], uint8_t track_shade[TK_TRACKS],
                                 uint32_t page, uint32_t dirty, uint8_t *out, uint32_t *out_n)
{
    const uint8_t *p = request->payload;
    uint32_t n = request->length, track, step, i, flags = 0;
    out[0] = TK_REMOTE_BAD_ARGUMENT; *out_n = 1;
    if (tk_banks.importing) {
        switch (request->command) {
        case TK_REMOTE_SET_STEP: case TK_REMOTE_TRACK: case TK_REMOTE_LENGTH:
        case TK_REMOTE_MUTE: case TK_REMOTE_TEMPO: case TK_REMOTE_TRANSPORT:
        case TK_REMOTE_TRACK_PRESET: case TK_REMOTE_CAPTURE: case TK_REMOTE_RECORD:
        case TK_REMOTE_SET_TRACK_MACRO: case TK_REMOTE_SELECT_BANK: case TK_REMOTE_SET_BANK_STEP:
        case TK_REMOTE_SET_BANK_LENGTH: case TK_REMOTE_COPY_BANK: case TK_REMOTE_CLEAR_BANK:
        case TK_REMOTE_SONG_BEGIN:
        case TK_REMOTE_SET_METRONOME:
            out[0] = TK_REMOTE_BUSY; return 0;
        default: break;
        }
    }
    switch (request->command) {
    case TK_REMOTE_HELLO:
        if (n) break;
        out[0] = TK_REMOTE_OK; out[1] = TK_REMOTE_VERSION;
        out[2] = 127u; out[3] = 127; /* high: macros, banks/song, mirror, palette/burst, UI state, metro, panel */
        out[4] = TK_TRACKS; out[5] = TK_PATTERN_STEPS; out[6] = TK_PATTERN_NOTES;
        out[7] = TK_VOICES; out[8] = (uint8_t)TK_PRESET_COUNT; out[9] = 2;
        for (i = 0; i < 24u && APP_VER[i]; i++) out[11u + i] = (uint8_t)APP_VER[i];
        out[10] = (uint8_t)i; *out_n = 11u + i;
        break;
    case TK_REMOTE_STATE:
        if (!n) *out_n = tk_remote_state(s, t, page, dirty, out);
        break;
    case TK_REMOTE_UI_STATE:
        if (!n) {
            out[0] = TK_REMOTE_OK; out[1] = 1u;
            out[2] = remote_ui_splash; out[3] = (uint8_t)page; *out_n = 4u;
        }
        break;
    case TK_REMOTE_PANEL_ACTIVITY:
        if (!n) {
            out[0] = TK_REMOTE_OK;
            *out_n = 1u + tk_panel_activity_pack(&tk_panel_activity, s->octave, fm1_ms, out + 1u);
        }
        break;
    case TK_REMOTE_PAD_ROUTE:
    case TK_REMOTE_SET_PAD_ROUTE:
        if (request->command == TK_REMOTE_PAD_ROUTE ? n != 0u :
            (n != 2u || p[0] != 1u || p[1] > 1u)) break;
        if (request->command == TK_REMOTE_SET_PAD_ROUTE) {
            if (p[1] && !midi_live_drum_enabled && (t->recording || tk_banks.importing)) {
                out[0] = TK_REMOTE_BUSY; break;
            }
            midi_live_drum_set(t, s, p[1]);
        }
        out[0] = TK_REMOTE_OK; out[1] = 1u;
        out[2] = midi_live_drum_enabled; out[3] = 16u; out[4] = TK_TRACK_DRUM;
        *out_n = 5u;
        break;
    case TK_REMOTE_SOLO:
        if (n > 1u || (n && p[0] > TK_TRACKS)) break;
        if (n) {
            if (tk_banks.importing && p[0] < TK_TRACKS && t->solo_track != p[0] + 1u) {
                out[0] = TK_REMOTE_BUSY; break;
            }
            if (transport_solo_set(t, s, p[0], fm1_ms)) flags = TK_REMOTE_CHANGED;
        }
        out[0] = TK_REMOTE_OK; out[1] = t->solo_track ? (uint8_t)(t->solo_track - 1u) : TK_TRACKS;
        *out_n = 2u;
        break;
    case TK_REMOTE_METRONOME:
        if (!n) {
            out[0] = TK_REMOTE_OK; out[1] = 1u;
            out[2] = s->metro_enabled; out[3] = s->metro_level;
            out[4] = s->metro_beats; out[5] = s->metro_unit; *out_n = 6u;
        }
        break;
    case TK_REMOTE_SET_METRONOME:
        if (n != 4u || p[0] > 1u || p[1] > 127u || p[2] < 1u || p[2] > 12u
            || (p[3] != 4u && p[3] != 8u)) break;
        if (s->metro_enabled != p[0] || s->metro_level != p[1]
            || s->metro_beats != p[2] || s->metro_unit != p[3]) {
            s->metro_enabled = p[0]; s->metro_level = p[1];
            s->metro_beats = p[2]; s->metro_unit = p[3];
            tk_metronome_clock_configure(&metro_clock, s->metro_beats, s->metro_unit);
            if (!s->metro_enabled || !s->metro_level) tk_metronome_cancel();
            flags = TK_REMOTE_CHANGED | TK_REMOTE_DIRTY;
        }
        out[0] = TK_REMOTE_OK;
        break;
    case TK_REMOTE_SCREEN_SUBSCRIBE:
        if (n == 1u && p[0] <= 4u) {
            out[0] = TK_REMOTE_OK;
            out[1] = tk_screen_subscribe(p[0], fm1_ms, midi_remote_generation());
            *out_n = 2u;
        } else if (n == 2u && (p[0] == 3u || p[0] == 4u) && p[1] == 1u) {
            out[0] = TK_REMOTE_OK;
            out[1] = tk_screen_subscribe_profile(p[0], p[1], fm1_ms, midi_remote_generation());
            out[2] = p[1]; *out_n = 3u;
        }
        break;
    case TK_REMOTE_STEP:
        if (n == 2u && p[0] < TK_TRACKS && p[1] < TK_PATTERN_STEPS)
            *out_n = tk_remote_step(s, p[0], p[1], out);
        break;
    case TK_REMOTE_TRACK_MACROS:
        if (n == 1u && p[0] < TK_TRACKS)
            *out_n = tk_remote_track_macros(s, p[0], out);
        break;
    case TK_REMOTE_BANK_STATE:
        if (!n) *out_n = tk_remote_banks(s, t, out);
        break;
    case TK_REMOTE_BANK_STEP:
        if (n == 3u && p[0] < TK_TRACKS && p[1] < TK_BANK_COUNT && p[2] < TK_PATTERN_STEPS) {
            tk_banks_observe(s); *out_n = tk_remote_bank_step(s, p[0], p[1], p[2], out);
        }
        break;
    case TK_REMOTE_TRACK_PATCH:
        if (n == 1u && p[0] < TK_TRACKS) {
            out[0] = TK_REMOTE_OK; out[1] = p[0];
            memcpy(out + 2, &s->track[p[0]], 14u);
            tk_remote_fm_pack(&s->fm[p[0] + 1u], out + 16); *out_n = 56u;
        }
        break;
    case TK_REMOTE_FM:
        if (n != 1u || p[0] > TK_TRACKS) break;
        out[0] = TK_REMOTE_OK; out[1] = p[0];
        out[2] = s->fm[p[0]].algorithm; out[3] = s->fm[p[0]].feedback;
        out[4] = s->fm[p[0]].pitch_depth; out[5] = s->fm[p[0]].pitch_time;
        for (i = 0; i < 6u; i++) {
            out[6u + i * 6u] = s->fm[p[0]].op[i].ratio;
            out[7u + i * 6u] = s->fm[p[0]].op[i].level;
            out[8u + i * 6u] = s->fm[p[0]].op[i].attack;
            out[9u + i * 6u] = s->fm[p[0]].op[i].decay;
            out[10u + i * 6u] = s->fm[p[0]].op[i].sustain;
            out[11u + i * 6u] = s->fm[p[0]].op[i].release;
        }
        *out_n = 42u;
        break;
    case TK_REMOTE_LOAD:
        if (n) break;
        memset(out, 0, 14u); out[0] = TK_REMOTE_OK; *out_n = 14u;
#ifndef TERKINSTEIN_HOST
        {
            uint32_t values[3], item;
            out[1] = 1u;
            /* Each audio-written scalar is read atomically, but these three
             * observations are not a coherent multi-field ISR snapshot. */
            values[0] = tk_render_last_ticks / FM1_TICKS_PER_US;
            values[1] = tk_render_max_ticks / FM1_TICKS_PER_US;
            values[2] = tk_render_late_blocks;
            for (item = 0; item < 3u; item++) {
                uint32_t value = values[item] > 268435455u ? 268435455u : values[item];
                for (i = 0; i < 4u; i++) {
                    out[2u + item * 4u + i] = (uint8_t)(value & 127u); value >>= 7;
                }
            }
        }
#endif
        break;
    case TK_REMOTE_SET_STEP:
        if (n < 3u || p[0] >= TK_TRACKS || p[1] >= TK_PATTERN_STEPS
            || p[2] > TK_PATTERN_NOTES || n != 3u + p[2] * 4u) break;
        track = p[0]; step = p[1];
        if (t->recording && t->selected == track) { out[0] = TK_REMOTE_BUSY; break; }
        {
            tk_pattern_step_t next;
            memset(&next, 0, sizeof next); next.count = p[2];
            for (i = 0; i < next.count; i++) {
                uint32_t at = 3u + i * 4u;
                if (!p[at + 1u] || p[at + 3u] > 1u || (i && p[at] <= next.note[i - 1u].pitch))
                    return 0; /* status remains BAD_ARGUMENT; no mutation */
                next.note[i].pitch = p[at]; next.note[i].velocity = p[at + 1u];
                next.note[i].duration_q4 = (uint8_t)(p[at + 2u] | p[at + 3u] << 7);
            }
            if (memcmp(&s->pattern[track].step[step], &next, sizeof next)) {
                s->pattern[track].step[step] = next;
                flags = TK_REMOTE_CHANGED | TK_REMOTE_DIRTY;
            }
        }
        *out_n = tk_remote_step(s, track, step, out);
        break;
    case TK_REMOTE_TRACK:
        if (n != 1u || p[0] >= TK_TRACKS) break;
        if (t->recording && p[0] != t->selected) { out[0] = TK_REMOTE_BUSY; break; }
        if (t->selected != p[0]) {
            t->selected = p[0]; t->edit_step %= s->length[t->selected]; flags = TK_REMOTE_CHANGED;
        }
        out[0] = TK_REMOTE_OK;
        break;
    case TK_REMOTE_LENGTH:
        if (n != 2u || p[0] >= TK_TRACKS || p[1] > 2u) break;
        if (t->recording) { out[0] = TK_REMOTE_BUSY; break; }
        track = p[0]; i = 16u << p[1];
        if (s->length[track] != i) {
            transport_release_part(t, track); tk_all_sound_off(TRACK_SOURCE[track]);
            s->length[track] = (uint8_t)i;
            if (track == t->selected) t->edit_step %= i;
            flags = TK_REMOTE_CHANGED | TK_REMOTE_DIRTY;
        }
        out[0] = TK_REMOTE_OK;
        break;
    case TK_REMOTE_MUTE:
        if (n != 2u || p[0] >= TK_TRACKS || p[1] > 1u) break;
        track = p[0];
        if (((s->track_mute >> track) & 1u) != p[1]) {
            if (p[1]) { s->track_mute |= (uint8_t)(1u << track);
                transport_release_part(t, track); tk_all_sound_off(TRACK_SOURCE[track]); }
            else s->track_mute &= (uint8_t)~(1u << track);
            flags = TK_REMOTE_CHANGED | TK_REMOTE_DIRTY;
        }
        out[0] = TK_REMOTE_OK;
        break;
    case TK_REMOTE_TEMPO:
        if (n != 2u || p[1] > 1u) break;
        i = p[0] | p[1] << 7;
        if (i < 40u || i > 180u) break;
        if (i != s->tempo) {
            uint32_t old_period = transport_period(t, s);
            s->tempo = (uint8_t)i;
            if (t->mode == TK_MODE_ARP) transport_retime(t, s, fm1_ms);
            else transport_retime_seq(t, old_period, transport_period(t, s), fm1_ms);
            flags = TK_REMOTE_CHANGED | TK_REMOTE_DIRTY;
        }
        out[0] = TK_REMOTE_OK;
        break;
    case TK_REMOTE_TRANSPORT:
        if (n != 1u || p[0] > 2u) break;
        if (p[0] == 0u) {
            if (t->recording) flags |= TK_REMOTE_DIRTY;
            transport_stop(t);
        } else if (p[0] == 1u) {
            if (t->recording) flags |= TK_REMOTE_DIRTY;
            if (t->mode != TK_MODE_SEQ) transport_mode(t, TK_MODE_SEQ, fm1_ms);
            else { transport_stop(t); transport_start(t, fm1_ms); }
        } else if (t->mode != TK_MODE_SEQ) transport_mode(t, TK_MODE_SEQ, fm1_ms);
        else if (!t->running) { t->running = 1; t->next_ms = fm1_ms; }
        flags |= TK_REMOTE_CHANGED; out[0] = TK_REMOTE_OK;
        break;
    case TK_REMOTE_LIVE_PRESET:
        if (n != 1u || p[0] >= TK_PRESET_COUNT) break;
        preset_apply(s, p[0]); tk_forget_memory(); tk_lfo_init(lfo, fm1_ms); *live_shade = s->p.shade;
        flags = TK_REMOTE_CHANGED | TK_REMOTE_DIRTY; out[0] = TK_REMOTE_OK;
        break;
    case TK_REMOTE_TRACK_PRESET:
    case TK_REMOTE_CAPTURE:
        if (n != (request->command == TK_REMOTE_CAPTURE ? 1u : 2u)
            || p[0] >= TK_TRACKS || (n == 2u && p[1] >= TK_PRESET_COUNT)) break;
        track = p[0];
        if (t->recording && track == t->selected) { out[0] = TK_REMOTE_BUSY; break; }
        if (request->command == TK_REMOTE_CAPTURE) track_capture_apply(s, track);
        else track_factory_apply(s, track, p[1]);
        tk_remote_track_refresh(s, t, track, track_lfo, track_shade);
        flags = TK_REMOTE_CHANGED | TK_REMOTE_DIRTY; out[0] = TK_REMOTE_OK;
        break;
    case TK_REMOTE_MACRO:
        if (n != 2u || p[0] > 5u) break;
        if (p[0] == 5u) {
            if (PRESETS[s->preset].instrument == TK_INSTRUMENT_DRUM_KIT) break;
            s->graft_amount = p[1]; graft_apply(s); *live_shade = s->p.shade;
        } else {
            graft_bake(s);
            if (p[0] < 4u) { macro_set(&s->p, p[0], p[1]); if (!p[0]) *live_shade = s->p.shade; }
            else s->memory = p[1];
        }
        flags = TK_REMOTE_CHANGED | TK_REMOTE_DIRTY; out[0] = TK_REMOTE_OK;
        break;
    case TK_REMOTE_SET_TRACK_MACRO:
        if (n != 3u || p[0] >= TK_TRACKS || p[1] >= 4u || p[2] > 127u) break;
        track = p[0];
        if (t->recording && track == t->selected) { out[0] = TK_REMOTE_BUSY; break; }
        if (macro_get(&s->track[track].p, p[1]) != p[2]) {
            const tk_track_patch_t *patch;
            macro_set(&s->track[track].p, p[1], p[2]);
            /* The existing packed source mailbox feeds future and held voices.
             * Preserve gate/phase and the track LFO's clock; never select its
             * preset for live playing or release the generated part. */
            publish_track(s, track);
            tk_follow_source_macros(TRACK_SOURCE[track]);
            patch = &s->track[track];
            track_shade[track] = tk_lfo_eval(&track_lfo[track], fm1_ms,
                patch->lfo_rate, patch->lfo_on ? patch->lfo_depth : 0u,
                patch->lfo_shape, patch->p.shade);
            tk_set_source_shade(TRACK_SOURCE[track], track_shade[track]);
            flags = TK_REMOTE_CHANGED | TK_REMOTE_DIRTY;
        }
        *out_n = tk_remote_track_macros(s, track, out);
        break;
    case TK_REMOTE_RECORD:
        if ((n != 2u && n != 3u) || p[0] >= TK_TRACKS || p[1] > 1u
            || (n == 3u && p[2] > 1u)) break;
        track = p[0];
        if (t->recording && track != t->selected) { out[0] = TK_REMOTE_BUSY; break; }
        if (!p[1]) {
            if (t->recording) { transport_capture_finish(t, fm1_ms); t->recording = 0;
                flags = TK_REMOTE_CHANGED | TK_REMOTE_DIRTY; }
        } else if (!t->recording) {
            tk_banks.pending[track] = TK_BANK_NONE;
            if (t->mode != TK_MODE_SEQ) transport_mode(t, TK_MODE_SEQ, fm1_ms);
            t->selected = (uint8_t)track;
            transport_release_part(t, track); tk_all_sound_off(TRACK_SOURCE[track]);
            tk_pattern_clear(&s->pattern[track]);
            if ((n == 2u || !p[2]) && !(midi_live_drum_enabled && track == TK_TRACK_DRUM)) {
                if (track == TK_TRACK_DRUM && PRESETS[s->preset].instrument != TK_INSTRUMENT_DRUM_KIT) {
                    preset_apply(s, TK_KIT_PRESET_INDEX); tk_forget_memory(); tk_lfo_init(lfo, fm1_ms);
                    *live_shade = s->p.shade;
                }
                track_capture_apply(s, track);
                tk_lfo_init(&track_lfo[track], fm1_ms);
            }
            track_shade[track] = tk_lfo_eval(&track_lfo[track], fm1_ms,
                s->track[track].lfo_rate, s->track[track].lfo_on ? s->track[track].lfo_depth : 0u,
                s->track[track].lfo_shape, s->track[track].p.shade);
            publish_track(s, track); tk_set_source_shade(TRACK_SOURCE[track], track_shade[track]);
            s->track_mute &= (uint8_t)~(1u << track);
            if (!t->running) transport_start(t, fm1_ms);
            t->recording = 1; t->record_pattern = &s->pattern[track]; t->record_length = s->length[track];
            t->editing = 0; flags = TK_REMOTE_CHANGED | TK_REMOTE_DIRTY;
        }
        out[0] = TK_REMOTE_OK;
        break;
    case TK_REMOTE_SELECT_BANK:
        if ((n != 2u && n != 3u) || p[0] >= TK_TRACKS || p[1] >= TK_BANK_COUNT
            || (n == 3u && p[2] > 1u)) break;
        track = p[0];
        if (t->recording && t->selected == track) { out[0] = TK_REMOTE_BUSY; break; }
        if (p[1] == tk_banks.active[track]) {
            if (tk_banks.pending[track] != TK_BANK_NONE) {
                tk_banks.pending[track] = TK_BANK_NONE;
                tk_banks.revision = (uint16_t)((tk_banks.revision + 1u) & 16383u);
                flags = TK_REMOTE_CHANGED;
            }
        } else if (t->running && t->mode == TK_MODE_SEQ && (n == 2u || !p[2])) {
            if (tk_banks.pending[track] != p[1]) {
                tk_banks.pending[track] = p[1];
                tk_banks.revision = (uint16_t)((tk_banks.revision + 1u) & 16383u);
                flags = TK_REMOTE_CHANGED;
            }
        } else if (tk_bank_activate(s, t, track, p[1], t->next_step))
            flags = TK_REMOTE_CHANGED | TK_REMOTE_DIRTY;
        out[0] = TK_REMOTE_OK;
        break;
    case TK_REMOTE_SET_BANK_STEP:
    case TK_REMOTE_SONG_STEP:
        if (n < 4u || p[0] >= TK_TRACKS || p[1] >= TK_BANK_COUNT || p[2] >= TK_PATTERN_STEPS) break;
        track = p[0];
        if (request->command == TK_REMOTE_SONG_STEP && !tk_banks.importing) break;
        if (t->recording && t->selected == track) { out[0] = TK_REMOTE_BUSY; break; }
        {
            tk_pattern_step_t next, *target;
            if (!tk_remote_parse_step(&next, p + 3u, n - 3u)) break;
            tk_banks_observe(s);
            target = request->command == TK_REMOTE_SONG_STEP ?
                &tk_bank_pool.stage.pattern[track][p[1]].step[p[2]] :
                &tk_bank_pattern(s, track, p[1])->step[p[2]];
            if (memcmp(target, &next, sizeof next)) {
                *target = next;
                if (request->command == TK_REMOTE_SET_BANK_STEP) {
                    if (p[1] == tk_banks.active[track]) { tk_banks_observe(s); flags = TK_REMOTE_CHANGED | TK_REMOTE_DIRTY; }
                    else { tk_bank_changed(track, p[1]); flags = TK_REMOTE_CHANGED; }
                }
            }
        }
        if (request->command == TK_REMOTE_SET_BANK_STEP)
            *out_n = tk_remote_bank_step(s, track, p[1], p[2], out);
        else out[0] = TK_REMOTE_OK;
        break;
    case TK_REMOTE_SET_BANK_LENGTH:
    case TK_REMOTE_SONG_LENGTH:
        if (n != 3u || p[0] >= TK_TRACKS || p[1] >= TK_BANK_COUNT || p[2] > 2u) break;
        track = p[0]; i = 16u << p[2];
        if (request->command == TK_REMOTE_SONG_LENGTH) {
            if (!tk_banks.importing) break;
            tk_bank_pool.stage.length[track][p[1]] = (uint8_t)i;
        } else {
            if (t->recording && t->selected == track) { out[0] = TK_REMOTE_BUSY; break; }
            tk_banks_observe(s);
            if (tk_banks.length[track][p[1]] != i) {
                if (p[1] == tk_banks.active[track]) {
                    transport_release_part(t, track); tk_all_sound_off(TRACK_SOURCE[track]);
                    s->length[track] = (uint8_t)i;
                    if (t->selected == track) t->edit_step %= i;
                    tk_banks_observe(s); flags = TK_REMOTE_CHANGED | TK_REMOTE_DIRTY;
                } else {
                    tk_banks.length[track][p[1]] = (uint8_t)i;
                    tk_bank_changed(track, p[1]); flags = TK_REMOTE_CHANGED;
                }
            }
        }
        out[0] = TK_REMOTE_OK;
        break;
    case TK_REMOTE_COPY_BANK:
    case TK_REMOTE_CLEAR_BANK:
        if (n != (request->command == TK_REMOTE_COPY_BANK ? 3u : 2u)
            || p[0] >= TK_TRACKS || p[1] >= TK_BANK_COUNT
            || (n == 3u && p[2] >= TK_BANK_COUNT)) break;
        track = p[0];
        if (t->recording && t->selected == track) { out[0] = TK_REMOTE_BUSY; break; }
        {
            uint32_t bank = n == 3u ? p[2] : p[1];
            tk_pattern_t *target;
            tk_banks_observe(s); target = tk_bank_pattern(s, track, bank);
            if (n == 3u && bank == p[1]) { out[0] = TK_REMOTE_OK; break; }
            if (bank == tk_banks.active[track]) {
                transport_release_part(t, track); tk_all_sound_off(TRACK_SOURCE[track]);
            }
            if (n == 3u) {
                *target = *tk_bank_pattern(s, track, p[1]);
                tk_banks.length[track][bank] = tk_banks.length[track][p[1]];
                if (bank == tk_banks.active[track]) s->length[track] = tk_banks.length[track][bank];
            } else tk_pattern_clear(target);
            if (bank == tk_banks.active[track]) {
                if (track == t->selected) t->edit_step %= s->length[track];
                tk_banks_observe(s); flags = TK_REMOTE_CHANGED | TK_REMOTE_DIRTY;
            } else { tk_bank_changed(track, bank); flags = TK_REMOTE_CHANGED; }
        }
        out[0] = TK_REMOTE_OK;
        break;
    case TK_REMOTE_SONG_BEGIN:
        if (n) break;
        if (t->recording) { out[0] = TK_REMOTE_BUSY; break; }
        transport_solo_set(t, s, TK_TRACKS, fm1_ms);
        tk_banks_cancel_pending(); transport_stop(t);
        tk_song_clone(s, t); tk_banks.importing = 1; tk_banks.import_failed = 0;
        flags = TK_REMOTE_CHANGED; out[0] = TK_REMOTE_OK;
        break;
    case TK_REMOTE_SONG_PATCH:
        if (n != 55u || p[0] >= TK_TRACKS || !tk_banks.importing) break;
        {
            tk_track_patch_t patch; tk_fm_patch_t fm;
            memcpy(&patch, p + 1, 14u); tk_remote_fm_unpack(&fm, p + 15);
            if (!track_patch_valid(&patch) || !tk_fm_valid(&fm)) break;
            tk_bank_pool.stage.patch[p[0]] = patch; tk_bank_pool.stage.fm[p[0]] = fm;
        }
        out[0] = TK_REMOTE_OK;
        break;
    case TK_REMOTE_SONG_GLOBAL:
        if ((n != 10u && n != 14u) || !tk_banks.importing || p[1] > 1u || p[2] >= 16u
            || p[3] >= TK_TRACKS || p[8] > 127u || p[9] > 1u) break;
        if (n == 14u && (p[10] > 1u || p[11] > 127u || p[12] < 1u || p[12] > 12u
            || (p[13] != 4u && p[13] != 8u))) break;
        i = p[0] | p[1] << 7;
        if (i < 40u || i > 180u) break;
        for (track = 0; track < TK_TRACKS; track++) if (p[4u + track] >= TK_BANK_COUNT) {
            tk_banks.import_failed = 1; return 0;
        }
        tk_bank_pool.stage.tempo = (uint8_t)i; tk_bank_pool.stage.mute = p[2];
        tk_bank_pool.stage.selected = p[3];
        for (track = 0; track < TK_TRACKS; track++) tk_bank_pool.stage.active[track] = p[4u + track];
        tk_bank_pool.stage.ghost_mask = p[8]; tk_bank_pool.stage.fx = p[9]; out[0] = TK_REMOTE_OK;
        tk_bank_pool.stage.metro_enabled = n == 14u ? p[10] : 0u;
        tk_bank_pool.stage.metro_level = n == 14u ? p[11] : 32u;
        tk_bank_pool.stage.metro_beats = n == 14u ? p[12] : 4u;
        tk_bank_pool.stage.metro_unit = n == 14u ? p[13] : 4u;
        break;
    case TK_REMOTE_SONG_COMMIT:
        if (n || !tk_banks.importing || tk_banks.import_failed || !tk_song_valid(&tk_bank_pool.stage)) break;
        tk_song_commit(s, t);
        for (track = 0; track < TK_TRACKS; track++) tk_remote_track_refresh(s, t, track, track_lfo, track_shade);
        flags = TK_REMOTE_CHANGED | TK_REMOTE_DIRTY; out[0] = TK_REMOTE_OK;
        break;
    case TK_REMOTE_SONG_ABORT:
        if (n) break;
        tk_banks.importing = 0; out[0] = TK_REMOTE_OK; flags = TK_REMOTE_CHANGED;
        break;
    case TK_REMOTE_OPERATOR:
        if (n != 3u || p[0] >= 6u || p[1] > 5u
            || (!p[1] && p[2] > 31u) || PRESETS[s->preset].instrument != TK_INSTRUMENT_FM) break;
        graft_bake(s);
        switch (p[1]) {
        case 0: s->fm[0].op[p[0]].ratio = p[2]; break;
        case 1: s->fm[0].op[p[0]].level = p[2]; break;
        case 2: s->fm[0].op[p[0]].attack = p[2]; break;
        case 3: s->fm[0].op[p[0]].decay = p[2]; break;
        case 4: s->fm[0].op[p[0]].sustain = p[2]; break;
        default: s->fm[0].op[p[0]].release = p[2]; break;
        }
        flags = TK_REMOTE_CHANGED | TK_REMOTE_DIRTY; out[0] = TK_REMOTE_OK;
        break;
    case TK_REMOTE_FM_GLOBAL:
        if (n != 2u || p[0] > 3u || (p[0] == 0u && p[1] > 7u)
            || (p[0] == 2u && p[1] > 48u) || PRESETS[s->preset].instrument != TK_INSTRUMENT_FM) break;
        graft_bake(s);
        switch (p[0]) {
        case 0: s->fm[0].algorithm = p[1]; break;
        case 1: s->fm[0].feedback = p[1]; break;
        case 2: s->fm[0].pitch_depth = p[1]; break;
        default: s->fm[0].pitch_time = p[1]; break;
        }
        flags = TK_REMOTE_CHANGED | TK_REMOTE_DIRTY; out[0] = TK_REMOTE_OK;
        break;
    case TK_REMOTE_VOICE_MODE:
        if (n != 1u || p[0] > 1u) break;
        if (s->mono != p[0]) {
            s->mono = p[0]; tk_set_mono(s->mono);
            flags = TK_REMOTE_CHANGED | TK_REMOTE_DIRTY;
        }
        out[0] = TK_REMOTE_OK;
        break;
    default: out[0] = TK_REMOTE_UNSUPPORTED; break;
    }
    if (tk_banks.importing && request->command >= TK_REMOTE_SONG_STEP
        && request->command <= TK_REMOTE_SONG_GLOBAL && out[0] != TK_REMOTE_OK)
        tk_banks.import_failed = 1;
    return flags;
}

static uint32_t tk_remote_service(tk_settings_t *s, tk_transport_t *t,
                                 uint8_t *live_shade, tk_lfo_t *lfo,
                                 tk_lfo_t track_lfo[TK_TRACKS], uint8_t track_shade[TK_TRACKS],
                                 uint32_t page, uint32_t dirty)
{
    const uint8_t *input;
    uint8_t request[TK_REMOTE_DATA_MAX], output[TK_REMOTE_PAYLOAD_MAX];
    uint32_t n, out_n = 1, flags = 0, generation = midi_remote_generation();
    tk_remote_message_t message;
    int decoded;
    if (generation != remote_generation) {
        midi_live_drum_set(t, s, 0u);
        if (transport_solo_set(t, s, TK_TRACKS, fm1_ms)) flags |= TK_REMOTE_CHANGED;
        tk_screen_reset(generation);
        tk_banks.importing = 0; /* an interrupted import never publishes its partial stage */
        remote_generation = generation; remote_last_valid = remote_reply_pending = remote_observed_valid = 0;
        remote_last_length = remote_reply_length = remote_cached_length = remote_revision = remote_pattern_revision = 0;
    }
    if (remote_reply_pending) {
        if (!midi_remote_send(remote_reply, remote_reply_length)) return flags;
        remote_reply_pending = 0;
    }
    if (!midi_remote_get(&input, &n)) return flags;
    if (n > sizeof request) { midi_remote_done(); return flags; }
    memcpy(request, input, n); midi_remote_done();
    if (generation != midi_remote_generation()) return flags; /* disconnected during the copy */
    decoded = tk_remote_decode(request, n, &message);
    if (!decoded || decoded == 2) return flags; /* foreign/reply never executes */
    if (decoded < 0) {
        if (n < 8u || request[4] != TK_REMOTE_REQUEST || request[5] > 127u || request[6] > 127u) return flags;
        output[0] = decoded == -2 ? TK_REMOTE_UNSUPPORTED : TK_REMOTE_BAD_FRAME;
        remote_reply_length = tk_remote_encode(remote_reply, sizeof remote_reply, TK_REMOTE_REPLY,
            request[5], request[6], output, 1u);
    } else if (remote_last_valid && request[6] == remote_last_request[6]) {
        if (n != remote_last_length || memcmp(request, remote_last_request, n)) {
            /* Preserve the original cached reply while rejecting ID reuse. */
            uint8_t conflict[TK_REMOTE_WIRE_MAX];
            output[0] = TK_REMOTE_ID_CONFLICT;
            n = tk_remote_encode(conflict, sizeof conflict, TK_REMOTE_REPLY, message.command, message.id, output, 1u);
            (void)midi_remote_send(conflict, n);
            return flags;
        }
        /* The original complete reply is retained, not the current playhead.
         * Retrying START/REC can never execute the destructive action again. */
        memcpy(remote_reply, remote_cached_reply, remote_cached_length);
        remote_reply_length = remote_cached_length;
    } else {
        flags |= tk_remote_command(&message, s, t, live_shade, lfo, track_lfo, track_shade,
            page, dirty, output, &out_n);
        memcpy(remote_last_request, request, n); remote_last_length = n; remote_last_valid = 1;
        remote_reply_length = tk_remote_encode(remote_reply, sizeof remote_reply, TK_REMOTE_REPLY,
            message.command, message.id, output, out_n);
        memcpy(remote_cached_reply, remote_reply, remote_reply_length);
        remote_cached_length = remote_reply_length;
    }
    remote_reply_pending = remote_reply_length != 0u;
    if (remote_reply_pending && midi_remote_send(remote_reply, remote_reply_length)) remote_reply_pending = 0;
    return flags;
}
static void tk_remote_screen_service(int allowed)
{
    const uint8_t *pending;
    uint32_t n;
    /* Peek without consuming: a newly received command always precedes screen
     * traffic, including a reply that could not yet reserve the USB ring. */
    tk_screen_pump(fm1_ms, midi_remote_generation(), allowed && !remote_reply_pending &&
        !midi_remote_get(&pending, &n), midi_remote_screen_send);
}
#endif
