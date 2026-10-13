/* SPDX-License-Identifier: GPL-3.0-only */
/* Main-owned page knob routing. Host checks cannot qualify hardware timing. */
#define TERKINSTEIN_HOST 1
#define main tk_page_knobs_host_main
#include "../firmware/src/terkinstein.c"
#undef main

static uint32_t page_checks, fixture_hold;
static uint8_t fixture_shade;
static tk_settings_t fixture, before, expected;
static tk_transport_t clock_state, clock_before;
static tk_lfo_t fixture_lfo;
static void page_expect(int condition, const char *message)
{
    page_checks++;
    if (!condition) { fprintf(stderr,"FAIL page knob %u: %s\n",page_checks,message); exit(1); }
}
static void reset_fixture(uint32_t preset)
{
    uint32_t track;
    settings_defaults(&fixture); preset_apply(&fixture,preset);
    memset(&clock_state,0,sizeof clock_state);
    tk_init(); tk_banks_init(&fixture,&clock_state); tk_ready=1; fm1_ms=1050;
    tk_lfo_init(&fixture_lfo,1000); fixture_hold=0; fixture_shade=fixture.p.shade;
    midi_focus=midi_learning=midi_latch=0;
    fixture.p.shade=fixture.p.fracture=fixture.p.drift=fixture.p.bloom=64;
    fixture.attack=fixture.release=fixture.memory=fixture.lfo_rate=fixture.lfo_depth=64;
    fixture.lfo_shape=0; fixture.lfo_on=fixture.fx=0;
    fixture.graft_amount=57;
    for(track=0;track<TK_TRACKS;track++) {
        tk_pattern_clear(&fixture.pattern[track]);
        tk_pattern_record(&fixture.pattern[track],63,48+track,90+track);
        fixture.pattern[track].step[63].note[0].duration_q4=(uint8_t)(251+track);
    }
}
static int turn(uint32_t page,uint32_t knob,int32_t delta)
{
    return tk_page_knob(&fixture,&clock_state,page,knob,delta,
        &fixture_hold,&fixture_shade,&fixture_lfo);
}
static void expect_bake(void)
{
    expected=before; expected.graft_amount=0;
    track_patch_capture(&expected.graft_base,&before); expected.fm[5]=before.fm[0];
}
enum { FIELD_MEMORY,FIELD_SHADE,FIELD_FRACTURE,FIELD_BLOOM,FIELD_ATTACK,
       FIELD_RELEASE,FIELD_DRIFT,FIELD_RATE,FIELD_DEPTH,FIELD_SHAPE };
