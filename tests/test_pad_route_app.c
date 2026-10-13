/* SPDX-License-Identifier: GPL-3.0-only */
/* Opt-in external pads share captured DRUM controls, never its note owner.
 * Host checks exercise real app commands/mailboxes/allocator. Device deadlines
 * and physical controller velocity still need an attended hardware check. */
#define TERKINSTEIN_HOST 1
#define main tk_pad_route_host_main
#include "../firmware/src/terkinstein.c"
#undef main
#include <stddef.h>

static uint32_t checks, reply_length;
static uint8_t reply[TK_REMOTE_PAYLOAD_MAX], live_shade, track_shade[TK_TRACKS];
static tk_settings_t fixture, before;
static tk_transport_t transport;
static tk_lfo_t live_lfo, track_lfo[TK_TRACKS];
static void check(int condition, const char *why)
{
    ++checks;
    if (!condition) { fprintf(stderr,"FAIL pad route %u: %s\n",checks,why); exit(1); }
}
static void reset_fixture(void)
{
    uint32_t track;
    tk_init(); tk_metronome_init(); tk_visual_init(); tk_ready=1u;
    settings_defaults(&fixture); memset(&transport,0,sizeof transport);
    tk_banks_init(&fixture,&transport); tk_lfo_init(&live_lfo,0u);
    fm1_ms=0u; midi_performance_init(&fixture);
    remote_observed_valid=remote_last_valid=remote_reply_pending=0u;
    remote_generation=usb_remote_generation;
    for(track=0;track<TK_TRACKS;++track) {
        tk_lfo_init(&track_lfo[track],0u); track_shade[track]=fixture.track[track].p.shade;
        publish_track(&fixture,track);
    }
    live_shade=fixture.p.shade; publish_params(&fixture,live_shade);
}
static uint32_t command(uint32_t which,const uint8_t *payload,uint32_t count)
{
    tk_remote_message_t request;
    request.direction=TK_REMOTE_REQUEST; request.command=(uint8_t)which;
    request.id=1u; request.length=(uint8_t)count; request.payload=payload;
    return tk_remote_command(&request,&fixture,&transport,&live_shade,
        &live_lfo,track_lfo,track_shade,TK_PAGE_PAD,0u,reply,&reply_length);
}
static uint32_t held(uint32_t note,uint32_t source)
{
    uint32_t v;
    for(v=0;v<TK_VOICES;++v) {
        uint32_t c=tk_voice[v].command;
        if((c&TK_GATE) && (c&127u)==note && ((c>>14)&63u)==source) return 1u;
    }
    return 0u;
}
static tk_voice_t *voice(uint32_t note,uint32_t source)
{
    uint32_t v;
    for(v=0;v<TK_VOICES;++v) if(tk_voice[v].audio_note==note && tk_voice[v].source==source && tk_voice[v].env)
        return &tk_voice[v];
    return 0;
}
static void render(uint32_t count)
{
    int32_t pcm[512]; uint32_t block;
    for(block=0;block<count;++block) { memset(pcm,0,sizeof pcm); app_audio(pcm,256u); }
}
static void protocol_and_lease(void)
{
    static const uint8_t ON[]={1u,1u}, OFF[]={1u,0u};
    static const uint8_t BAD_VERSION[]={2u,1u}, BAD_ENABLED[]={1u,2u};
    uint32_t flags,now; uint8_t hello[TK_REMOTE_PAYLOAD_MAX]; uint32_t hello_n;
    reset_fixture(); before=fixture;
    check(!midi_live_drum_enabled,"route defaults OFF without negotiation");
    command(TK_REMOTE_HELLO,0,0u); hello_n=reply_length; memcpy(hello,reply,hello_n);
    check(hello_n==11u+reply[10] && reply[2]==127u && reply[3]==127u,"original HELLO layout/capabilities unchanged");
    flags=command(TK_REMOTE_PAD_ROUTE,0,0u);
    check(!flags && reply_length==5u && !memcmp(reply,"\0\1\0\20\0",5u),"read probe reports version1,OFF,channel16,DRUM");
    check(!memcmp(&fixture,&before,sizeof fixture),"route query does not mutate saved settings");
    command(TK_REMOTE_SET_PAD_ROUTE,BAD_VERSION,2u);
    check(reply[0]==TK_REMOTE_BAD_ARGUMENT && !midi_live_drum_enabled,"unsupported route version rejects atomically");
    command(TK_REMOTE_SET_PAD_ROUTE,BAD_ENABLED,2u);
    check(reply[0]==TK_REMOTE_BAD_ARGUMENT && !midi_live_drum_enabled,"invalid enabled field rejects atomically");
    command(TK_REMOTE_SET_PAD_ROUTE,ON,1u);
    check(reply[0]==TK_REMOTE_BAD_ARGUMENT && !midi_live_drum_enabled,"legacy-shaped one-byte SET cannot enable route");
    transport.recording=1u; command(TK_REMOTE_SET_PAD_ROUTE,ON,2u);
    check(reply[0]==TK_REMOTE_BUSY && !midi_live_drum_enabled,"new enabling is locked during capture");
    transport.recording=0u; tk_banks.importing=1u; command(TK_REMOTE_SET_PAD_ROUTE,ON,2u);
    check(reply[0]==TK_REMOTE_BUSY && !midi_live_drum_enabled,"new enabling is locked during staged import");
    tk_banks.importing=0u; flags=command(TK_REMOTE_SET_PAD_ROUTE,ON,2u);
    check(!flags && reply_length==5u && !memcmp(reply,"\0\1\1\20\0",5u),"enable acknowledges exact volatile route without dirty flags");
    check(!memcmp(&fixture,&before,sizeof fixture),"enable never adds save fields or modifies current sound/song");
    command(TK_REMOTE_HELLO,0,0u);
    check(reply_length==hello_n && !memcmp(reply,hello,hello_n),"enabled routing retains exact HELLO prefix and version");
    transport.recording=1u; tk_banks.importing=1u; fm1_ms=2999u;
    command(TK_REMOTE_SET_PAD_ROUTE,ON,2u);
    check(reply[0]==TK_REMOTE_OK && midi_live_drum_enabled,"same-route heartbeat allowed during REC/import");
    midi_live_drum_expire(&transport,&fixture,5998u); check(midi_live_drum_enabled,"lease survives through2999ms");
    midi_live_drum_expire(&transport,&fixture,5999u); check(!midi_live_drum_enabled,"3000ms lease expires even during REC/import");
    transport.recording=0u; tk_banks.importing=0u;
    fm1_ms=0xFFFFFF00u; command(TK_REMOTE_SET_PAD_ROUTE,ON,2u); now=fm1_ms;
    midi_live_drum_expire(&transport,&fixture,now+2999u); check(midi_live_drum_enabled,"lease arithmetic handles clock wrap");
    midi_live_drum_expire(&transport,&fixture,now+3000u); check(!midi_live_drum_enabled,"wrapped lease expires exactly at threshold");
    command(TK_REMOTE_SET_PAD_ROUTE,OFF,2u); check(reply[0]==TK_REMOTE_OK,"OFF cleanup is idempotent");
    check(sizeof fixture==3700u && offsetof(tk_settings_t,fm)==3452u && TK_SOURCES==40u,
        "unchanged flash record,FM offsets and per-source array count");
}
static void ownership_and_captured_sound(void)
{
    uint32_t i; tk_voice_t *pad,*key; tk_fm_patch_t expected;
    reset_fixture();
    check(midi_source(0u,15u)==16u && midi_source(1u,15u)==32u,"OFF preserves USB/rear channel16 owners");
    track_factory_apply(&fixture,TK_TRACK_DRUM,54u); publish_track(&fixture,TK_TRACK_DRUM);
    midi_live_drum_set(&transport,&fixture,1u);
    check(midi_source(0u,15u)==TK_SOURCE_LIVE_DRUM && midi_source(1u,15u)==32u,"ON reserves USB16 only");
    for(i=0;i<15u;++i) check(midi_source(0u,i)==i+1u,"all other USB channels remain independent");
    check(tk_note_on(36u,77u,TK_SOURCE_LIVE_DRUM) && tk_note_on(60u,101u,1u),"pads and live melody allocate simultaneously");
    render(4u); pad=voice(36u,TK_SOURCE_LIVE_DRUM); key=voice(60u,1u);
    check(pad && pad->instrument==TK_INSTRUMENT_DRUM_KIT && pad->frozen_patch,"pad uses captured DRUM kit and envelope");
    check(key && key->instrument==PRESETS[fixture.preset].instrument && !key->frozen_patch,"keys retain independently selected live sound");
    check(pad->level==77u*32u,"incoming pad strike velocity reaches existing gain exactly");
    check(tk_note_on(36u,99u,TK_SOURCE_DRUM),"sequencer can own the same drum pitch separately");
    tk_note_off(36u,TK_SOURCE_LIVE_DRUM);
    check(held(36u,TK_SOURCE_DRUM) && held(60u,1u) && !held(36u,TK_SOURCE_LIVE_DRUM),"pad release never releases sequence or keyboard notes");
    track_factory_apply(&fixture,TK_TRACK_DRUM,44u); publish_track(&fixture,TK_TRACK_DRUM);
    tk_source_shade_control[TK_SOURCE_DRUM]=37u; tk_follow_source_macros(TK_SOURCE_DRUM); publish_live_drum();
    check(tk_source_instrument_control[TK_SOURCE_LIVE_DRUM]==TK_INSTRUMENT_PAD &&
        tk_source_shade_control[TK_SOURCE_LIVE_DRUM]==37u &&
        tk_source_macro_control[TK_SOURCE_LIVE_DRUM]==tk_source_macro_control[TK_SOURCE_DRUM],
        "deliberate non-kit DRUM sounds,track LFO and macro following mirror independently");
    reset_fixture(); track_factory_apply(&fixture,TK_TRACK_DRUM,43u); publish_track(&fixture,TK_TRACK_DRUM);
    expected=fixture.fm[1u]; midi_live_drum_set(&transport,&fixture,1u);
    tk_note_on(48u,90u,TK_SOURCE_LIVE_DRUM); render(4u); pad=voice(48u,TK_SOURCE_LIVE_DRUM);
    check(pad && pad->instrument==TK_INSTRUMENT_FM && pad->fm.bank==2u &&
        !memcmp(&pad->fm.patch,&expected,sizeof expected),"live pads use captured DRUM FM bank2,never live bank0");
    reset_fixture(); midi_live_drum_set(&transport,&fixture,1u); tk_set_mono(1u);
    tk_note_on(36u,100u,TK_SOURCE_LIVE_DRUM); tk_note_on(38u,100u,TK_SOURCE_LIVE_DRUM);
    check(held(36u,TK_SOURCE_LIVE_DRUM) && held(38u,TK_SOURCE_LIVE_DRUM),"pad chords remain polyphonic under keyboard MONO");
    reset_fixture(); midi_live_drum_set(&transport,&fixture,1u);
    tk_note_on(36u,100u,TK_SOURCE_LIVE_DRUM);
    for(i=1;i<TK_VOICES;++i) tk_note_on(60u+i,100u,i);
    check(tk_live_voice_count()==TK_VOICES && !tk_note_on(50u,100u,TK_SOURCE_DRUM),"generated notes cannot steal any held pad/key voice");
    check(held(36u,TK_SOURCE_LIVE_DRUM),"failed generated allocation preserves pad gate");
    reset_fixture(); midi_live_drum_set(&transport,&fixture,1u);
    tk_note_on(60u,100u,1u);
    for(i=1;i<TK_VOICES;++i) tk_note_on(40u+i,100u,TK_SOURCE_DRUM);
    check(tk_note_on(36u,100u,TK_SOURCE_LIVE_DRUM) && held(60u,1u),"live pad can take generated capacity without evicting held keys");
    check(TK_VOICES==6u && TK_FM_VOICE_LIMIT==4u,"routing does not increase DSP voices or FM cap");
}
static void controller_and_reset_isolation(void)
{
    static const uint8_t IGNORE[]={1u,6u,20u,64u,66u,67u,71u,74u,100u,101u};
    uint32_t i,buttons=0u,word; uint8_t enabled[2]={1u,1u};
    reset_fixture(); midi_live_drum_set(&transport,&fixture,1u); before=fixture;
    tk_note_on(36u,100u,TK_SOURCE_LIVE_DRUM); tk_note_on(36u,100u,TK_SOURCE_DRUM); tk_note_on(60u,100u,1u);
    midi_control_cc(&fixture,&transport,0u,0xBFu,7u,87u,&live_shade,&buttons);
    midi_control_cc(&fixture,&transport,0u,0xBFu,11u,59u,&live_shade,&buttons);
    word=tk_source_performance[TK_SOURCE_LIVE_DRUM];
    check((word&127u)==87u && ((word>>7)&127u)==59u,"pad volume/expression are independently controllable");
    for(i=0;i<sizeof IGNORE;++i) midi_control_cc(&fixture,&transport,0u,0xBFu,IGNORE[i],127u,&live_shade,&buttons);
    check(tk_source_performance[TK_SOURCE_LIVE_DRUM]==word &&
        (tk_source_control[TK_SOURCE_LIVE_DRUM]&TK_CONTROLLER_MASK)==TK_BEND_CENTER &&
        tk_source_timbre[TK_SOURCE_LIVE_DRUM]==(64u|(64u<<7)) &&
        !memcmp(&fixture,&before,sizeof fixture) && !buttons,"pad modulation,pedals,RPN,mappings cannot edit keyboard/DRUM sound");
    midi_control_cc(&fixture,&transport,0u,0xBFu,120u,0u,&live_shade,&buttons);
    check(!held(36u,TK_SOURCE_LIVE_DRUM) && held(36u,TK_SOURCE_DRUM) && held(60u,1u),"pad CC120 panic stays source-local");
    command(TK_REMOTE_SET_PAD_ROUTE,enabled,2u); tk_note_on(38u,100u,TK_SOURCE_LIVE_DRUM);
    midi_live_drum_set(&transport,&fixture,0u);
    check(!held(38u,TK_SOURCE_LIVE_DRUM) && held(36u,TK_SOURCE_DRUM) && held(60u,1u),"route disabling cleans pads without stopping other music");
    midi_live_drum_set(&transport,&fixture,1u); tk_note_on(38u,100u,TK_SOURCE_LIVE_DRUM);
    tk_note_on(62u,100u,17u); midi_port_reset(&transport,&fixture,0u);
    check(!midi_live_drum_enabled && !held(38u,TK_SOURCE_LIVE_DRUM) &&
        held(36u,TK_SOURCE_DRUM) && held(62u,17u),"USB loss disables pads and preserves rear/generated owners");
    midi_live_drum_set(&transport,&fixture,1u); tk_note_on(38u,100u,TK_SOURCE_LIVE_DRUM);
    ++usb_remote_generation; tk_remote_service(&fixture,&transport,&live_shade,&live_lfo,track_lfo,track_shade,TK_PAGE_PAD,0u);
    check(!midi_live_drum_enabled && !held(38u,TK_SOURCE_LIVE_DRUM) && held(62u,17u),"USB session reset requires explicit renegotiation");
}
static void recording_sound(void)
{
    uint8_t enabled[]={1u,1u},record[]={0u,1u},stop[]={0u,0u};
    tk_track_patch_t prior; tk_fm_patch_t prior_fm; uint32_t preset;
    reset_fixture(); track_factory_apply(&fixture,TK_TRACK_DRUM,57u); publish_track(&fixture,TK_TRACK_DRUM);
    prior=fixture.track[0]; prior_fm=fixture.fm[1]; preset=fixture.preset;
    command(TK_REMOTE_SET_PAD_ROUTE,enabled,2u); command(TK_REMOTE_RECORD,record,2u);
    check(reply[0]==TK_REMOTE_OK && transport.recording && fixture.preset==preset &&
        !memcmp(&prior,&fixture.track[0],sizeof prior) && !memcmp(&prior_fm,&fixture.fm[1],sizeof prior_fm),
        "DRUM REC retains selected captured sound and live melodic preset");
    check(midi_capture_allowed(&transport,TK_SOURCE_LIVE_DRUM) && !midi_capture_allowed(&transport,1u) &&
        !midi_capture_allowed(&transport,0u),"DRUM capture accepts pads and excludes keyboard/panel");
    command(TK_REMOTE_RECORD,stop,2u); transport.selected=TK_TRACK_MELODY;
    check(!midi_capture_allowed(&transport,TK_SOURCE_LIVE_DRUM) && midi_capture_allowed(&transport,1u) &&
        midi_capture_allowed(&transport,0u),"melody capture accepts keyboard/panel and excludes pads");
    midi_live_drum_set(&transport,&fixture,0u); transport.selected=TK_TRACK_DRUM;
    check(midi_capture_allowed(&transport,0u) && midi_capture_allowed(&transport,1u),"OFF restores original any-live-input recording");
}
int main(void)
{
    protocol_and_lease(); ownership_and_captured_sound(); controller_and_reset_isolation(); recording_sound();
    printf("PASS: %u pad routing app checks (host only)\n",checks); return 0;
}
