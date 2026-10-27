/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TERKINSTEIN_ARRANGEMENT_UI_H
#define TERKINSTEIN_ARRANGEMENT_UI_H
static const char *const TK_CONDITION_NAMES[] = {"ALWAYS","FIRST","AFTER","1 OF 2","1 OF 4","FILL","NO FILL","4 OF 4"};
static void draw_arrangement(const tk_settings_t *s, const tk_transport_t *t, uint32_t page)
{
    static const char *const tracks[] = {"DRUM","MELODY","ATMOS","BASS"};
    static const char *const macro[] = {"K1 SHADE","K2 FRACTURE","K3 DRIFT","K4 BLOOM"};
    static const char *const kit[] = {"K1 BODY","K2 WIRE","K3 DECAY","K4 BITE"};
    static const char *const notes[] = {"K1 NOTE","K2 VELOCITY","K3 CHANCE %","K4 WHEN"};
    tk_arr_step_t *step = &tk_arr_song.step[t->selected][tk_banks.active[t->selected]][t->edit_step];
    char number[4], bank[2]; uint32_t i;
    if (page == TK_PAGE_SCENES) {
        const tk_scene_t *scene = &tk_arr_song.scene[tk_scene_focus];
        draw_text_box(8, 49, 112, &FONT_S, "SCENE", C_ACC, 0); draw_number(number, tk_scene_focus + 1u);
        draw_text_box(112, 49, 32, &FONT_S, number, TK_GREEN, 0);
        draw_text_box(156, 49, 76, &FONT_S, scene->enabled ? "READY" : "EMPTY", TK_DIM, 2);
        for (i = 0; i < TK_TRACKS; i++) {
            bank[0] = (char)('A' + scene->bank[i]); bank[1] = 0;
            draw_text_box(8, 78u + i * 24u, 152, &FONT_S, tracks[i], TK_DIM, 0);
            draw_text_box(184, 78u + i * 24u, 48, &FONT_S, bank, TK_GREEN, 2);
        }
        draw_text_box(8, 179, 152, &FONT_S, "ALGORITHM: BPM", TK_DIM, 0); draw_number(number, scene->tempo);
        draw_text_box(184, 179, 48, &FONT_S, scene->tempo ? number : "KEEP", TK_GREEN, 2);
        draw_text_box(8, 201, 224, &FONT_S, "SEL STORE / PLAY LAUNCH", C_ACC, 0);
        draw_text_box(8, 220, 224, &FONT_S, "SELECT SCENE / EDIT ON", TK_DIM, 0); return;
    }
    draw_text_box(8, 49, 96, &FONT_S, tracks[t->selected], C_ACC, 0);
    bank[0] = (char)('A' + tk_banks.active[t->selected]); bank[1] = 0;
    draw_text_box(112, 49, 32, &FONT_S, bank, TK_GREEN, 0); draw_number(number, t->edit_step + 1u);
    draw_text_box(168, 49, 64, &FONT_S, number, TK_GREEN, 2);
    draw_text_box(8, 71, 224, &FONT_S, page == TK_PAGE_STEP_EDIT ? "NOTE VALUES / CONDITIONS" :
        tk_arr_lock_edit ? "STEP PARAMETER LOCKS" : tk_arr.auto_armed & (1u << t->selected) ? "AUTOMATION RECORD ARMED" : "CAPTURED TRACK SOUND", C_ACC, 0);
    for (i = 0; i < 4u; i++) {
        uint32_t value;
        const char *label, *text;
        if (page == TK_PAGE_STEP_EDIT) {
            const tk_pattern_step_t *part = &s->pattern[t->selected].step[t->edit_step];
            uint32_t gate = part->count ? part->note[0].duration_q4 : 0u;
            label = notes[i]; value = i == 1u ? (part->count ? part->note[0].velocity : 0u) : step->probability;
            draw_number(number, value);
            text = !i ? TK_NOTE_VALUE_NAMES[tk_note_value_index(gate)] : i == 3u ? TK_CONDITION_NAMES[step->mask_condition >> 4] : number;
        } else {
            label = s->track[t->selected].instrument == TK_INSTRUMENT_DRUM_KIT ? kit[i] : macro[i];
            value = tk_arr_lock_edit && (step->mask_condition & (1u << i)) ? step->value[i] : macro_get(&s->track[t->selected].p, i);
            draw_number(number, value); text = number;
        }
        draw_text_box(8, 90u + i * 22u, 152, &FONT_S, label, TK_DIM, 0);
        draw_text_box(160, 90u + i * 22u, 72, &FONT_S, text, TK_GREEN, 2);
    }
    draw_text_box(8, 182, 224, &FONT_S, page == TK_PAGE_STEP_EDIT ? "EDIT: CLEAR STEP LOCKS" : "EDIT LOCK / REC AUTO ARM", C_ACC, 0);
    draw_text_box(8, 201, 224, &FONT_S, page == TK_PAGE_STEP_EDIT ? "SEL BACK / HOLD: PIANO ROLL" : "SEL NOTES / GLO SCENES", TK_DIM, 0);
    draw_text_box(8, 220, 224, &FONT_S, "SELECT TRACK / ARP FILL", TK_DIM, 0);
}
static int tk_arr_page_encoder(tk_settings_t *s, tk_transport_t *t, uint32_t page, uint32_t encoder, int32_t delta)
{
    uint8_t next;
    if (page != TK_PAGE_ARRANGE && page != TK_PAGE_STEP_EDIT && page != TK_PAGE_SCENES) return 0;
    if (tk_banks.importing) return 1;
    if (page == TK_PAGE_SCENES) {
        if (!encoder) { tk_scene_focus = (uint8_t)(((int32_t)tk_scene_focus + delta % TK_SCENES + TK_SCENES) % TK_SCENES); return 5; }
        next = tk_arr_song.scene[tk_scene_focus].tempo;
        if (!next) next = delta > 0 ? 40u : 0u;
        else if (next == 40u && delta < 0) next = 0;
        else next = adjust_u8(next, delta, 40u, 180u);
        if (next == tk_arr_song.scene[tk_scene_focus].tempo) return 1;
        tk_arr_song.scene[tk_scene_focus].tempo = next; tk_arr_changed(); return 7;
    }
    if (!encoder) {
        t->selected = (uint8_t)(((int32_t)t->selected + delta % TK_TRACKS + TK_TRACKS) % TK_TRACKS);
        t->edit_step %= s->length[t->selected]; return 5;
    }
    t->edit_step = (uint8_t)(((int32_t)t->edit_step + delta % s->length[t->selected] + s->length[t->selected]) % s->length[t->selected]); return 5;
}
static int tk_arr_page_knob(tk_settings_t *s, tk_transport_t *t, uint32_t page, uint32_t knob, int32_t delta)
{
    tk_arr_step_t *step; uint8_t old, next; uint32_t i;
    if (page != TK_PAGE_ARRANGE && page != TK_PAGE_STEP_EDIT && page != TK_PAGE_SCENES) return 0;
    if (knob >= 4u || tk_banks.importing) return 1;
    if (page == TK_PAGE_SCENES) {
        old = tk_arr_song.scene[tk_scene_focus].bank[knob]; next = adjust_u8(old, delta, 0u, 3u);
        if (next == old) return 1;
        tk_arr_song.scene[tk_scene_focus].bank[knob] = next; tk_arr_changed(); return 7;
    }
    step = &tk_arr_song.step[t->selected][tk_banks.active[t->selected]][t->edit_step];
    if (page == TK_PAGE_STEP_EDIT) {
        tk_pattern_step_t *part = &s->pattern[t->selected].step[t->edit_step];
        if (t->recording) return 1;
        if (knob < 2u) {
            if (!part->count) return 1;
            old = knob ? part->note[0].velocity : part->note[0].duration_q4;
            next = knob ? adjust_u8(old, delta, 1u, 127u) : TK_NOTE_VALUES[adjust_u8((uint8_t)tk_note_value_index(old), delta, 0u, TK_NOTE_VALUE_COUNT - 1u)];
            if (next == old) return 1;
            for (i = 0; i < part->count; i++) if (knob) part->note[i].velocity = next; else part->note[i].duration_q4 = next;
            tk_banks_observe(s); return 7;
        }
        old = knob == 2u ? step->probability : step->mask_condition >> 4;
        next = adjust_u8(old, delta, 0u, knob == 2u ? 100u : 7u);
        if (next == old) return 1;
        if (knob == 2u) step->probability = next;
        else step->mask_condition = (uint8_t)((step->mask_condition & 15u) | next << 4);
        tk_arr_changed(); return 7;
    }
    old = tk_arr_lock_edit && (step->mask_condition & (1u << knob)) ? step->value[knob] : macro_get(&s->track[t->selected].p, knob);
    next = adjust_u8(old, delta, 0u, 127u);
    if (next == old) return 1;
    if (tk_arr_lock_edit) {
        if (t->recording) return 1;
        step->value[knob] = next; step->mask_condition |= (uint8_t)(1u << knob); tk_arr_changed();
    } else {
        macro_set(&s->track[t->selected].p, knob, next); tk_arr_record_knob(t, t->selected, knob, next);
        publish_track(s, t->selected); tk_follow_source_macros(TRACK_SOURCE[t->selected]);
    }
    return 7;
}
static void tk_scene_store_current(const tk_settings_t *s)
{
    tk_scene_t *scene = &tk_arr_song.scene[tk_scene_focus]; uint32_t track;
    for (track = 0; track < TK_TRACKS; track++) scene->bank[track] = tk_banks.active[track];
    scene->tempo = s->tempo; scene->mute = s->track_mute; scene->section_mutes = s->section_mutes;
    scene->enabled = 1u; scene->reserved = 0u; tk_arr_changed();
}
#endif