static uint8_t *field(tk_settings_t *s,uint32_t which)
{
    switch(which) {
    case FIELD_MEMORY:return &s->memory; case FIELD_SHADE:return &s->p.shade;
    case FIELD_FRACTURE:return &s->p.fracture; case FIELD_BLOOM:return &s->p.bloom;
    case FIELD_ATTACK:return &s->attack; case FIELD_RELEASE:return &s->release;
    case FIELD_DRIFT:return &s->p.drift; case FIELD_RATE:return &s->lfo_rate;
    case FIELD_DEPTH:return &s->lfo_depth; default:return &s->lfo_shape;
    }
}
static void sound_field(uint32_t preset,uint32_t page,uint32_t knob,uint32_t which)
{
    static const int32_t detent[4]={7,-7,32767,-32767};
    static const uint8_t value[4]={71,57,127,0};
    uint32_t pass;
    for(pass=0;pass<4;pass++) {
        reset_fixture(preset); *field(&fixture,which)=64; before=fixture; expect_bake();
        *field(&expected,which)=value[pass];
        if((preset!=30 && which==FIELD_FRACTURE) || (preset==30 && which==FIELD_BLOOM)) expected.fx=1;
        if(page==TK_PAGE_LFO && which==FIELD_DEPTH && value[pass]) expected.lfo_on=1;
        page_expect(turn(page,knob,detent[pass])==7,"actual sound edit reports consumed, dirty and visual bits");
        page_expect(!memcmp(&fixture,&expected,sizeof fixture),"sound knob changes only its named field and effective GRAFT base");
        page_expect(settings_valid(&fixture) && fixture.schema==TK_SCHEMA,
            "page knob preserves valid current settings without a schema rewrite");
    }
    for(pass=0;pass<3;pass++) {
        reset_fixture(preset); *field(&fixture,which)=pass?127:0; before=fixture;
        page_expect(turn(page,knob,pass==0?-32767:pass==1?32767:0)==1,
            "zero and outward saturated sound edits are consumed no-ops");
        page_expect(!memcmp(&fixture,&before,sizeof fixture),"saturated no-op cannot bake GRAFT or alter unrelated settings");
    }
}
static void page_sound_fields(void)
{
    static const uint8_t memory[4]={FIELD_MEMORY,FIELD_SHADE,FIELD_FRACTURE,FIELD_BLOOM};
    static const uint8_t envelope[3][4]={
        {FIELD_ATTACK,FIELD_RELEASE,FIELD_BLOOM,0},
        {FIELD_ATTACK,FIELD_BLOOM,FIELD_DRIFT,0},
        {FIELD_DRIFT,FIELD_SHADE,FIELD_FRACTURE,FIELD_BLOOM}};
    static const uint8_t presets[4]={0,11,30,32};
    uint32_t i,k;
    for(k=0;k<4;k++) sound_field(0,TK_PAGE_MEMORY,k,memory[k]);
    for(i=0;i<4;i++) for(k=0;k<(i==2?4u:3u);k++)
        sound_field(presets[i],TK_PAGE_ENV,k,envelope[i==3?0:i][k]);
    for(i=0;i<4;i++) if(i!=2) {
        sound_field(presets[i],TK_PAGE_LFO,0,FIELD_RATE);
        sound_field(presets[i],TK_PAGE_LFO,1,FIELD_DEPTH);
        sound_field(presets[i],TK_PAGE_LFO,3,FIELD_SHADE);
    }
    for(i=0;i<4;i++) for(k=0;k<4;k++) {
        reset_fixture(presets[i]); before=fixture; clock_before=clock_state;
        page_expect(turn(TK_PAGE_GLO,k,32767)==1,"diagnostics knobs are explicitly read-only");
        page_expect(!memcmp(&fixture,&before,sizeof fixture) && !memcmp(&clock_state,&clock_before,sizeof clock_state),
            "diagnostics cannot silently edit live macros or transport");
        if(i) {
            page_expect(turn(TK_PAGE_MEMORY,k,7)==1,"non-PAD afterimage controls are explicitly unavailable");
            page_expect(!memcmp(&fixture,&before,sizeof fixture),"inactive MEMORY page cannot change unrelated live sound");
        }
        if(i==2) {
            page_expect(turn(TK_PAGE_LFO,k,7)==1,"KIT filter-motion controls are explicitly unavailable");
            page_expect(!memcmp(&fixture,&before,sizeof fixture),"inactive KIT LFO cannot alter hidden live macros");
        }
    }
    for(i=0;i<3;i++) for(k=0;k<7;k++) {
        static const int32_t detent[7]={-32767,-4,-1,0,1,4,32767};
        uint8_t next=(uint8_t)(((int32_t)i+detent[k]%3+3)%3);
        reset_fixture(0); fixture.lfo_shape=(uint8_t)i; before=fixture;
        if(next==i) expected=before; else { expect_bake(); expected.lfo_shape=next; }
        page_expect(turn(TK_PAGE_LFO,2,detent[k])==(next==i?1:7),"LFO shape wraps both ways and only actual changes dirty settings");
        page_expect(!memcmp(&fixture,&expected,sizeof fixture),"LFO shape leaves enable, captured patches and unrelated controls intact");
    }
    reset_fixture(0); fixture.lfo_on=1; fixture.lfo_depth=2;
    page_expect(turn(TK_PAGE_LFO,1,-32767)==7 && fixture.lfo_on && !fixture.lfo_depth,
        "zero depth preserves the existing motion enable flag");
}
static void hold_ownership(void)
{
    uint32_t source;
    reset_fixture(0); before=fixture;
    tk_sustain(1,127); tk_sustain(TRACK_SOURCE[TK_TRACK_MELODY],127);
    page_expect(turn(TK_PAGE_ENV,3,1)==5 && fixture_hold==1,"HOLD is a visual/performance edit without dirtying patch settings");
    page_expect(tk_source_control[0]&TK_SUSTAIN,"HOLD controls the local panel source");
    for(source=1;source<TK_SOURCES;source++)
        page_expect(!!(tk_source_control[source]&TK_SUSTAIN)==(source==1 || source==TRACK_SOURCE[TK_TRACK_MELODY]),
            "panel HOLD leaves external channels and captured sources unchanged");
    page_expect(turn(TK_PAGE_ENV,3,32767)==1 && fixture_hold==1,"HOLD cannot resend sustain at its upper bound");
    page_expect(turn(TK_PAGE_ENV,3,-1)==5 && !fixture_hold && !(tk_source_control[0]&TK_SUSTAIN),
        "HOLD release clears only the panel pedal");
    page_expect(turn(TK_PAGE_ENV,3,-32767)==1,"HOLD cannot resend release at its lower bound");
    page_expect(!memcmp(&fixture,&before,sizeof fixture),"HOLD cannot bake GRAFT or alter persistent settings");
}
static void actual_graft_and_dispatch(void)
{
    static const uint8_t pages[3]={TK_PAGE_MEMORY,TK_PAGE_ENV,TK_PAGE_LFO};
    static const uint8_t fields[3]={FIELD_MEMORY,FIELD_ATTACK,FIELD_RATE};
    uint32_t i;
    for(i=0;i<3;i++) {
        int32_t delta;
        reset_fixture(0); fixture.graft_amount=64; graft_apply(&fixture);
        before=fixture; expect_bake(); delta=*field(&fixture,fields[i])?-1:1;
        *field(&expected,fields[i])=(uint8_t)((int32_t)*field(&before,fields[i])+delta);
        page_expect(turn(pages[i],0,delta)==7 && !memcmp(&fixture,&expected,sizeof fixture),
            "actual intermediate GRAFT bakes its effective sound before a named page edit");
        page_expect(!memcmp(&fixture.fm[5],&before.fm[0],sizeof fixture.fm[0]),
            "page edit retains effective six-operator GRAFT base without endpoint jump");
    }
    reset_fixture(0); before=fixture;
    page_expect(turn(TK_PAGE_PAD,0,7)==0 && turn(TK_PAGE_SOUND_EDIT,0,7)==0 && turn(TK_PAGE_FX,0,7)==0,
        "HOME, SOUND EDIT and FX continue through their own established controls");
    page_expect(!memcmp(&fixture,&before,sizeof fixture),"unowned helper dispatch cannot change sound");
    page_expect(turn(TK_PAGE_ENV,4,7)==1 && !memcmp(&fixture,&before,sizeof fixture),
        "out-of-range knob is consumed harmlessly");
}
static void arp_controls(void)
{
    uint32_t order,division,gate;
    reset_fixture(0); fixture.tempo=120; fixture.arp_mode=arp_pack(0,1,1);
    clock_state.mode=TK_MODE_ARP; clock_state.running=1; clock_state.last_step_ms=1000; clock_state.next_ms=1125;
    before=fixture; expected=before; expected.tempo=180;
    page_expect(turn(TK_PAGE_ARP,0,32767)==7 && !memcmp(&fixture,&expected,sizeof fixture),"ARP Knob1 edits BPM without baking timbre");
    page_expect(clock_state.next_ms==1083 && clock_state.running && clock_state.mode==TK_MODE_ARP,
        "ARP BPM retimes from the prior pulse and keeps playback running");
    page_expect(turn(TK_PAGE_ARP,0,1)==1,"ARP tempo saturation is a no-op");
    for(order=0;order<TK_ARP_MODE_COUNT;order++) for(division=0;division<TK_ARP_DIV_COUNT;division++) for(gate=0;gate<TK_ARP_GATE_COUNT;gate++) {
        reset_fixture(0); fixture.arp_mode=arp_pack((uint8_t)order,(uint8_t)division,(uint8_t)gate); before=fixture;
        expected=before; expected.arp_mode=arp_pack((uint8_t)((order+1)%TK_ARP_MODE_COUNT),(uint8_t)division,(uint8_t)gate);
        page_expect(turn(TK_PAGE_ARP,1,1)==7 && !memcmp(&fixture,&expected,sizeof fixture),"ARP order edits preserve independent rate/gate bits and live macros");
        before=fixture; expected=before; expected.arp_mode=arp_pack(arp_mode_of(before.arp_mode),(uint8_t)((division+1)%TK_ARP_DIV_COUNT),(uint8_t)gate);
        page_expect(turn(TK_PAGE_ARP,2,1)==7 && !memcmp(&fixture,&expected,sizeof fixture),"ARP rate edits preserve order/gate and live macros");
        before=fixture; expected=before; expected.arp_mode=arp_pack(arp_mode_of(before.arp_mode),arp_division_of(before.arp_mode),(uint8_t)((gate+1)%TK_ARP_GATE_COUNT));
        page_expect(turn(TK_PAGE_ARP,3,1)==7 && !memcmp(&fixture,&expected,sizeof fixture),"ARP gate edits preserve order/rate and live macros");
    }
}
static void sequencer_controls(void)
{
    uint32_t knob,track;
    reset_fixture(0); fixture.tempo=120; before=fixture; expected=before; expected.tempo=180;
    clock_state.mode=TK_MODE_SEQ; clock_state.running=1; clock_state.next_ms=1125; clock_state.last_step_ms=1000;
    clock_state.playing[0].active=1; clock_state.playing[0].part=TK_TRACK_ATMOS; clock_state.playing[0].due_ms=1200;
    page_expect(turn(TK_PAGE_SEQ,0,60)==7 && !memcmp(&fixture,&expected,sizeof fixture),"SEQ Knob1 changes BPM without changing live or captured timbres");
    page_expect(clock_state.next_ms==1100 && clock_state.playing[0].due_ms==1150 && clock_state.running,
        "SEQ BPM retains the phase of next pulse and held-note deadline");
    clock_state.editing=1; before=fixture; expected=before; expected.tempo=179;
    page_expect(turn(TK_PAGE_SEQ,0,-1)==7 && !memcmp(&fixture,&expected,sizeof fixture)
        && !clock_state.edit_step,"SEQ Knob1 retains BPM ownership during step editing");
    reset_fixture(0); fixture.tempo=120; fixture.arp_mode=arp_pack(0,1,1);
    clock_state.mode=TK_MODE_ARP; clock_state.running=1;
    clock_state.last_step_ms=1000; clock_state.next_ms=1125;
    page_expect(turn(TK_PAGE_SEQ,0,60)==7 && fixture.tempo==180
        && clock_state.next_ms==1083 && clock_state.mode==TK_MODE_ARP && clock_state.running,
        "SEQ BPM correctly retimes a still-running ARP instead of applying SEQ clock math");
    before=fixture; clock_before=clock_state;
    page_expect(turn(TK_PAGE_SEQ,0,32767)==1 && turn(TK_PAGE_SEQ,0,0)==1
        && !memcmp(&fixture,&before,sizeof fixture) && !memcmp(&clock_state,&clock_before,sizeof clock_state),
        "SEQ tempo saturation and zero turns neither dirty nor retime playback");
    reset_fixture(0); before=fixture;
    for(track=1;track<TK_TRACKS;track++) {
        page_expect(tk_seq_encoder(&fixture,&clock_state,0,1)==5 && clock_state.selected==track,
            "SEQ SELECT navigates each track including BASS");
        page_expect(!memcmp(&fixture,&before,sizeof fixture) && !clock_state.running,"SEQ navigation neither changes live preset nor starts playback");
    }
    page_expect(tk_seq_encoder(&fixture,&clock_state,0,1)==5 && !clock_state.selected,
        "SEQ SELECT wraps from BASS to DRUM");
    page_expect(tk_seq_encoder(&fixture,&clock_state,0,-1)==5 && clock_state.selected==TK_TRACK_BASS,
        "SEQ SELECT wraps backwards from DRUM to BASS");
    clock_before=clock_state;
    page_expect(tk_seq_encoder(&fixture,&clock_state,0,TK_TRACKS)==1
        && !memcmp(&clock_state,&clock_before,sizeof clock_state),"whole track cycles are navigation no-ops");
    page_expect(turn(TK_PAGE_SEQ,2,32767)==1 && !memcmp(&fixture,&before,sizeof fixture)
        && !memcmp(&clock_state,&clock_before,sizeof clock_state),"SEQ Knob3 has no hidden duty outside EDIT");
    clock_state.editing=1; fixture.length[clock_state.selected]=64; clock_state.edit_step=63;
    page_expect(turn(TK_PAGE_SEQ,2,1)==5 && clock_state.edit_step==0,"SEQ edit Knob3 wraps through all 64 positions");
    page_expect(turn(TK_PAGE_SEQ,2,-1)==5 && clock_state.edit_step==63,"SEQ edit Knob3 wraps backwards");
    before=fixture; clock_state.recording=1; clock_before=clock_state;
    page_expect(tk_seq_encoder(&fixture,&clock_state,0,1)==1
        && tk_seq_encoder(&fixture,&clock_state,1,1)==1
        && turn(TK_PAGE_SEQ,1,-1)==1 && turn(TK_PAGE_SEQ,2,-1)==1,
        "recording locks track, bank, track length and edit cursor");
    page_expect(!memcmp(&fixture,&before,sizeof fixture) && !memcmp(&clock_state,&clock_before,sizeof clock_state),
        "REC locks protect captured notes and transport state");
    clock_state.recording=0; fixture.length[clock_state.selected]=64; before=fixture; expected=before; expected.length[clock_state.selected]=16;
    page_expect(turn(TK_PAGE_SEQ,1,-32767)==7 && !memcmp(&fixture,&expected,sizeof fixture),
        "SEQ Knob2 length shrink changes only selected length and preserves hidden step data");
    page_expect(fixture.pattern[clock_state.selected].step[63].count==1 && clock_state.edit_step<16,
        "shortening hides rather than erases later notes and bounds editing cursor");
    page_expect(turn(TK_PAGE_SEQ,1,-1)==1,"length cannot shrink below sixteen steps");
    page_expect(turn(TK_PAGE_SEQ,1,1)==7 && fixture.length[clock_state.selected]==32,
        "SEQ Knob2 grows from sixteen to thirty-two steps");
    page_expect(turn(TK_PAGE_SEQ,1,32767)==7 && fixture.length[clock_state.selected]==64,
        "SEQ Knob2 grows through sixty-four steps");
    before=fixture; clock_before=clock_state;
    page_expect(turn(TK_PAGE_SEQ,1,1)==1 && !memcmp(&fixture,&before,sizeof fixture)
        && !memcmp(&clock_state,&clock_before,sizeof clock_state),"length upper bound is a complete no-op");
    before=fixture; expected=before; expected.ghost_mask=127;
    page_expect(turn(TK_PAGE_SEQ,3,32767)==7 && !memcmp(&fixture,&expected,sizeof fixture),"SEQ MASK edits only its existing shared field");
    tk_banks.importing=1; before=fixture; clock_before=clock_state;
    for(knob=0;knob<4;knob++) page_expect(turn(TK_PAGE_SEQ,knob,7)==1,"staged song import consumes all four panel knobs");
    for(knob=0;knob<2;knob++) page_expect(tk_seq_encoder(&fixture,&clock_state,knob,1)==1,
        "staged song import consumes SELECT and ALGORITHM");
    page_expect(!memcmp(&fixture,&before,sizeof fixture) && !memcmp(&clock_state,&clock_before,sizeof clock_state),
        "import lock cannot change staged sequence or musical globals");
    tk_banks.importing=0;
}
static void sequencer_banks(void)
{
    tk_pattern_t *bank;
    uint16_t revision;
    reset_fixture(0); clock_state.selected=TK_TRACK_ATMOS; clock_state.editing=1; clock_state.edit_step=63;
    bank=tk_bank_pattern(&fixture,TK_TRACK_ATMOS,1);
    tk_pattern_record(bank,2,71,110); tk_banks.length[TK_TRACK_ATMOS][1]=32;
    before=fixture; expected=before; expected.pattern[TK_TRACK_ATMOS]=*bank;
    expected.length[TK_TRACK_ATMOS]=32;
    page_expect(tk_seq_encoder(&fixture,&clock_state,1,1)==7
        && tk_banks.active[TK_TRACK_ATMOS]==1 && !clock_state.running
        && !memcmp(&fixture,&expected,sizeof fixture),
        "stopped SEQ ALGORITHM selects a bank with only its pattern and length changed");
    page_expect(clock_state.edit_step==31 && tk_banks.pending[TK_TRACK_ATMOS]==TK_BANK_NONE,
        "stopped bank change bounds cursor and leaves no queued request");
    page_expect(tk_seq_encoder(&fixture,&clock_state,1,-1)==7
        && tk_banks.active[TK_TRACK_ATMOS]==0 && !memcmp(&fixture,&before,sizeof fixture),
        "returning to bank A restores its full hidden notes and preserves GRAFT/live/captured sounds");
    before=fixture; clock_before=clock_state; revision=tk_banks.revision;
    page_expect(tk_seq_encoder(&fixture,&clock_state,1,TK_BANK_COUNT)==1
        && tk_seq_encoder(&fixture,&clock_state,1,0)==1
        && tk_banks.revision==revision && !memcmp(&fixture,&before,sizeof fixture)
        && !memcmp(&clock_state,&clock_before,sizeof clock_state),"whole bank cycles and zero turns are complete no-ops");
    clock_state.mode=TK_MODE_SEQ; clock_state.running=1;
    clock_state.next_step=7; clock_state.shown_step=6;
    before=fixture; clock_before=clock_state;
    page_expect(tk_seq_encoder(&fixture,&clock_state,1,1)==5
        && tk_banks.pending[TK_TRACK_ATMOS]==1 && !tk_banks.active[TK_TRACK_ATMOS]
        && !memcmp(&fixture,&before,sizeof fixture) && !memcmp(&clock_state,&clock_before,sizeof clock_state),
        "running SEQ ALGORITHM queues at the selected track boundary without dirtying patch or retiming transport");
    page_expect(!tk_banks_boundary(&fixture,&clock_state,7) && !tk_banks.active[TK_TRACK_ATMOS],
        "queued bank cannot activate before that track's boundary");
    page_expect(tk_seq_encoder(&fixture,&clock_state,1,-1)==5
        && tk_banks.pending[TK_TRACK_ATMOS]==TK_BANK_NONE
        && !memcmp(&fixture,&before,sizeof fixture) && !memcmp(&clock_state,&clock_before,sizeof clock_state),
        "returning ALGORITHM to the active bank cancels a cue without changing the sounding pattern");
    page_expect(tk_seq_encoder(&fixture,&clock_state,1,1)==5
        && tk_seq_encoder(&fixture,&clock_state,1,1)==5 && tk_banks.pending[TK_TRACK_ATMOS]==2,
        "ALGORITHM advances relative to the pending bank rather than repeatedly requesting B");
    page_expect(tk_banks_boundary(&fixture,&clock_state,16)
        && tk_banks.active[TK_TRACK_ATMOS]==2 && clock_state.running
        && clock_state.mode==TK_MODE_SEQ && tk_banks.pending[TK_TRACK_ATMOS]==TK_BANK_NONE,
        "queued bank activates at its loop boundary while playback stays running");
    page_expect(!memcmp(&fixture.p,&before.p,sizeof fixture.p)
        && !memcmp(&fixture.track,&before.track,sizeof fixture.track)
        && !memcmp(&fixture.fm,&before.fm,sizeof fixture.fm)
        && fixture.graft_amount==before.graft_amount && fixture.graft_target==before.graft_target,
        "bank activation cannot substitute the captured sound for the auditioned patch or disturb GRAFT");
    reset_fixture(0); clock_state.mode=TK_MODE_ARP; clock_state.running=1; clock_state.next_ms=1200;
    page_expect(tk_seq_encoder(&fixture,&clock_state,1,-1)==7 && tk_banks.active[0]==3
        && clock_state.running && clock_state.mode==TK_MODE_ARP && clock_state.next_ms==1200,
        "bank browsing under a running ARP applies immediately without stopping or retiming ARP");
}
static void midi_controls(void)
{
    uint32_t knob;
    reset_fixture(0); before=fixture; midi_learning=1;
    page_expect(turn(TK_PAGE_MIDI,0,1)==5 && midi_focus==1 && !midi_learning,
        "MIDI Knob1 changes target, cancels learning and does not dirty settings");
    page_expect(!memcmp(&fixture,&before,sizeof fixture),"MIDI target navigation cannot edit hidden sound macros");
    fixture.midi_bind[1].cc=30; before=fixture; expected=before; expected.midi_bind[1].cc=31;
    page_expect(turn(TK_PAGE_MIDI,1,1)==7 && !memcmp(&fixture,&expected,sizeof fixture),"MIDI Knob2 changes only its selected CC mapping");
    fixture.midi_bind[1].port=0; before=fixture; expected=before; expected.midi_bind[1].port=1;
    page_expect(turn(TK_PAGE_MIDI,2,1)==7 && !memcmp(&fixture,&expected,sizeof fixture),"MIDI Knob3 edits selected USB/TRS/BOTH port");
    fixture.midi_bind[1].channel=0; before=fixture; expected=before; expected.midi_bind[1].channel=1;
    page_expect(turn(TK_PAGE_MIDI,3,1)==7 && !memcmp(&fixture,&expected,sizeof fixture),"MIDI Knob4 edits selected channel/ALL route");
    midi_focus=TK_MIDI_BINDINGS; before=fixture; expected=before; expected.midi_bend_range=24;
    page_expect(turn(TK_PAGE_MIDI,1,32767)==7 && !memcmp(&fixture,&expected,sizeof fixture),"MIDI bend target edits default range without hidden macro changes");
    before=fixture;
    for(knob=2;knob<4;knob++) page_expect(turn(TK_PAGE_MIDI,knob,7)==1,"bend range has no artificial port/channel parameters");
    page_expect(!memcmp(&fixture,&before,sizeof fixture),"inactive MIDI route controls remain harmless");
    page_expect(turn(TK_PAGE_MIDI,0,32767)==1 && midi_focus==TK_MIDI_BINDINGS,"target upper saturation is a navigation no-op");
    reset_fixture(0); midi_focus=1; fixture.midi_bind[1].cc=30; fixture.midi_bind[2].cc=31;
    before=fixture; expected=before; expected.midi_bind[1].cc=31; expected.midi_bind[2].cc=255;
    page_expect(turn(TK_PAGE_MIDI,1,1)==7 && !memcmp(&fixture,&expected,sizeof fixture),
        "CC editing clears only an exact conflicting route rather than assigning duplicate bindings");
    fixture.midi_bind[1].cc=0;
    page_expect(turn(TK_PAGE_MIDI,1,1)==7 && fixture.midi_bind[1].cc==2,
        "MIDI Knob2 skips reserved performance CC1");
    before=fixture;
    page_expect(turn(TK_PAGE_MIDI,2,32767)==1 && turn(TK_PAGE_MIDI,3,32767)==1,
        "MIDI port/channel saturated turns do not reapply or dirty mappings");
    page_expect(!memcmp(&fixture,&before,sizeof fixture),"MIDI route no-op retains GRAFT and captured settings");
}
int main(void)
{
    page_sound_fields(); hold_ownership(); actual_graft_and_dispatch(); arp_controls(); sequencer_controls(); sequencer_banks(); midi_controls();
    printf("PASS page knobs: %u actual-app routing, ownership, GRAFT, bounds and transport checks; host only\n",page_checks);
    return 0;
}
