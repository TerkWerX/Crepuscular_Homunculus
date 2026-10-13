/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual-app labeled-page knob ownership, boundaries and LCD previews.
 * Host checks do not qualify hardware gestures, SPI time or audio deadlines. */
#define TERKINSTEIN_HOST 1
#define main tk_panel_host_main
#include "../firmware/src/terkinstein.c"
#undef main

static uint32_t panel_checks;
static tk_settings_t fixture, before, expected;
static void panel_expect(int good,const char *why)
{
    panel_checks++;
    if (!good) { fprintf(stderr,"FAIL panel %u: %s\n",panel_checks,why); exit(1); }
}
enum { F_RATIO,F_LEVEL,F_ATTACK,F_DECAY,F_SUSTAIN,F_OP_RELEASE,
       F_DEPTH,F_TIME,F_FEEDBACK,F_ROUTING,F_BEND,F_OCTAVE,F_MONO,
       F_RELEASE,F_BLOOM,F_DRIFT };
static uint8_t *panel_field(tk_settings_t *s,uint32_t field)
{
    switch (field) {
    case F_RATIO: return &s->fm[0].op[tk_operator_focus].ratio;
    case F_LEVEL: return &s->fm[0].op[tk_operator_focus].level;
    case F_ATTACK: return &s->fm[0].op[tk_operator_focus].attack;
    case F_DECAY: return &s->fm[0].op[tk_operator_focus].decay;
    case F_SUSTAIN: return &s->fm[0].op[tk_operator_focus].sustain;
    case F_OP_RELEASE: return &s->fm[0].op[tk_operator_focus].release;
    case F_DEPTH: return &s->fm[0].pitch_depth;
    case F_TIME: return &s->fm[0].pitch_time;
    case F_FEEDBACK: return &s->fm[0].feedback;
    case F_ROUTING: return &s->fm[0].algorithm;
    case F_BEND: return &s->midi_bend_range;
    case F_OCTAVE: return (uint8_t *)&s->octave;
    case F_MONO: return &s->mono;
    case F_RELEASE: return &s->release;
    case F_BLOOM: return &s->p.bloom;
    default: return &s->p.drift;
    }
}
static void panel_fixture(uint32_t preset)
{
    uint32_t track,source;
    tk_init(); settings_defaults(&fixture); preset_apply(&fixture,preset);
    for (track=0; track<TK_TRACKS; track++) {
        tk_pattern_clear(&fixture.pattern[track]);
        tk_pattern_record(&fixture.pattern[track],63u,50u+track,83u+track);
        fixture.pattern[track].step[63].note[0].duration_q4=(uint8_t)(251u+track);
        fm_for_preset(&fixture.fm[track+1u],32u+track);
    }
    fixture.graft_amount=preset==TK_KIT_PRESET_INDEX ? 0u : 57u; graft_apply(&fixture);
    for (source=0; source<TK_SOURCES; source++) tk_set_bend_range(source,11u);
}
static void panel_expected_bake(void)
{
    expected=before; expected.graft_amount=0;
    track_patch_capture(&expected.graft_base,&before);
    expected.fm[5]=before.fm[0];
}
static uint32_t panel_max(uint32_t field)
{
    return field==F_RATIO ? 31u : field==F_DEPTH ? 48u :
        field==F_ROUTING ? 7u : field==F_BEND ? 24u : field==F_MONO ? 1u : 127u;
}
static int panel_timbre(uint32_t field)
{
    return field!=F_BEND && field!=F_OCTAVE && field!=F_MONO;
}
static int panel_invoke(uint32_t page,uint32_t knob,int32_t delta,int encoder)
{
    return encoder ? tk_panel_encoder(&fixture,page,knob,delta)
        : tk_panel_knob(&fixture,page,knob,delta);
}
static void panel_control(uint32_t preset,uint32_t page,uint32_t knob,uint32_t field,int encoder)
{
    uint32_t pass,source,max=panel_max(field);
    for (pass=0; pass<4u; pass++) {
        int32_t delta=pass==0u ? 1 : pass==1u ? -1 : pass==2u ? 32767 : -32767;
        uint8_t current=(uint8_t)(max/2u),next;
        panel_fixture(preset);
        if (field==F_OCTAVE) { fixture.octave=0; next=(uint8_t)(int8_t)(pass==0u ? 1 : pass==1u ? -1 : pass==2u ? 2 : -2); }
        else {
            if (field==F_MONO) current=(uint8_t)(pass==1u || pass==3u);
            *panel_field(&fixture,field)=current;
            if (field==F_MONO) tk_set_mono(current);
            next=pass==0u ? (uint8_t)(current+1u) : pass==1u ? (uint8_t)(current-1u)
                : pass==2u ? (uint8_t)max : 0u;
        }
        before=fixture;
        if (panel_timbre(field)) panel_expected_bake(); else expected=before;
        *panel_field(&expected,field)=next;
        panel_expect(panel_invoke(page,knob,delta,encoder)==3,"an actual knob/alias edit is consumed and reports dirty");
        panel_expect(!memcmp(&fixture,&expected,sizeof fixture),"only the named live field and required GRAFT bake metadata change");
        if (!settings_valid(&fixture)) fprintf(stderr,"Invalid fixture: preset=%u page=%u knob=%u field=%u pass=%u\n",
            preset,page,knob,field,pass);
        panel_expect(settings_valid(&fixture) && fixture.schema==TK_SCHEMA,
            "edited settings retain the valid current layout and schema");
        if (field==F_BEND) for (source=0; source<TK_SOURCES; source++)
            panel_expect(tk_source_bend_range[source]==(source<TK_SOURCE_SEQUENCE ? next : 11u),
                "default bend publishes to every live owner without rewriting generated tracks");
        if (field==F_MONO) panel_expect(tk_mono==next,"mono knob publishes actual main-owned allocator mode");
        if (field==F_RELEASE || field==F_BLOOM || field==F_DRIFT) {
            publish_params(&fixture,fixture.p.shade);
            panel_expect(tk_envelope_control==(fixture.attack | ((uint32_t)fixture.release<<7)),
                "the normal main publication retains actual attack/release controls");
            panel_expect(tk_parameter_word==((uint32_t)fixture.p.shade |
                ((uint32_t)fixture.p.fracture<<8) | ((uint32_t)fixture.p.drift<<16) |
                ((uint32_t)fixture.p.bloom<<24)),"contextual tail edit reaches the existing macro publication");
        }
    }
    for (pass=0; pass<3u; pass++) {
        int32_t delta=pass==0u ? -32767 : pass==1u ? 32767 : 0;
        panel_fixture(preset);
        if (field==F_OCTAVE) fixture.octave=pass ? 2 : -2;
        else *panel_field(&fixture,field)=pass ? (uint8_t)max : 0u;
        before=fixture;
        panel_expect(panel_invoke(page,knob,delta,encoder)==1,"outward bounds and zero turns are consumed without reporting dirty");
        panel_expect(!memcmp(&fixture,&before,sizeof fixture),"no-op turns preserve effective GRAFT, captured sounds and all settings");
    }
}
static void panel_mappings(void)
{
    static const uint8_t presets[4]={0u,11u,32u,30u};
    static const uint8_t op_fields[6]={F_RATIO,F_LEVEL,F_ATTACK,F_DECAY,F_SUSTAIN,F_OP_RELEASE};
    uint32_t mode,operator,knob,pass;
    for (operator=0; operator<6u; operator++) {
        tk_operator_focus=(uint8_t)operator;
        for (knob=0; knob<6u; knob++) panel_control(32u,TK_PAGE_OPERATOR,knob<4u ? knob : knob-4u,
            op_fields[knob],knob>=4u);
    }
    for (mode=0; mode<4u; mode++) {
        if (mode==2u) {
            panel_control(presets[mode],TK_PAGE_PITCH,0,F_DEPTH,0);
            panel_control(presets[mode],TK_PAGE_PITCH,1,F_TIME,0);
            panel_control(presets[mode],TK_PAGE_GLOBAL,0,F_FEEDBACK,0);
            panel_control(presets[mode],TK_PAGE_GLOBAL,2,F_ROUTING,0);
        } else {
            panel_fixture(presets[mode]); before=fixture;
            for (pass=0; pass<2u; pass++) {
                int32_t delta=pass ? -32767 : 32767;
                for (knob=0; knob<4u; knob++) panel_expect(
                    tk_panel_knob(&fixture,TK_PAGE_OPERATOR,knob,delta)==1,
                    "non-FM operator knobs explicitly consume inactive edits");
                panel_expect(tk_panel_knob(&fixture,TK_PAGE_PITCH,0,delta)==1
                    && tk_panel_knob(&fixture,TK_PAGE_PITCH,1,delta)==1
                    && tk_panel_knob(&fixture,TK_PAGE_GLOBAL,0,delta)==1
                    && tk_panel_knob(&fixture,TK_PAGE_GLOBAL,2,delta)==1,
                    "non-FM PIT/GLOBAL fields cannot fall through to hidden sound macros");
                panel_expect(tk_panel_encoder(&fixture,TK_PAGE_OPERATOR,0,delta)==1
                    && tk_panel_encoder(&fixture,TK_PAGE_OPERATOR,1,delta)==1
                    && tk_panel_encoder(&fixture,TK_PAGE_PITCH,0,delta)==1
                    && tk_panel_encoder(&fixture,TK_PAGE_PITCH,1,delta)==1
                    && tk_panel_encoder(&fixture,TK_PAGE_GLOBAL,1,delta)==1,
                    "non-FM legacy FM aliases remain inactive");
            }
            panel_expect(!memcmp(&fixture,&before,sizeof fixture),"inactive FM fields preserve every setting and GRAFT");
        }
        panel_control(presets[mode],TK_PAGE_PITCH,2,F_BEND,0);
        panel_control(presets[mode],TK_PAGE_PITCH,3,F_OCTAVE,0);
        panel_control(presets[mode],TK_PAGE_GLOBAL,1,F_BEND,0);
        panel_control(presets[mode],TK_PAGE_GLOBAL,3,F_MONO,0);
        panel_control(presets[mode],TK_PAGE_VOICE,0,F_MONO,0);
        panel_control(presets[mode],TK_PAGE_VOICE,1,F_BEND,0);
        panel_control(presets[mode],TK_PAGE_VOICE,2,F_OCTAVE,0);
        panel_control(presets[mode],TK_PAGE_VOICE,3,mode==1u ? F_BLOOM : mode==3u ? F_DRIFT : F_RELEASE,0);
        panel_fixture(presets[mode]); before=fixture;
        for(pass=TK_PAGE_PITCH;pass<=TK_PAGE_VOICE;pass++) {
            uint32_t encoder;
            for(encoder=0;encoder<2u;encoder++) {
                panel_expect(tk_panel_encoder(&fixture,pass,encoder,32767)==1
                    && tk_panel_encoder(&fixture,pass,encoder,-32767)==1,
                    "PIT, GLOBAL and VOICE consume SELECT/ALGORITHM without duplicate knob duties");
                panel_expect(!memcmp(&fixture,&before,sizeof fixture),
                    "disabled encoder aliases cannot change sound, GRAFT, captured patches or performance settings");
            }
        }
    }
    panel_fixture(32u); before=fixture;
    for (pass=TK_PAGE_OPERATOR; pass<=TK_PAGE_VOICE; pass++)
        panel_expect(tk_panel_knob(&fixture,pass,4u,32767)==1
            && tk_panel_encoder(&fixture,pass,2u,32767)==1,"invalid labeled-page control indices cannot change settings");
    panel_expect(!memcmp(&fixture,&before,sizeof fixture),"invalid control indices preserve GRAFT and all owners");
    panel_expect(!tk_panel_knob(&fixture,TK_PAGE_PAD,0u,1)
        && !tk_panel_encoder(&fixture,TK_PAGE_PAD,0u,1),"unrelated pages retain their own routing");
}
static uint32_t panel_gates(uint32_t source)
{
    uint32_t i,count=0;
    for (i=0; i<TK_VOICES; i++) if ((tk_voice[i].command&TK_GATE)
        && ((tk_voice[i].command>>14)&63u)==source) count++;
    return count;
}
static int panel_has_gate(uint32_t source,uint32_t note)
{
    uint32_t i;
    for (i=0; i<TK_VOICES; i++) if ((tk_voice[i].command&TK_GATE)
        && ((tk_voice[i].command>>14)&63u)==source && (tk_voice[i].command&127u)==note) return 1;
    return 0;
}
static void panel_performance_ownership(void)
{
    uint32_t old_note,new_note;
    panel_fixture(0u);
    panel_expect(tk_note_on(60u,100u,1u) && tk_note_on(64u,100u,1u)
        && tk_note_on(65u,100u,2u) && tk_note_on(67u,100u,TK_SOURCE_SEQUENCE)
        && tk_note_on(71u,100u,TK_SOURCE_SEQUENCE),"mixed live owners and recorded chord fit the existing shared slots");
    panel_expect(tk_panel_knob(&fixture,TK_PAGE_GLOBAL,3u,1)==3
        && panel_gates(1u)==1u && panel_has_gate(1u,64u) && panel_gates(2u)==1u
        && panel_gates(TK_SOURCE_SEQUENCE)==2u,"mode knob applies last-note priority independently and leaves recorded chord polyphonic");
    tk_note_off(64u,1u);
    panel_expect(panel_has_gate(1u,60u) && !panel_has_gate(1u,64u),"releasing mono winner restores earlier held live note");
    tk_note_on(64u,100u,1u);
    panel_expect(tk_panel_knob(&fixture,TK_PAGE_VOICE,0u,-1)==3
        && panel_gates(1u)==2u && panel_gates(TK_SOURCE_SEQUENCE)==2u,
        "voice-page mode knob restores held live polyphony without clearing captured chord");
    panel_fixture(0u); memset(panel_note,0,sizeof panel_note); memset(panel_down,0,sizeof panel_down);
    memset(panel_refs,0,sizeof panel_refs); panel_arpeggiated=0;
    old_note=(uint32_t)(KEY0+12*fixture.octave); panel_press(0u,old_note);
    panel_expect(tk_panel_knob(&fixture,TK_PAGE_PITCH,3u,2)==3
        && panel_note[0]==old_note && panel_refs[old_note]==1u,
        "panel octave knob retains original held-key pitch and release ownership");
    new_note=(uint32_t)(KEY0+12*fixture.octave)+1u; panel_press(1u,new_note);
    panel_expect(new_note==old_note+25u && panel_note[1]==new_note,
        "new panel key uses the updated octave while original key remains held");
    panel_release(0u); panel_release(1u);
    panel_expect(!panel_refs[old_note] && !panel_refs[new_note],"both transposition-era keys release their original references cleanly");
}
static void panel_previews(const char *directory)
{
    static const uint8_t presets[4]={0u,11u,32u,30u};
    static const char *const modes[4]={"pad","metal","fm","kit"};
    static const uint8_t pages[3]={TK_PAGE_PITCH,TK_PAGE_GLOBAL,TK_PAGE_VOICE};
    static const char *const names[3]={"pitch","global","voice"};
    uint32_t mode,page,i,green,purple;
    for (mode=0; mode<4u; mode++) for (page=0; page<3u; page++) {
        char path[512]; panel_fixture(presets[mode]);
        lcd_fill(0,0,240,240,C_BLACK); draw_header(pages[page]);
        if (!page) tk_draw_pitch(&fixture);
        else if (page==1u) tk_draw_global(&fixture); else tk_draw_voice(&fixture);
        for (i=green=purple=0; i<240u*240u; i++) {
            green+=fb[i]==TK_GREEN; purple+=fb[i]==C_ACC;
        }
        panel_expect(green>100u && purple>100u,"labeled pages retain readable purple labels and green current values");
        if (directory) {
            snprintf(path,sizeof path,"%s/panel-%s-%s.ppm",directory,names[page],modes[mode]);
            write_ppm_as(path);
        }
    }
}
int main(int argc,char **argv)
{
    panel_mappings(); panel_performance_ownership(); panel_previews(argc>1 ? argv[1] : 0);
    printf("PASS panel controls: %u actual-app ownership, bounds, GRAFT and LCD checks; host only\n",panel_checks);
    return 0;
}
