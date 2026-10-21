/* SPDX-License-Identifier: GPL-3.0-only */
/* Main-owned musical events. No extra synthesis voices or per-sample work. */
#ifndef TERKINSTEIN_ARRANGEMENT_H
#define TERKINSTEIN_ARRANGEMENT_H
static void tk_arr_transport_reset(void)
{
    uint32_t track;
    for (track = 0; track < TK_TRACKS; track++) {
        tk_arr.cycle[track] = 0; tk_arr.seen[track] = 0; tk_arr.lock_mask[track] = 0;
        tk_arr.auto_touched[track] = 0;
    }
    tk_arr.fill = 0; tk_arr.pending_scene = TK_SCENE_NONE; tk_arr.rng = 0x13DFA965u;
}
static void tk_arr_changed(void)
{
    tk_arr.revision = (uint16_t)((tk_arr.revision + 1u) & 16383u);
    tk_arr.dirty = 1;
}
static void tk_arr_record_knob(tk_transport_t *t, uint32_t track, uint32_t knob, uint8_t value)
{
    tk_arr_step_t *step;
    uint32_t local;
    if (track >= TK_TRACKS || knob >= 4u || value > 127u || !(tk_arr.auto_armed & (1u << track))
        || !t->running || t->mode != TK_MODE_SEQ || tk_banks.importing) return;
    tk_arr.auto_touched[track] |= (uint8_t)(1u << knob); tk_arr.auto_value[track][knob] = value;
    tk_arr.lock_mask[track] |= (uint8_t)(1u << knob); tk_arr.value[track][knob] = value;
    local = tk_transport_local_step(t, track, t->shown_step, tk_banks.settings->length[track]);
    step = &tk_arr_song.step[track][tk_banks.active[track]][local];
    if (!(step->mask_condition & (1u << knob)) || step->value[knob] != value) {
        step->mask_condition |= (uint8_t)(1u << knob); step->value[knob] = value; tk_arr_changed();
    }
}
static int tk_arr_step_begin(tk_settings_t *s, tk_transport_t *t, uint32_t track, uint32_t local)
{
    tk_arr_step_t *step = &tk_arr_song.step[track][tk_banks.active[track]][local];
    uint32_t knob, old_mask = tk_arr.lock_mask[track], condition = step->mask_condition >> 4;
    int accepted;
    if (!local && tk_arr.seen[track]) tk_arr.cycle[track]++;
    tk_arr.seen[track] = 1;
    if (tk_arr.auto_armed & (1u << track)) {
        for (knob = 0; knob < 4u; knob++) if (tk_arr.auto_touched[track] & (1u << knob)) {
            if (!(step->mask_condition & (1u << knob)) || step->value[knob] != tk_arr.auto_value[track][knob]) {
                step->mask_condition |= (uint8_t)(1u << knob); step->value[knob] = tk_arr.auto_value[track][knob]; tk_arr_changed();
            }
        }
    }
    accepted = tk_arr_condition(condition, tk_arr.cycle[track], tk_arr.fill);
    if (accepted && step->probability < 100u) {
        uint32_t x = tk_arr.rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; tk_arr.rng = x;
        accepted = (x % 100u) < step->probability;
    }
    tk_arr.lock_mask[track] = accepted ? step->mask_condition & 15u : 0u;
    memcpy(tk_arr.value[track], step->value, 4u);
    if (old_mask || tk_arr.lock_mask[track]) {
        publish_track(s, track); tk_follow_source_macros(TRACK_SOURCE[track]);
    }
    (void)t; return accepted;
}
static int tk_arr_scene_apply(tk_settings_t *s, tk_transport_t *t, uint32_t scene, uint32_t global)
{
    const tk_scene_t *next;
    uint32_t track;
    if (scene >= TK_SCENES || !tk_arr_song.scene[scene].enabled) return 0;
    next = &tk_arr_song.scene[scene];
    for (track = 0; track < TK_TRACKS; track++) {
        tk_bank_activate(s, t, track, next->bank[track], global);
        t->phase_origin[track] = (uint8_t)global;
        tk_arr.cycle[track] = 0; tk_arr.seen[track] = 0;
        transport_release_part(t, track); tk_all_sound_off(TRACK_SOURCE[track]);
        if ((next->mute & (1u << track)) || tk_section_muted(next->section_mutes, track,
            tk_transport_local_step(t, track, global, s->length[track]))) {
            transport_release_part(t, track); tk_all_sound_off(TRACK_SOURCE[track]);
        }
    }
    s->track_mute = next->mute; s->section_mutes = next->section_mutes;
    if (next->tempo) s->tempo = next->tempo;
    tk_arr.scene = (uint8_t)scene; tk_arr.pending_scene = TK_SCENE_NONE; tk_arr_changed(); return 1;
}
static int tk_arr_scene_launch(tk_settings_t *s, tk_transport_t *t, uint32_t scene)
{
    if (scene >= TK_SCENES || !tk_arr_song.scene[scene].enabled || t->recording || tk_banks.importing) return -1;
    if (!t->running) return tk_arr_scene_apply(s, t, scene, 0);
    tk_arr.pending_scene = (uint8_t)scene; return 0;
}
static int tk_arr_scene_boundary(tk_settings_t *s, tk_transport_t *t, uint32_t step)
{
    if (!(step % 16u) && tk_arr.pending_scene < TK_SCENES)
        return tk_arr_scene_apply(s, t, tk_arr.pending_scene, step);
    return 0;
}
#endif
