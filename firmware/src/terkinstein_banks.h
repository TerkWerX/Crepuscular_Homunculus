/* SPDX-License-Identifier: GPL-3.0-only */
/* Main-owned A-D pattern banks and a complete, isolated song import stage.
 * Included after settings validation and transport note-release helpers.
 * The audio interrupt never reads this storage or performs a bank switch. */
#ifndef TERKINSTEIN_BANKS_H
#define TERKINSTEIN_BANKS_H
#define TK_BANK_COUNT 4u
#define TK_BANK_NONE 4u
typedef struct {
    tk_pattern_t pattern[TK_TRACKS][TK_BANK_COUNT];
    tk_track_patch_t patch[TK_TRACKS];
    tk_fm_patch_t fm[TK_TRACKS];
    uint8_t length[TK_TRACKS][TK_BANK_COUNT];
    uint8_t tempo, mute, selected, active[TK_TRACKS], ghost_mask, fx;
    uint8_t metro_enabled, metro_level, metro_beats, metro_unit;
    uint16_t section_mutes;
    tk_drum_mix_t drum_mix[TK_DRUM_LANES];
    tk_arr_song_t arrangement;
} tk_song_stage_t;
typedef struct {
    tk_pattern_t alternate[TK_TRACKS][TK_BANK_COUNT - 1u];
    tk_pattern_t scratch, observed[TK_TRACKS];
    tk_song_stage_t stage;
} tk_bank_pool_t;
#ifdef TERKINSTEIN_HOST
static tk_bank_pool_t tk_bank_pool;
#else
static tk_bank_pool_t tk_bank_pool __attribute__((section(".pool")));
#endif
static struct {
    tk_settings_t *settings;
    uint8_t active[TK_TRACKS], pending[TK_TRACKS], map[TK_TRACKS][TK_BANK_COUNT];
    uint8_t head_pending[TK_TRACKS]; /* newly selected bank has not sounded its first step yet */
    uint8_t length[TK_TRACKS][TK_BANK_COUNT], dirty[TK_TRACKS], importing, import_failed;
    uint16_t revision, bank_revision[TK_TRACKS][TK_BANK_COUNT];
} tk_banks;

