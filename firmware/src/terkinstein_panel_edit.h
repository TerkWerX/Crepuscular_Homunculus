/* SPDX-License-Identifier: GPL-3.0-only */
/* Bounded main-owned captured-track editor. Never used from audio. */
#ifndef TERKINSTEIN_PANEL_EDIT_H
#define TERKINSTEIN_PANEL_EDIT_H
typedef struct {
    tk_track_patch_t patch, graft;
    tk_fm_patch_t fm, graft_fm;
    uint8_t target, amount;
} tk_panel_sound_t;
static struct {
    tk_panel_sound_t live, edit;
    tk_lfo_t live_lfo;
    uint8_t live_shade, loaded, valid;
} tk_panel_editor;

static void tk_panel_sound_read(tk_panel_sound_t *out, const tk_settings_t *s)
{
    track_patch_capture(&out->patch, s);
    out->fm = s->fm[0]; out->graft = s->graft_base; out->graft_fm = s->fm[5];
    out->target = s->graft_target; out->amount = s->graft_amount;
}
static void tk_panel_sound_write(tk_settings_t *s, const tk_panel_sound_t *in)
{
    const tk_track_patch_t *p = &in->patch;
    s->p = p->p; s->preset = p->preset; s->fx = p->fx; s->memory = p->memory;
    s->attack = p->attack; s->release = p->release;
    s->lfo_on = p->lfo_on; s->lfo_shape = p->lfo_shape;
    s->lfo_rate = p->lfo_rate; s->lfo_depth = p->lfo_depth;
    s->fm[0] = in->fm; s->graft_base = in->graft; s->fm[5] = in->graft_fm;
    s->graft_target = in->target; s->graft_amount = in->amount;
}
static void tk_panel_editor_begin(tk_settings_t *s, uint8_t *shade, tk_lfo_t *lfo,
                                 tk_lfo_t track_lfo[TK_TRACKS], uint8_t track_shade[TK_TRACKS])
{
    uint32_t track = tk_panel_edit_track;
    uint32_t reload;
    if (track >= TK_TRACKS || tk_panel_editor.loaded || tk_banks.importing) return;
    tk_panel_sound_read(&tk_panel_editor.live, s);
    tk_panel_editor.live_lfo = *lfo; tk_panel_editor.live_shade = *shade;
    reload = !tk_panel_editor.valid || memcmp(&tk_panel_editor.edit.patch, &s->track[track], sizeof s->track[track]) ||
        memcmp(&tk_panel_editor.edit.fm, &s->fm[track + 1u], sizeof s->fm[0]);
    if (reload) {
        /* External edits invalidate a pending morph, without overwriting them. */
        tk_panel_editor.edit.patch = s->track[track];
        tk_panel_editor.edit.fm = s->fm[track + 1u];
        tk_panel_editor.edit.graft = s->track[track];
        tk_panel_editor.edit.graft_fm = s->fm[track + 1u];
        tk_panel_editor.edit.target = s->track[track].preset;
        tk_panel_editor.edit.amount = 0u; tk_panel_editor.valid = 1u;
    }
    tk_panel_sound_write(s, &tk_panel_editor.edit);
    if (reload) { graft_reset(s); tk_panel_sound_read(&tk_panel_editor.edit, s); }
    *lfo = track_lfo[track]; *shade = track_shade[track];
    tk_panel_editor.loaded = 1u;
}
static uint32_t tk_panel_editor_end(tk_settings_t *s, tk_transport_t *t,
    uint8_t *shade, tk_lfo_t *lfo, tk_lfo_t track_lfo[TK_TRACKS], uint8_t track_shade[TK_TRACKS])
{
    uint32_t track = tk_panel_edit_track, changed = 0u, i;
    if (!tk_panel_editor.loaded) return 0u;
    tk_panel_sound_read(&tk_panel_editor.edit, s);
    /* The ordinary main-loop zero-amount rebase runs after LIVE is restored.
     * Capture the edited track's final values here too, after a field change,
     * so its next GRAFT turn cannot resurrect the pre-edit patch/FM base. */
    if (!tk_panel_editor.edit.amount) {
        tk_panel_editor.edit.graft = tk_panel_editor.edit.patch;
        tk_panel_editor.edit.graft_fm = tk_panel_editor.edit.fm;
    }
    if (track < TK_TRACKS) {
        const tk_track_patch_t *p = &tk_panel_editor.edit.patch;
        changed = memcmp(&s->track[track], p, sizeof *p) ||
            memcmp(&s->fm[track + 1u], &s->fm[0], sizeof s->fm[0]);
        if (changed) {
            if (s->track[track].preset != p->preset || s->track[track].instrument != p->instrument) {
                transport_release_part(t, track); tk_all_sound_off(TRACK_SOURCE[track]);
            }
            for (i = 0; i < 4u; i++)
                if (macro_get(&s->track[track].p, i) != macro_get(&p->p, i))
                    tk_arr_record_knob(t, track, i, macro_get(&p->p, i));
            s->track[track] = *p; s->fm[track + 1u] = s->fm[0];
            publish_track(s, track); tk_follow_source_macros(TRACK_SOURCE[track]);
        }
        track_lfo[track] = *lfo; track_shade[track] = *shade;
    }
    tk_panel_sound_write(s, &tk_panel_editor.live);
    *lfo = tk_panel_editor.live_lfo; *shade = tk_panel_editor.live_shade;
    tk_panel_editor.loaded = 0u;
    return changed;
}
static void tk_panel_editor_publish(const tk_settings_t *s)
{
    uint32_t track = tk_panel_audition_track, source;
    if (track >= TK_TRACKS) {
        tk_source_patch_timing[0] = tk_source_macro_control[0] = 0u;
        tk_panel_patch_bank = 0u;
        return;
    }
    source = TRACK_SOURCE[track];
    tk_source_instrument_control[0] = tk_source_instrument_control[source];
    tk_source_patch_word[0] = tk_source_patch_word[source];
    tk_source_macro_control[0] = tk_source_macro_control[source];
    tk_source_shade_control[0] = tk_source_shade_control[source];
    tk_source_memory_control[0] = tk_source_memory_control[source];
    tk_panel_patch_bank = source - 32u;
    tk_source_patch_timing[0] = tk_source_patch_timing[source];
    /* ARP is generated from panel keys and follows their displayed sound. */
    tk_source_instrument_control[TK_SOURCE_ARP] = tk_source_instrument_control[source];
    tk_source_patch_word[TK_SOURCE_ARP] = tk_source_patch_word[source];
    tk_source_macro_control[TK_SOURCE_ARP] = tk_source_macro_control[source];
    tk_source_shade_control[TK_SOURCE_ARP] = tk_source_shade_control[source];
    tk_source_memory_control[TK_SOURCE_ARP] = tk_source_memory_control[source];
    tk_source_patch_timing[TK_SOURCE_ARP] = tk_source_patch_timing[source];
    tk_set_source_fm_patch(TK_SOURCE_ARP, &s->fm[track + 1u]);
}
static void tk_panel_editor_bind(const tk_settings_t *s, tk_transport_t *t, uint32_t new_gesture)
{
    uint32_t i;
    if (tk_panel_audition_track == tk_panel_edit_track) return;
    /* Navigation must not cut a held chord or pedal. Existing release tails
     * also finish unless the player deliberately starts a new gesture. */
    for (i = 0; i < 128u; i++) if (panel_refs[i]) return;
    if (tk_source_control[0] & (TK_SUSTAIN | TK_SOSTENUTO)) return;
    if (!new_gesture) for (i = 0; i < TK_VOICES; i++) {
        uint32_t cmd = tk_voice[i].command;
        if (!((cmd >> 14) & 63u) && ((cmd & TK_GATE) || tk_voice[i].tail)) return;
    }
    tk_all_sound_off(0u); tk_forget_panel_memory();
    if (t->mode == TK_MODE_ARP) transport_release_part(t, TK_TRACKS);
    tk_panel_audition_track = tk_panel_edit_track;
    tk_panel_editor_publish(s);
}
static void tk_panel_editor_select(tk_settings_t *s, tk_transport_t *t, uint32_t track,
    uint8_t *shade, tk_lfo_t *lfo, tk_lfo_t track_lfo[TK_TRACKS], uint8_t track_shade[TK_TRACKS], uint32_t *hold)
{
    (void)hold;
    if (track >= TK_TRACKS) track = TK_TRACKS;
    if (track == tk_panel_edit_track) return;
    tk_panel_editor_end(s, t, shade, lfo, track_lfo, track_shade);
    tk_panel_edit_track = (uint8_t)track; tk_panel_editor.valid = 0u;
    tk_panel_editor_bind(s, t, 0u);
    tk_panel_editor_begin(s, shade, lfo, track_lfo, track_shade);
}
static void tk_panel_editor_sequence(tk_settings_t *s, tk_transport_t *t, uint32_t page,
    uint8_t *shade, tk_lfo_t *lfo, tk_lfo_t track_lfo[TK_TRACKS], uint8_t track_shade[TK_TRACKS], uint32_t *hold)
{
    if (t->recording || tk_banks.importing) return;
    /* Step entry must audition/edit the capture that will play the notes.
     * The overview keeps independent LIVE/legacy REC behavior. Run before
     * note input and after navigation so track changes cannot leave a stale
     * workbench or redirect PRESETS to the invisible LIVE patch. */
    if (page == TK_PAGE_SEQ || page == TK_PAGE_PIANO)
        tk_panel_editor_select(s, t,
            page == TK_PAGE_PIANO || t->editing ? t->selected : TK_TRACKS,
            shade, lfo, track_lfo, track_shade, hold);
}
#endif
