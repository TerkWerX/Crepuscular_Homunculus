/* SPDX-License-Identifier: GPL-3.0-only */
/* A volatile Fire SOLO lease must never rewrite saved mute/song state. */
#define TERKINSTEIN_HOST 1
#define main tk_solo_host_main
#include "../firmware/src/terkinstein.c"
#undef main

static uint32_t checks, reply_length;
static uint8_t reply[TK_REMOTE_PAYLOAD_MAX], live_shade, track_shade[TK_TRACKS];
static tk_settings_t fixture, before;
static tk_transport_t transport;
static tk_lfo_t live_lfo, track_lfo[TK_TRACKS];

static void check(int condition,const char *why)
{
    ++checks;
    if (!condition) { fprintf(stderr,"FAIL solo %u: %s\n",checks,why); exit(1); }
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
    for (track=0;track<TK_TRACKS;track++) {
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
static void protocol_and_lease(void)
{
    static const uint8_t DRUM[]={0u}, ATMOS[]={2u}, OFF[]={4u}, BAD[]={5u};
    uint32_t flags,now;
    reset_fixture(); before=fixture;
    flags=command(TK_REMOTE_SOLO,0,0u);
    check(!flags && reply_length==2u && reply[0]==TK_REMOTE_OK && reply[1]==4u,
          "query reports OFF without a saved setting");
    command(TK_REMOTE_SOLO,BAD,1u);
    check(reply[0]==TK_REMOTE_BAD_ARGUMENT && !transport.solo_track,"invalid track rejected");
    flags=command(TK_REMOTE_SOLO,ATMOS,1u);
    check(flags==TK_REMOTE_CHANGED && reply[0]==TK_REMOTE_OK && reply[1]==2u &&
          transport.solo_track==3u,"ATMOS solo is volatile and acknowledges exact part");
    check(!memcmp(&fixture,&before,sizeof fixture),"SOLO never changes saved mutes or any settings");
    fm1_ms=2999u; flags=command(TK_REMOTE_SOLO,ATMOS,1u);
    check(!flags && transport.solo_seen_ms==2999u,"same SOLO is a quiet lease heartbeat");
    check(!transport_solo_expire(&transport,&fixture,5998u),"lease survives 2999ms");
    check(transport_solo_expire(&transport,&fixture,5999u) && !transport.solo_track,
          "lease restores normal mix at 3000ms");
    fm1_ms=0xFFFFFF00u;command(TK_REMOTE_SOLO,DRUM,1u);now=fm1_ms;
    check(!transport_solo_expire(&transport,&fixture,now+2999u),"lease survives wrap");
    check(transport_solo_expire(&transport,&fixture,now+3000u),"lease expires across wrap");
    command(TK_REMOTE_SOLO,ATMOS,1u);command(TK_REMOTE_SOLO,OFF,1u);
    check(!transport.solo_track && reply[1]==4u,"explicit OFF restores normal track mix");
}
static void source_isolation_and_disconnect(void)
{
    static const uint8_t BASS[]={3u},OFF[]={4u};
    reset_fixture();
    tk_note_on(36u,100u,TK_SOURCE_DRUM);
    tk_note_on(40u,100u,TK_SOURCE_BASS);
    tk_note_on(60u,100u,1u); /* USB keyboard, never sequencer-owned */
    transport.playing[0].active=1u;transport.playing[0].part=TK_TRACK_DRUM;
    transport.playing[0].pitch=36u;
    transport.playing[1].active=1u;transport.playing[1].part=TK_TRACK_BASS;
    transport.playing[1].pitch=40u;
    command(TK_REMOTE_SOLO,BASS,1u);
    check(!transport.playing[0].active && transport.playing[1].active,
          "activating solo releases an already sounding other sequence part");
    check(tk_note_is_held(60u,1u),"keyboard note owner remains audible");
    midi_port_reset(&transport,&fixture,0u);
    check(!transport.solo_track,"USB reset clears volatile SOLO");
    command(TK_REMOTE_SOLO,BASS,1u);
    ++usb_remote_generation;
    check(tk_remote_service(&fixture,&transport,&live_shade,&live_lfo,track_lfo,
                            track_shade,TK_PAGE_PAD,0u)&TK_REMOTE_CHANGED,
          "remote generation reset reports the cleared SOLO");
    check(!transport.solo_track,"remote session reset clears volatile SOLO");
    command(TK_REMOTE_SOLO,OFF,1u);
}
static void sequencer_respects_solo_and_saved_mutes(void)
{
    static const uint8_t ATMOS[]={2u},OFF[]={4u};
    uint32_t track,active;
    reset_fixture();
    for (track=0;track<TK_TRACKS;track++)
        check(tk_pattern_record(&fixture.pattern[track],0u,48u+track*3u,100u),
              "fixture records each track's first step");
    fixture.track_mute=1u<<TK_TRACK_MELODY;
    before=fixture;
    transport.mode=TK_MODE_SEQ;
    transport_start(&transport,0u);transport_tick(&transport,&fixture,0u);
    active=0u;
    for (track=0;track<TK_VOICES;track++)
        if (transport.playing[track].active) active|=1u<<transport.playing[track].part;
    check((active&15u)==(15u&~(1u<<TK_TRACK_MELODY)),
          "ordinary saved mute applies when SOLO is off");
    transport_stop(&transport);
    command(TK_REMOTE_SOLO,ATMOS,1u);
    transport_start(&transport,100u);transport_tick(&transport,&fixture,100u);
    active=0u;
    for (track=0;track<TK_VOICES;track++)
        if (transport.playing[track].active) active|=1u<<transport.playing[track].part;
    check(active==(1u<<TK_TRACK_ATMOS),"solo emits only its chosen sequencer part");
    command(TK_REMOTE_SOLO,OFF,1u);
    check(!memcmp(&fixture,&before,sizeof fixture),"saved mute and all patterns survive SOLO");
}
int main(void)
{
    protocol_and_lease();source_isolation_and_disconnect();sequencer_respects_solo_and_saved_mutes();
    printf("PASS: %u solo app checks (host only)\n",checks); return 0;
}
