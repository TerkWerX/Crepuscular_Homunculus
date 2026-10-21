/* SPDX-License-Identifier: GPL-3.0-only */
/* Real app helpers and DSP; host checks do not qualify physical timing. */
#define TERKINSTEIN_HOST 1
#define main panel_track_host_main
#include "../firmware/src/terkinstein.c"
#undef main
static uint32_t checks;
static tk_settings_t s, before;
static tk_transport_t t;
static tk_lfo_t lfo, track_lfo[TK_TRACKS];
static uint8_t shade, track_shade[TK_TRACKS];
static uint32_t hold;
static void check(int value, const char *why)
{
    checks++;
    if (!value) { fprintf(stderr, "FAIL panel track %u: %s\n", checks, why); exit(1); }
}
static void reset(void)
{
    uint32_t i;
    tk_init(); settings_defaults(&s); memset(&t, 0, sizeof t);
    memset(&tk_panel_editor, 0, sizeof tk_panel_editor);
    memset(panel_refs, 0, sizeof panel_refs); memset(panel_down, 0, sizeof panel_down);
    tk_panel_edit_track = tk_panel_audition_track = TK_TRACKS; hold = 0u; panel_arpeggiated = 0u;
    tk_banks_init(&s, &t); midi_performance_init(&s);
    preset_apply(&s, 63u); s.graft_amount = 37u; graft_apply(&s);
    shade = s.p.shade; tk_lfo_init(&lfo, 0u);
    for (i = 0; i < TK_TRACKS; i++) {
        track_factory_apply(&s, i, i == 0u ? 55u : i == 1u ? 43u : i == 2u ? 3u : 36u);
        s.track[i].attack = (uint8_t)(20u + i);
        s.track[i].p.shade = (uint8_t)(31u + i);
        s.fm[i + 1u].op[0].level = (uint8_t)(70u + i);
        tk_lfo_init(&track_lfo[i], 0u); track_shade[i] = s.track[i].p.shade;
        publish_track(&s, i);
    }
    publish_params(&s, shade); before = s;
}
static void begin(void) { tk_panel_editor_begin(&s, &shade, &lfo, track_lfo, track_shade); }
static uint32_t end(void) { return tk_panel_editor_end(&s, &t, &shade, &lfo, track_lfo, track_shade); }
static void select_track(uint32_t which)
{
    tk_panel_editor_select(&s, &t, which, &shade, &lfo, track_lfo, track_shade, &hold);
}
static void baked_base_roundtrip(void)
{
    static const uint32_t presets[3]={0u,14u,43u};
    uint32_t track,preset;tk_track_patch_t edited;tk_fm_patch_t fm;
    for(track=0u;track<TK_TRACKS;track++)for(preset=0u;preset<3u;preset++){
        reset();track_factory_apply(&s,track,presets[preset]);before=s;
        select_track(track);end();begin();
        check(tk_panel_value(&s,&s.p.shade,s.p.shade<127u?1:-1,127u)==3,
              "regression performs a real captured macro edit");
        track_patch_capture(&edited,&s);fm=s.fm[0];end();
        begin();s.graft_amount=64u;graft_apply(&s);end();
        begin();check(s.graft_amount==64u&&!memcmp(&s.graft_base,&edited,sizeof edited)&&
            !memcmp(&s.fm[5],&fm,sizeof fm),"active GRAFT retains its original edited base between passes");
        s.graft_amount=0u;graft_apply(&s);
        check(!memcmp(&s.p,&edited.p,sizeof s.p)&&!memcmp(&s.fm[0],&fm,sizeof fm),
              "GRAFT returning to zero retains the last captured macro edit");
        end();before.track[track]=edited;before.fm[track+1u]=fm;
        check(!memcmp(&s,&before,sizeof s),"GRAFT roundtrip preserves live sound, other tracks and settings");
        if(preset!=2u)continue;
        begin();check(tk_panel_value(&s,&s.fm[0].op[1].release,
            s.fm[0].op[1].release<127u?1:-1,127u)==3,"regression edits a captured FM release field");
        track_patch_capture(&edited,&s);fm=s.fm[0];end();
        begin();s.graft_amount=127u;graft_apply(&s);end();
        begin();check(s.graft_amount==127u&&!memcmp(&s.graft_base,&edited,sizeof edited)&&
            !memcmp(&s.fm[5],&fm,sizeof fm),"full GRAFT endpoint does not overwrite its source base");
        s.graft_amount=0u;graft_apply(&s);
        check(!memcmp(&s.fm[0],&fm,sizeof fm),"GRAFT returning from its endpoint retains captured FM edits");
        end();before.track[track]=edited;before.fm[track+1u]=fm;
        check(!memcmp(&s,&before,sizeof s),"FM roundtrip keeps unrelated live GRAFT and track settings");
    }
}
int main(void)
{
    uint32_t track, i, live_word, other_held = 0u;
    tk_track_patch_t edited;
    tk_fm_patch_t edited_fm;
    int32_t pcm[512];
    for (track = 0; track < TK_TRACKS; track++) {
        reset(); live_word = tk_parameter_word;
        select_track(track);
        check(tk_panel_editor.loaded && s.preset == before.track[track].preset, "view uses the captured preset");
        check(!memcmp(&s.p, &before.track[track].p, sizeof s.p) && s.attack == before.track[track].attack,
              "custom captured values survive entry");
        check(!memcmp(&s.fm[0], &before.fm[track + 1u], sizeof s.fm[0]), "custom captured FM fields survive entry");
        check(!end() && !memcmp(&s, &before, sizeof s), "navigation restores the complete global settings exactly");
        check(tk_parameter_word == live_word, "navigation does not publish a different global live sound");
        publish_params(&s, shade); tk_panel_editor_publish(&s);
        check(tk_source_instrument(0u) == s.track[track].instrument && tk_source_instrument(1u) == PRESETS[s.preset].instrument,
              "panel auditions track while an ordinary MIDI keyboard retains the live sound");
        check(tk_fm_bank(0u) == TRACK_SOURCE[track] - 32u && tk_fm_bank(1u) == 0u,
              "panel and external MIDI have independent FM bank selection");
        begin(); s.attack = 87u; s.release = 83u; s.p.shade = 92u;
        s.fm[0].op[1].release = 45u;
        track_patch_capture(&edited, &s); edited_fm = s.fm[0];
        check(end(), "real patch edit commits");
        check(!memcmp(&s.track[track], &edited, sizeof edited) && !memcmp(&s.fm[track + 1u], &edited_fm, sizeof edited_fm),
              "edit updates the complete selected captured patch");
        before.track[track] = edited; before.fm[track + 1u] = edited_fm;
        check(!memcmp(&s, &before, sizeof s), "edit preserves every unrelated global, pattern and track field");
        for (i = 0; i < 3u; i++) { begin(); check(!end(), "drawing/inspection is not another edit"); }
        begin(); s.graft_amount = 64u; graft_apply(&s); end();
        begin(); check(s.graft_amount == 64u, "track morph retains its base/amount between main passes"); end();
        s.track[track].p.shade = 11u;
        begin(); check(s.p.shade == 11u && !s.graft_amount, "external track edits win and bake the old morph"); end();
        select_track(TK_TRACKS);
        check(!tk_panel_editor.loaded && !tk_panel_patch_bank && !tk_source_patch_timing[0], "HOME-to-live clears panel override");
        check(s.preset == before.preset && s.graft_amount == before.graft_amount && !memcmp(&s.fm[0], &before.fm[0], sizeof s.fm[0]),
              "return to live restores edited global GRAFT and FM, not factory defaults");
    }
    reset(); select_track(1u); end();
    tk_note_on(60u, 100u, 1u); panel_press(7u, 60u); hold = 1u; tk_sustain(0u, 127u);
    memset(pcm, 0, sizeof pcm); tk_render(pcm, 256u, &s.p);
    select_track(2u); end();
    check(hold && panel_refs[60] && panel_down[7] && tk_panel_audition_track == 1u,
          "context navigation preserves a held panel chord and HOLD until a new gesture");
    for (i = 0; i < TK_VOICES; i++)
        if ((tk_voice[i].command & TK_GATE) && ((tk_voice[i].command >> 14) & 63u) == 1u &&
            (tk_voice[i].command & 127u) == 60u) other_held = 1u;
    check(other_held, "context switch preserves another input's same-pitch key");
    panel_release(7u); hold = 0u; tk_sustain(0u, 0u);
    tk_panel_editor_bind(&s, &t, 1u);
    check(tk_panel_audition_track == 2u, "first released/new gesture auditions the newly displayed track");
    tk_memory_init(&tk_history);
    tk_memory_remember(&tk_history, 60u, 1u, 5000, 3000);
    tk_memory_remember(&tk_history, 61u, 0u, 4000, 2000);
    tk_forget_panel_memory(); memset(pcm, 0, sizeof pcm); tk_render(pcm, 256u, &s.p);
    check(tk_history.count == 1u && tk_history.cell[0].source == 1u,
          "changing panel context forgets only panel afterimages");
    tk_banks.importing = 1u; begin();
    check(!tk_panel_editor.loaded, "import never receives a temporary editor workbench");
    tk_banks.importing = 0u;
    baked_base_roundtrip();
    printf("PASS: %u captured-track panel checks (host only)\n", checks);
    return 0;
}