static uint8_t tk_transport_local_step(const tk_transport_t *t,
                                       uint32_t track, uint32_t global, uint32_t length)
{
    return (uint8_t)(((global + TK_PATTERN_STEPS - t->phase_origin[track])
        % TK_PATTERN_STEPS) % length);
}
static tk_pattern_t *tk_bank_pattern(tk_settings_t *s, uint32_t track, uint32_t bank)
{
    uint32_t buffer = tk_banks.map[track][bank];
    return buffer ? &tk_bank_pool.alternate[track][buffer - 1u] : &s->pattern[track];
}
static void tk_bank_changed(uint32_t track, uint32_t bank)
{
    tk_arr.dirty = 1; /* Physical SAVE now covers inactive banks as well. */
    tk_banks.bank_revision[track][bank] = (uint16_t)((tk_banks.bank_revision[track][bank] + 1u) & 16383u);
    tk_banks.revision = (uint16_t)((tk_banks.revision + 1u) & 16383u);
    tk_banks.dirty[track] |= (uint8_t)(1u << bank);
}
static void tk_banks_observe(tk_settings_t *s)
{
    uint32_t track;
    for (track = 0; track < TK_TRACKS; track++) {
        uint32_t bank = tk_banks.active[track];
        if (tk_banks.length[track][bank] != s->length[track]
            || memcmp(&tk_bank_pool.observed[track], &s->pattern[track], sizeof(tk_pattern_t))) {
            tk_banks.length[track][bank] = s->length[track];
            memcpy(&tk_bank_pool.observed[track], &s->pattern[track], sizeof(tk_pattern_t));
            tk_bank_changed(track, bank);
        }
    }
}
static void tk_banks_init(tk_settings_t *s, tk_transport_t *t)
{
    uint32_t track, bank;
    memset(&tk_banks, 0, sizeof tk_banks);
    memset(&tk_bank_pool, 0, sizeof tk_bank_pool);
    tk_arr_defaults(); tk_drums_defaults(tk_drum_mix);
    tk_banks.settings = s;
    for (track = 0; track < TK_TRACKS; track++) {
        tk_banks.pending[track] = TK_BANK_NONE;
        t->phase_origin[track] = 0;
        memcpy(&tk_bank_pool.observed[track], &s->pattern[track], sizeof(tk_pattern_t));
        for (bank = 0; bank < TK_BANK_COUNT; bank++) {
            tk_banks.map[track][bank] = (uint8_t)bank;
            tk_banks.length[track][bank] = s->length[track];
        }
    }
}
static void tk_banks_cancel_pending(void)
{
    uint32_t track;
    for (track = 0; track < TK_TRACKS; track++) tk_banks.pending[track] = TK_BANK_NONE;
    tk_banks.revision = (uint16_t)((tk_banks.revision + 1u) & 16383u);
}
static int tk_bank_activate(tk_settings_t *s, tk_transport_t *t,
                             uint32_t track, uint32_t bank, uint32_t global_step)
{
    uint32_t old = tk_banks.active[track], buffer;
    tk_pattern_t *next;
    tk_banks.pending[track] = TK_BANK_NONE;
    if (old == bank) return 0;
    tk_banks_observe(s);
    buffer = tk_banks.map[track][bank]; next = tk_bank_pattern(s, track, bank);
    transport_release_part(t, track);
    tk_all_sound_off(TRACK_SOURCE[track]);
    memcpy(&tk_bank_pool.scratch, &s->pattern[track], sizeof(tk_pattern_t));
    memcpy(&s->pattern[track], next, sizeof(tk_pattern_t));
    memcpy(next, &tk_bank_pool.scratch, sizeof(tk_pattern_t));
    tk_banks.map[track][old] = (uint8_t)buffer;
    tk_banks.map[track][bank] = 0;
    tk_banks.active[track] = (uint8_t)bank;
    tk_arr.cycle[track] = 0; tk_arr.seen[track] = 0;
    tk_banks.head_pending[track] = 1;
    s->length[track] = tk_banks.length[track][bank];
    t->phase_origin[track] = (uint8_t)global_step;
    if (t->selected == track) t->edit_step %= s->length[track];
    memcpy(&tk_bank_pool.observed[track], &s->pattern[track], sizeof(tk_pattern_t));
    tk_banks.revision = (uint16_t)((tk_banks.revision + 1u) & 16383u);
    return 1;
}
static int tk_banks_stopped(tk_transport_t *t)
{
    uint32_t track; int changed = 0;
    if (!tk_banks.settings || tk_banks.importing) return 0;
    for (track = 0; track < TK_TRACKS; track++)
        if (tk_banks.pending[track] < TK_BANK_COUNT)
            changed |= tk_bank_activate(tk_banks.settings, t, track, tk_banks.pending[track], t->next_step);
    return changed;
}
static int tk_banks_boundary(tk_settings_t *s, tk_transport_t *t, uint32_t global_step)
{
    uint32_t track; int changed = 0;
    for (track = 0; track < TK_TRACKS; track++)
        if (tk_banks.pending[track] < TK_BANK_COUNT
            && !(t->recording && t->selected == track)
            && !tk_transport_local_step(t, track, global_step, s->length[track]))
            changed |= tk_bank_activate(s, t, track, tk_banks.pending[track], global_step);
    return changed;
}
static void tk_song_clone(tk_settings_t *s, const tk_transport_t *t)
{
    tk_song_stage_t *stage = &tk_bank_pool.stage;
    uint32_t track, bank;
    tk_banks_observe(s);
    for (track = 0; track < TK_TRACKS; track++) {
        stage->patch[track] = s->track[track]; stage->fm[track] = s->fm[track + 1u];
        stage->active[track] = tk_banks.active[track];
        for (bank = 0; bank < TK_BANK_COUNT; bank++) {
            stage->pattern[track][bank] = *tk_bank_pattern(s, track, bank);
            stage->length[track][bank] = tk_banks.length[track][bank];
        }
    }
    stage->tempo = s->tempo; stage->mute = s->track_mute; stage->selected = t->selected;
    stage->ghost_mask = s->ghost_mask; stage->fx = s->fx;
    stage->metro_enabled = s->metro_enabled; stage->metro_level = s->metro_level;
    stage->metro_beats = s->metro_beats; stage->metro_unit = s->metro_unit;
    stage->section_mutes = s->section_mutes;
    memcpy(stage->drum_mix, tk_drum_mix, sizeof tk_drum_mix);
    stage->arrangement = tk_arr_song;
}
static int tk_song_valid(const tk_song_stage_t *stage)
{
    uint32_t track, bank;
    if (stage->tempo < 40u || stage->tempo > 180u || stage->mute >= 16u
        || stage->selected >= TK_TRACKS || stage->ghost_mask > 127u || stage->fx > 1u
        || stage->metro_enabled > 1u || stage->metro_level > 127u
        || stage->metro_beats < 1u || stage->metro_beats > 12u
        || (stage->metro_unit != 4u && stage->metro_unit != 8u)
        || !tk_drums_valid(stage->drum_mix) || !tk_arr_song_valid(&stage->arrangement)) return 0;
    for (track = 0; track < TK_TRACKS; track++) {
        if (stage->active[track] >= TK_BANK_COUNT || !track_patch_valid(&stage->patch[track])
            || !tk_fm_valid(&stage->fm[track])) return 0;
        for (bank = 0; bank < TK_BANK_COUNT; bank++)
            if ((stage->length[track][bank] != 16u && stage->length[track][bank] != 32u
                && stage->length[track][bank] != 64u) || !pattern_valid(&stage->pattern[track][bank])) return 0;
    }
    return 1;
}
static void tk_song_commit(tk_settings_t *s, tk_transport_t *t)
{
    const tk_song_stage_t *stage = &tk_bank_pool.stage;
    uint32_t track, bank;
    for (track = 0; track < TK_TRACKS; track++) {
        uint32_t alternate = 1u;
        transport_release_part(t, track); tk_all_sound_off(TRACK_SOURCE[track]);
        tk_banks.active[track] = stage->active[track]; tk_banks.pending[track] = TK_BANK_NONE;
        tk_banks.head_pending[track] = 0;
        t->phase_origin[track] = 0;
        for (bank = 0; bank < TK_BANK_COUNT; bank++) {
            tk_banks.map[track][bank] = bank == stage->active[track] ? 0u : (uint8_t)alternate++;
            *tk_bank_pattern(s, track, bank) = stage->pattern[track][bank];
            tk_banks.length[track][bank] = stage->length[track][bank];
            tk_bank_changed(track, bank);
        }
        s->length[track] = stage->length[track][stage->active[track]];
        s->track[track] = stage->patch[track]; s->fm[track + 1u] = stage->fm[track];
        memcpy(&tk_bank_pool.observed[track], &s->pattern[track], sizeof(tk_pattern_t));
    }
    s->tempo = stage->tempo; s->track_mute = stage->mute;
    s->ghost_mask = stage->ghost_mask; s->fx = stage->fx;
    s->metro_enabled = stage->metro_enabled; s->metro_level = stage->metro_level;
    s->metro_beats = stage->metro_beats; s->metro_unit = stage->metro_unit;
    s->section_mutes = stage->section_mutes;
    memcpy(tk_drum_mix, stage->drum_mix, sizeof tk_drum_mix);
    tk_arr_song = stage->arrangement;
    memset(&tk_arr, 0, sizeof tk_arr); tk_arr.scene = tk_arr.pending_scene = TK_SCENE_NONE; tk_arr.rng = 0x13DFA965u;
    publish_drum_mix();
    tk_metronome_cancel();
    tk_metronome_clock_configure(&metro_clock, s->metro_beats, s->metro_unit);
    tk_metronome_clock_reset(&metro_clock);
    t->selected = stage->selected; t->edit_step %= s->length[t->selected];
    t->running = t->recording = 0; t->next_step = t->shown_step = 0;
    t->record_pattern = 0; tk_banks.importing = 0;
}
#endif
