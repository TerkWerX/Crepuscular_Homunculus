/* SPDX-License-Identifier: GPL-3.0-only */
/* Portable wire/framing and app-command ownership checks. These mocks do not
 * qualify physical USB, Fire hardware, or the target audio deadline. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "terkinstein_transport.h"
#include "terkinstein_protocol.h"
#include "terkinstein_lfo.h"
#ifdef _MSC_VER
#define __attribute__(x) /* target-timing mock uses ordinary desktop storage */
#endif
#include "terkinstein_screen.h"

#define APP_VER "0.14.0-dev"
#ifndef TK_REMOTE_TIMING_TEST
#define TERKINSTEIN_HOST 1 /* no hardware timing exists in these portable mocks */
#else
/* The optional second build tests target limb math with explicit fake scalar
 * counters. Its values are fixtures, never measured hardware qualification. */
#define FM1_TICKS_PER_US 24u
static volatile uint32_t tk_render_last_ticks, tk_render_max_ticks, tk_render_late_blocks;
#endif
#define TK_TRACKS 4u
#define TK_VOICES 6u
#define TK_PRESET_COUNT 40u
#define TK_INSTRUMENT_DRUM_KIT 2u
#define TK_INSTRUMENT_FM 3u
#define TK_KIT_PRESET_INDEX 30u
enum { TK_TRACK_DRUM, TK_TRACK_MELODY, TK_TRACK_ATMOS, TK_TRACK_BASS };
enum { TK_MODE_OFF, TK_MODE_ARP, TK_MODE_SEQ };
typedef struct { uint8_t shade, fracture, drift, bloom; } tk_params_t;
typedef struct { uint8_t ratio, level, attack, decay, sustain, release; } mock_operator_t;
typedef struct { mock_operator_t op[6]; uint8_t algorithm, feedback, pitch_depth, pitch_time; } mock_fm_t;
typedef mock_fm_t tk_fm_patch_t;
typedef struct { tk_params_t p; uint8_t preset, fx, memory, attack, release,
    lfo_on, lfo_shape, lfo_rate, lfo_depth, instrument; } tk_track_patch_t;
typedef struct {
    tk_params_t p;
    uint8_t tempo, preset, track_mute, length[4], memory, graft_target, graft_amount;
    uint8_t ghost_mask, fx, attack, release, mono;
    tk_pattern_t pattern[4]; tk_track_patch_t track[4]; mock_fm_t fm[6];
} tk_settings_t;
typedef struct { uint8_t mode, running, recording, editing, selected, shown_step, edit_step, next_step, record_length;
    uint32_t next_ms; tk_pattern_t *record_pattern; uint8_t phase_origin[4], bank_changed; } tk_transport_t;
static struct { uint8_t instrument; } PRESETS[40];
static const uint8_t TRACK_SOURCE[4] = {34,33,35,37};
static uint32_t fm1_ms, releases, kills, starts, finishes, patches, forgets, bakes, mono_calls;
static uint32_t published_track, published_shade_source, published_shade, shade_calls;
static uint32_t followed_source, follow_calls;
static void transport_release_part(tk_transport_t *t, uint32_t track) { (void)t; (void)track; releases++; }
static void tk_all_sound_off(uint32_t source) { (void)source; kills++; }
static void publish_track(const tk_settings_t *s, uint32_t track) { (void)s; published_track=track; patches++; }
static void tk_set_source_shade(uint32_t source, uint32_t shade)
{ published_shade_source=source; published_shade=shade; shade_calls++; }
static void tk_follow_source_macros(uint32_t source) { followed_source=source; follow_calls++; }
static uint32_t transport_period(const tk_transport_t *t, const tk_settings_t *s) { (void)t; return 15000u / s->tempo; }
static void transport_retime(tk_transport_t *t, const tk_settings_t *s, uint32_t now) { t->next_ms = now + transport_period(t,s); }
static void transport_retime_seq(tk_transport_t *t, uint32_t old, uint32_t next, uint32_t now)
{ (void)old; t->next_ms = now + next; }
static void transport_capture_finish(tk_transport_t *t, uint32_t now) { (void)now; finishes++; t->record_pattern = NULL; }
static int tk_banks_stopped(tk_transport_t *t);
static void transport_stop(tk_transport_t *t) { if (t->recording) transport_capture_finish(t, fm1_ms); t->running = t->recording = 0; if (tk_banks_stopped(t)) t->bank_changed=1; }
static void transport_start(tk_transport_t *t, uint32_t now) { starts++; t->running = 1; t->shown_step = t->next_step = 0; t->next_ms = now; memset(t->phase_origin,0,sizeof t->phase_origin); }
static void transport_mode(tk_transport_t *t, uint8_t mode, uint32_t now) { transport_stop(t); t->mode = mode; if (mode) transport_start(t,now); }
static void preset_apply(tk_settings_t *s, uint32_t preset) { s->preset = (uint8_t)preset; s->p.shade = (uint8_t)(10u + preset); }
static void tk_forget_memory(void) { forgets++; }
static void track_factory_apply(tk_settings_t *s, uint32_t track, uint32_t preset)
{ s->track[track].preset = (uint8_t)preset; s->track[track].instrument = PRESETS[preset].instrument; s->track[track].p.shade = 64; }
static void track_capture_apply(tk_settings_t *s, uint32_t track)
{ s->track[track].preset = s->preset; s->track[track].instrument = PRESETS[s->preset].instrument; s->track[track].p = s->p; s->fm[track+1u] = s->fm[0]; }
static void graft_bake(tk_settings_t *s) { s->graft_amount = 0; bakes++; }
static void graft_apply(tk_settings_t *s) { s->p.shade = s->graft_amount; }
static void macro_set(tk_params_t *p, uint32_t macro, uint32_t value)
{ if (!macro) p->shade = (uint8_t)value; else if (macro == 1) p->fracture = (uint8_t)value; else if (macro == 2) p->drift = (uint8_t)value; else p->bloom = (uint8_t)value; }
static uint8_t macro_get(const tk_params_t *p, uint32_t macro)
{ return !macro ? p->shade : macro == 1 ? p->fracture : macro == 2 ? p->drift : p->bloom; }
static void tk_set_mono(uint32_t mono) { (void)mono; mono_calls++; }
static int pattern_valid(const tk_pattern_t *pattern)
{
    uint32_t step,n;
    for(step=0;step<64;step++) {
        const tk_pattern_step_t *p=&pattern->step[step];
        if(p->count>4) return 0;
        for(n=0;n<p->count;n++) if(p->note[n].pitch>127||!p->note[n].velocity||p->note[n].velocity>127
            ||(n&&p->note[n-1].pitch>=p->note[n].pitch)) return 0;
    }
    return 1;
}
static int track_patch_valid(const tk_track_patch_t *p)
{
    return p->preset<40&&p->fx<=1&&p->memory<=127&&p->attack<=127&&p->release<=127
        &&p->lfo_on<=1&&p->lfo_shape<=2&&p->lfo_rate<=127&&p->lfo_depth<=127
        &&p->p.shade<=127&&p->p.fracture<=127&&p->p.drift<=127&&p->p.bloom<=127
        &&p->instrument==PRESETS[p->preset].instrument;
}
static int tk_fm_valid(const tk_fm_patch_t *p)
{
    uint32_t n;
    if(p->algorithm>7||p->feedback>127||p->pitch_depth>48||p->pitch_time>127) return 0;
    for(n=0;n<6;n++) if(p->op[n].ratio>31||p->op[n].level>127||p->op[n].attack>127
        ||p->op[n].decay>127||p->op[n].sustain>127||p->op[n].release>127) return 0;
    return 1;
}
#include "terkinstein_banks.h"

static uint8_t mock_input[TK_REMOTE_DATA_MAX], mock_output[TK_REMOTE_WIRE_MAX];
static uint32_t mock_in_length, mock_out_length, mock_generation, mock_tx_calls;
static int mock_ready, mock_tx_busy, mock_reset_in_get;
static int midi_remote_get(const uint8_t **p, uint32_t *n)
{ if (!mock_ready) return 0; *p = mock_input; *n = mock_in_length; if (mock_reset_in_get) { mock_generation++; mock_reset_in_get = 0; } return 1; }
static void midi_remote_done(void) { mock_ready = 0; }
static int midi_remote_send(const uint8_t *p, uint32_t n)
{ mock_tx_calls++; if (mock_tx_busy) return 0; memcpy(mock_output,p,n); mock_out_length=n; return 1; }
static int midi_remote_screen_send(const uint8_t *p, uint32_t n) { return midi_remote_send(p,n); }
static uint32_t midi_remote_generation(void) { return mock_generation; }
#include "terkinstein_remote.h"

static uint32_t checks;
static void expect(int ok, const char *name)
{ checks++; if (!ok) { fprintf(stderr,"FAIL: %s\n",name); exit(1); } }
static tk_settings_t settings;
static tk_transport_t transport;
static tk_lfo_t lfo, track_lfo[4];
static uint8_t live_shade, track_shade[4], sequence;
static uint32_t last_flags;
static uint32_t service(void)
{ return tk_remote_service(&settings,&transport,&live_shade,&lfo,track_lfo,track_shade,6u,1u); }
static void enqueue(uint8_t command, uint8_t id, const uint8_t *p, uint32_t n)
{
    uint8_t wire[TK_REMOTE_WIRE_MAX]; uint32_t length;
    length = tk_remote_encode(wire,sizeof wire,TK_REMOTE_REQUEST,command,id,p,n);
    expect(length != 0,"encode request");
    memcpy(mock_input,wire+1,length-2u); mock_in_length = length-2u; mock_ready = 1;
}
static tk_remote_message_t output(void)
{
    tk_remote_message_t message;
    expect(mock_out_length >= 11u,"reply present");
    expect(tk_remote_decode(mock_output+1,mock_out_length-2u,&message) == 2,"valid reply frame");
    return message;
}
static tk_remote_message_t command(uint8_t cmd, const uint8_t *p, uint32_t n)
{
    sequence = (uint8_t)((sequence+1u)&127u);
    enqueue(cmd,sequence,p,n); last_flags=service(); return output();
}
static void initialize(void)
{
    uint32_t i;
    memset(&settings,0,sizeof settings); memset(&transport,0,sizeof transport);
    settings.tempo = 90; for(i=0;i<4;i++) settings.length[i]=16;
    transport.selected = 1;
    PRESETS[30].instrument = TK_INSTRUMENT_DRUM_KIT;
    for(i=32;i<40;i++) PRESETS[i].instrument = TK_INSTRUMENT_FM;
    mock_generation++; service();
    tk_banks_init(&settings,&transport);
}
static void wire_checks(void)
{
    uint8_t wire[TK_REMOTE_WIRE_MAX], data[TK_REMOTE_PAYLOAD_MAX], guard[TK_REMOTE_WIRE_MAX];
    uint32_t n,i,j,count,pos; tk_remote_message_t m;
    for(n=0;n<=TK_REMOTE_PAYLOAD_MAX;n++) {
        for(i=0;i<n;i++) data[i]=(uint8_t)((i*37u+n)&127u);
        pos=tk_remote_encode(wire,sizeof wire,0,16,127,data,n);
        expect(pos==11u+n,"bounded wire length");
        expect(tk_remote_decode(wire+1,pos-2,&m)==1,"request roundtrip");
        expect(m.length==n&&!memcmp(m.payload,data,n),"payload roundtrip");
        count=tk_remote_packet_count(pos); j=0;
        for(i=0;i<count;i++) {
            uint32_t pkt=tk_remote_packet(wire,pos,i*3u),cin=pkt&15u,k=cin==4u||cin==7u?3u:cin-4u;
            expect((i+1u<count)==(cin==4u),"only last packet ends SysEx");
            for(pos=0;pos<k;pos++) guard[j++]=(uint8_t)(pkt>>(8u*(pos+1u)));
            pos=11u+n;
        }
        expect(j==pos&&!memcmp(guard,wire,pos),"actual USB MIDI CIN packet roundtrip");
        for(i=1;i+1u<pos;i++) {
            wire[i]^=0x80u;
            if(i>3u) expect(tk_remote_decode(wire+1,pos-2,&m)<0,"reject every high data bit");
            wire[i]^=0x80u;
        }
    }
    memset(guard,0xCC,sizeof guard);
    expect(!tk_remote_encode(guard,sizeof guard,0,1,1,data,81),"reject oversized payload");
    expect(guard[0]==0xCC,"failed encode leaves destination untouched");
    expect(!tk_remote_namespace((const uint8_t*)"\x22\x24\x35\x7f",4),"OTA soft key namespace is foreign");
    expect(!tk_remote_namespace((const uint8_t*)"\x00\x59\x11",3),"packed OTA namespace is foreign");
    expect(tk_remote_namespace((const uint8_t*)"\x7d\x54\x4b\x02",4),"future app versions still isolated from OTA");
    expect(!tk_remote_packet_count(0)&&!tk_remote_packet_count(92),"TX refuses unbounded frames");
}
static void app_checks(void)
{
    tk_remote_message_t m; uint8_t p[32], saved[TK_REMOTE_WIRE_MAX]; uint32_t saved_n,i,prior,revision,pattern_revision;
    initialize();
    m=command(TK_REMOTE_HELLO,NULL,0);
    expect(m.payload[0]==0&&m.payload[1]==1&&m.payload[4]==4&&m.payload[8]==40,"hello capabilities and expanded bank");
    expect(m.payload[2]==127&&m.payload[3]==15,"HELLO advertises macros, banks/song, screen and opt-in palette/burst explicitly");
#ifdef TK_REMOTE_TIMING_TEST
    tk_render_last_ticks=24u*1379u;tk_render_max_ticks=24u*2890u;tk_render_late_blocks=0xFFFFFFFFu;
    m=command(TK_REMOTE_LOAD,NULL,0);
    expect(m.length==14&&m.payload[0]==0&&m.payload[1]==1,"target counter availability flag");
    {
        const uint32_t values[3]={1379u,2890u,268435455u};
        uint32_t item;
        for(item=0;item<3;item++) {
            uint32_t decoded=0;
            for(i=0;i<4;i++)decoded|=(uint32_t)m.payload[2+item*4+i]<<(i*7u);
            expect(decoded==values[item],"target fixture unit conversion and 28-bit saturation");
        }
    }
#else
    m=command(TK_REMOTE_LOAD,NULL,0);
    expect(m.length==14&&m.payload[0]==0&&m.payload[1]==0,"host diagnostics explicitly unavailable");
    for(i=2;i<14;i++)expect(m.payload[i]==0,"host does not invent measured timing");
#endif
    m=command(TK_REMOTE_STATE,NULL,0);
    expect(m.length==79&&m.payload[0]==0&&m.payload[2]==TK_MODE_OFF,"query does not start sequence");
    expect(m.payload[4]==1&&m.payload[7]==90&&m.payload[76]==0,"confirmed settings offsets");
    revision=m.payload[30]|m.payload[31]<<7;
    pattern_revision=m.payload[77]|m.payload[78]<<7;
    settings.p.shade=99;
    m=command(TK_REMOTE_STATE,NULL,0);
    expect((uint32_t)(m.payload[30]|m.payload[31]<<7)!=revision,"macro edit advances full revision");
    expect((uint32_t)(m.payload[77]|m.payload[78]<<7)==pattern_revision,"macro edit does not invalidate note bank");
    revision=m.payload[30]|m.payload[31]<<7;
    settings.pattern[3].step[63].count=1;
    settings.pattern[3].step[63].note[0].pitch=41; settings.pattern[3].step[63].note[0].velocity=100;
    settings.pattern[3].step[63].note[0].duration_q4=255;
    m=command(TK_REMOTE_STATE,NULL,0);
    expect(m.payload[71]==1,"step63 occupancy high bank");
    expect((uint32_t)(m.payload[30]|m.payload[31]<<7)!=revision,"physical settings edit advances revision");
    expect((uint32_t)(m.payload[77]|m.payload[78]<<7)!=pattern_revision,"note edit advances separate pattern revision");
    revision=m.payload[30]|m.payload[31]<<7;
    settings.pattern[3].step[63].note[0].pitch=42;
    m=command(TK_REMOTE_STATE,NULL,0);
    expect((uint32_t)(m.payload[30]|m.payload[31]<<7)!=revision,"same occupancy pitch edit advances revision");
    p[0]=3;p[1]=63;m=command(TK_REMOTE_STEP,p,2);
    expect(m.length==8&&m.payload[4]==42&&m.payload[6]==127&&m.payload[7]==1,"full held gate serializes seven bit safely");
    p[0]=1; m=command(TK_REMOTE_TRANSPORT,p,1); expect(m.payload[0]==0&&transport.running&&starts==1,"explicit start");
    saved_n=mock_out_length; memcpy(saved,mock_output,saved_n);
    transport.shown_step=12; enqueue(TK_REMOTE_TRANSPORT,sequence,p,1); service();
    expect(starts==1&&transport.shown_step==12,"duplicate start cannot restart");
    expect(mock_out_length==saved_n&&!memcmp(saved,mock_output,saved_n),"duplicate reply is identical");
    enqueue(TK_REMOTE_TRANSPORT,sequence,p,1); mock_input[mock_in_length-1]^=1; service();
    expect(output().payload[0]==TK_REMOTE_BAD_FRAME,"malformed frame gets rejection");
    enqueue(TK_REMOTE_TRANSPORT,sequence,p,1); service();
    expect(output().payload[0]==0&&starts==1,"malformed traffic does not overwrite replay cache");
    p[0]=0;enqueue(TK_REMOTE_TRANSPORT,sequence,p,1);service();
    expect(output().payload[0]==TK_REMOTE_ID_CONFLICT&&transport.running,"different payload same ID rejected without stop");
    p[0]=0;m=command(TK_REMOTE_TRANSPORT,p,1); expect(m.payload[0]==0&&!transport.running,"explicit stop");
    p[0]=2;m=command(TK_REMOTE_TRANSPORT,p,1); expect(m.payload[0]==0&&transport.running&&starts==1,"continue preserves transport position");
    /* Atomic chord edit including an octave and long gate. */
    p[0]=2;p[1]=20;p[2]=2;p[3]=36;p[4]=92;p[5]=127;p[6]=1;p[7]=60;p[8]=100;p[9]=8;p[10]=0;
    m=command(TK_REMOTE_SET_STEP,p,11);
    expect(m.payload[0]==0&&m.length==12&&settings.pattern[2].step[20].count==2,"atomic two-note step");
    prior=settings.pattern[2].step[20].note[1].pitch; p[7]=20;
    m=command(TK_REMOTE_SET_STEP,p,11);
    expect(m.payload[0]==TK_REMOTE_BAD_ARGUMENT&&settings.pattern[2].step[20].note[1].pitch==prior,"unsorted edit leaves entire step unchanged");
    p[7]=60;transport.recording=1;transport.selected=2;
    m=command(TK_REMOTE_SET_STEP,p,11);expect(m.payload[0]==TK_REMOTE_BUSY,"recording track step mutation rejected");
    p[0]=3;m=command(TK_REMOTE_SET_STEP,p,11);expect(m.payload[0]==0,"other track editable during recording");
    p[0]=2;p[1]=2;m=command(TK_REMOTE_LENGTH,p,2);expect(m.payload[0]==TK_REMOTE_BUSY,"length while recording rejected");
    p[0]=2;p[1]=0;m=command(TK_REMOTE_RECORD,p,2);expect(m.payload[0]==0&&!transport.recording&&transport.running,"record stop keeps playback");
    p[1]=2;m=command(TK_REMOTE_LENGTH,p,2);expect(m.payload[0]==0&&settings.length[2]==64,"64 steps selected");
    p[1]=0;m=command(TK_REMOTE_LENGTH,p,2);expect(m.payload[0]==0&&settings.pattern[2].step[20].count==2,"shrinking keeps hidden notes");
    p[0]=0;p[1]=1;m=command(TK_REMOTE_RECORD,p,2);
    expect(m.payload[0]==0&&transport.recording&&settings.preset==30,"DRUM recording explicitly selects kit");
    settings.pattern[0].step[2].count=1;
    m=command(TK_REMOTE_RECORD,p,2);expect(m.payload[0]==0&&settings.pattern[0].step[2].count==1,"already recording start preserves captured notes");
    p[0]=1;m=command(TK_REMOTE_RECORD,p,2);expect(m.payload[0]==TK_REMOTE_BUSY,"other recording track cannot replace capture");
    p[0]=0;p[1]=0;command(TK_REMOTE_RECORD,p,2);
    p[0]=40;m=command(TK_REMOTE_LIVE_PRESET,p,1);expect(m.payload[0]==TK_REMOTE_BAD_ARGUMENT,"preset outside bank rejected");
    p[0]=32;m=command(TK_REMOTE_LIVE_PRESET,p,1);expect(m.payload[0]==0&&settings.preset==32,"FM preset selected");
    p[0]=5;p[1]=0;p[2]=31;m=command(TK_REMOTE_OPERATOR,p,3);
    expect(m.payload[0]==0&&settings.fm[0].op[5].ratio==31,"operator ratio max accepted");
    p[2]=32;m=command(TK_REMOTE_OPERATOR,p,3);expect(m.payload[0]==TK_REMOTE_BAD_ARGUMENT&&settings.fm[0].op[5].ratio==31,"operator ratio bounds atomic");
    p[0]=2;p[1]=48;m=command(TK_REMOTE_FM_GLOBAL,p,2);
    expect(m.payload[0]==0&&settings.fm[0].pitch_depth==48,"pitch envelope maximum");
    p[1]=49;m=command(TK_REMOTE_FM_GLOBAL,p,2);expect(m.payload[0]==TK_REMOTE_BAD_ARGUMENT,"pitch envelope bounded");
    p[0]=0;m=command(TK_REMOTE_FM,p,1);
    expect(m.length==42&&m.payload[4]==48&&m.payload[36]==31,"explicit FM wire field order independent of struct layout");
    p[0]=1;m=command(TK_REMOTE_VOICE_MODE,p,1);expect(m.payload[0]==0&&settings.mono==1&&mono_calls==1,"MONO absolute control");
    m=command(TK_REMOTE_VOICE_MODE,p,1);expect(m.payload[0]==0&&mono_calls==1,"repeated MONO value does not republish");
    /* Blocked output never executes a mutation twice or consumes queued input. */
    p[0]=1;sequence=(uint8_t)((sequence+1u)&127u);enqueue(TK_REMOTE_TRANSPORT,sequence,p,1);
    prior=starts;mock_tx_busy=1;service();expect(starts==prior+1&&remote_reply_pending,"busy TX defers complete reply");
    for(i=0;i<100;i++) service();expect(starts==prior+1,"busy output bounded without repeated start");
    mock_tx_busy=0;service();expect(!remote_reply_pending,"deferred output eventually sends");
    enqueue(TK_REMOTE_TRANSPORT,sequence,p,1);service();expect(starts==prior+1,"retry after TX blockage idempotent");
    /* Looped-back replies do not execute commands. */
    memcpy(mock_input,mock_output+1,mock_out_length-2u);mock_in_length=mock_out_length-2u;mock_ready=1;
    prior=starts;service();expect(starts==prior,"incoming reply cannot loop into command");
    sequence=(uint8_t)((sequence+1u)&127u);enqueue(TK_REMOTE_TRANSPORT,sequence,p,1);mock_reset_in_get=1;
    service();expect(starts==prior,"USB reset during copy cancels old command");
    enqueue(TK_REMOTE_TRANSPORT,sequence,p,1);service();expect(starts==prior+1,"USB reset clears duplicate ownership");
}
static void track_macro_checks(void)
{
    tk_settings_t saved;
    tk_transport_t saved_transport;
    tk_remote_message_t m;
    uint8_t p[4], bytes[80], saved_wire[TK_REMOTE_WIRE_MAX];
    uint32_t track, macro, i, before_patches, before_follow, before_shade, before_releases, before_kills;
    uint32_t before_revision, before_pattern_revision, out_n, wire_n;
    initialize();
    for (track=0;track<4;track++) {
        for (macro=0;macro<4;macro++) macro_set(&settings.track[track].p,macro,20u+track*4u+macro);
        tk_lfo_init(&track_lfo[track],fm1_ms);
    }
    command(TK_REMOTE_STATE,NULL,0);
    before_revision=remote_revision;before_pattern_revision=remote_pattern_revision;
    saved=settings;saved_transport=transport;
    before_patches=patches;before_follow=follow_calls;before_shade=shade_calls;
    for (track=0;track<4;track++) {
        p[0]=(uint8_t)track;m=command(TK_REMOTE_TRACK_MACROS,p,1);
        expect(m.length==6&&m.payload[0]==0&&m.payload[1]==track,"captured macros query has explicit track and four bytes");
        for(macro=0;macro<4;macro++)expect(m.payload[2+macro]==macro_get(&saved.track[track].p,macro),"captured query returns independent track values");
        expect(last_flags==0,"captured macro read requests neither dirty nor UI mutation");
    }
    expect(!memcmp(&saved,&settings,sizeof settings)&&!memcmp(&saved_transport,&transport,sizeof transport),"captured macro queries leave musical settings and transport immutable");
    expect(remote_revision==before_revision&&remote_pattern_revision==before_pattern_revision,"captured queries do not advance either revision");
    expect(patches==before_patches&&follow_calls==before_follow&&shade_calls==before_shade,"captured queries publish no audio controls");

    for(track=0;track<4;track++)for(macro=0;macro<4;macro++) {
        saved=settings;saved_transport=transport;
        before_patches=patches;before_follow=follow_calls;before_releases=releases;before_kills=kills;
        p[0]=(uint8_t)track;p[1]=(uint8_t)macro;p[2]=(uint8_t)((track+macro)&1u?127u:0u);
        macro_set(&saved.track[track].p,macro,p[2]);
        m=command(TK_REMOTE_SET_TRACK_MACRO,p,3);
        expect(last_flags==3&&m.length==6&&m.payload[0]==0&&m.payload[1]==track,"accepted track macro returns confirmed patch and dirty flags");
        expect(!memcmp(&saved,&settings,sizeof settings),"track macro edits preserve every other captured and live setting");
        expect(!memcmp(&saved_transport,&transport,sizeof transport)&&releases==before_releases&&kills==before_kills,"macro edits preserve held gates and do not release generated notes");
        expect(patches==before_patches+1&&published_track==track&&follow_calls==before_follow+1&&followed_source==TRACK_SOURCE[track],"only edited track receives packed patch follow publication");
        expect(published_shade_source==TRACK_SOURCE[track]&&published_shade==settings.track[track].p.shade,"track SHADE publication remains source isolated");
        for(i=0;i<4;i++)expect(m.payload[2+i]==macro_get(&settings.track[track].p,i),"ACK contains all accepted captured macro values");
        before_patches=patches;before_follow=follow_calls;
        m=command(TK_REMOTE_SET_TRACK_MACRO,p,3);
        expect(m.payload[0]==0&&last_flags==0&&patches==before_patches&&follow_calls==before_follow,"same track macro value causes no redundant publication or dirty flag");
    }
    command(TK_REMOTE_STATE,NULL,0);
    expect(remote_revision!=before_revision&&remote_pattern_revision==before_pattern_revision,"track sound edit changes settings revision while preserving note-cache revision");

    saved=settings;before_patches=patches;before_follow=follow_calls;
    for(i=0;i<5;i++) {
        p[0]=(uint8_t)(i==0?4:1);p[1]=(uint8_t)(i==1?4:0);p[2]=64;p[3]=0;
        m=command(TK_REMOTE_SET_TRACK_MACRO,p,i==2?2:i==3?4:i==4?0:3);
        expect(m.payload[0]==TK_REMOTE_BAD_ARGUMENT&&last_flags==0,"track macro invalid target/index/length rejected before mutation");
        expect(!memcmp(&saved,&settings,sizeof settings)&&patches==before_patches&&follow_calls==before_follow,"invalid track macro cannot alter settings or audio publication");
    }
    p[0]=1;p[1]=0;p[2]=128;
    m.command=TK_REMOTE_SET_TRACK_MACRO;m.length=3;m.payload=p;
    expect(tk_remote_command(&m,&settings,&transport,&live_shade,&lfo,track_lfo,track_shade,6,1,bytes,&out_n)==0&&bytes[0]==TK_REMOTE_BAD_ARGUMENT,"direct malformed macro value rejected beyond seven-bit range");
    expect(!memcmp(&saved,&settings,sizeof settings)&&patches==before_patches,"out-of-range value leaves complete saved patch unchanged");
    p[0]=4;m=command(TK_REMOTE_TRACK_MACROS,p,1);expect(m.payload[0]==TK_REMOTE_BAD_ARGUMENT,"query track outside range rejected");
    p[0]=1;m=command(TK_REMOTE_TRACK_MACROS,p,0);expect(m.payload[0]==TK_REMOTE_BAD_ARGUMENT,"query needs exactly one track byte");

    transport.recording=1;transport.selected=2;
    p[0]=2;p[1]=1;p[2]=61;saved=settings;before_patches=patches;
    m=command(TK_REMOTE_SET_TRACK_MACRO,p,3);
    expect(m.payload[0]==TK_REMOTE_BUSY&&last_flags==0&&!memcmp(&saved,&settings,sizeof settings)&&patches==before_patches,"recording owner rejects captured macro mutation");
    p[0]=3;m=command(TK_REMOTE_SET_TRACK_MACRO,p,3);
    expect(m.payload[0]==0&&settings.track[3].p.fracture==61&&transport.recording&&transport.selected==2,"another track remains editable during capture");
    p[0]=2;m=command(TK_REMOTE_TRACK_MACROS,p,1);expect(m.payload[0]==0,"recording track macros remain queryable");
    transport.recording=0;

    p[0]=1;p[1]=2;p[2]=73;before_patches=patches;
    m=command(TK_REMOTE_SET_TRACK_MACRO,p,3);wire_n=mock_out_length;memcpy(saved_wire,mock_output,wire_n);
    enqueue(TK_REMOTE_SET_TRACK_MACRO,sequence,p,3);expect(service()==0,"track macro retry does not execute again");
    expect(patches==before_patches+1&&mock_out_length==wire_n&&!memcmp(saved_wire,mock_output,wire_n),"track macro retries replay exact accepted patch without republishing");
}

static void preserved_record_checks(void)
{
    tk_remote_message_t m;
    tk_settings_t saved;
    uint8_t p[4]={0,1,1,0};
    uint32_t track, before_starts, before_patches;
    initialize();
    settings.preset=32;settings.p.shade=99;settings.fm[0].op[0].ratio=31;
    for(track=0;track<4;track++) {
        settings.track[track].preset=(uint8_t)(10+track);
        settings.track[track].p.shade=(uint8_t)(40+track);
        settings.fm[track+1].op[0].ratio=(uint8_t)(track+1);
    }
    saved=settings;
    for(track=0;track<4;track++) {
        p[0]=(uint8_t)track;p[1]=1;p[2]=1;
        m=command(TK_REMOTE_RECORD,p,3);
        expect(m.payload[0]==0&&transport.recording&&transport.selected==track,"station REC explicitly starts its owned track");
        expect(!memcmp(settings.track,saved.track,sizeof settings.track)&&!memcmp(settings.fm,saved.fm,sizeof settings.fm),"station REC retains every captured preset macro and FM operator patch");
        expect(settings.preset==saved.preset&&!memcmp(&settings.p,&saved.p,sizeof saved.p),"station REC leaves live preset and macro sound untouched including DRUM");
        settings.pattern[track].step[2].count=1;
        before_starts=starts;before_patches=patches;
        m=command(TK_REMOTE_RECORD,p,3);
        expect(m.payload[0]==0&&settings.pattern[track].step[2].count==1&&starts==before_starts&&patches==before_patches,"repeated station REC cannot erase newly captured notes");
        p[1]=0;command(TK_REMOTE_RECORD,p,2);
    }
    saved=settings;before_starts=starts;before_patches=patches;
    p[0]=0;p[1]=1;p[2]=2;m=command(TK_REMOTE_RECORD,p,3);
    expect(m.payload[0]==TK_REMOTE_BAD_ARGUMENT&&!memcmp(&saved,&settings,sizeof settings)&&starts==before_starts&&patches==before_patches,"invalid preserved-patch flag validated before any recording mutation");
    p[2]=1;m=command(TK_REMOTE_RECORD,p,4);expect(m.payload[0]==TK_REMOTE_BAD_ARGUMENT,"unexpected fourth RECORD byte is rejected");
    p[0]=0;p[1]=1;p[2]=0;m=command(TK_REMOTE_RECORD,p,3);
    expect(m.payload[0]==0&&settings.preset==30&&settings.track[0].preset==30,"explicit preserve flag zero retains legacy drum/live capture behavior");
}
static void bank_checks(void)
{
    tk_remote_message_t m;
    uint8_t p[64]={0};
    tk_settings_t before;
    uint32_t track,bank,prior;
    initialize();
    settings.pattern[0].step[63].count=1;
    settings.pattern[0].step[63].note[0].pitch=36;
    settings.pattern[0].step[63].note[0].velocity=80;
    m=command(TK_REMOTE_BANK_STATE,NULL,0);
    expect(m.length==73&&m.payload[1]==1&&!m.payload[2],"bank state fixed72body layout and idle import");
    expect(m.payload[65]==1&&m.payload[69]==1,"A adopts existing hidden last-step data and observes its change");
    for(track=0;track<4;track++) for(bank=0;bank<4;bank++) {
        p[0]=(uint8_t)track;p[1]=(uint8_t)bank;p[2]=63;
        m=command(TK_REMOTE_BANK_STEP,p,3);
        expect(m.payload[0]==0&&m.payload[1]==track&&m.payload[2]==bank&&m.payload[3]==63,"read bank includes explicit owner IDs");
        expect(m.payload[4]==(track==0&&bank==0?1:0),"new alternate banks remain empty");
    }
    p[0]=0;p[1]=1;p[2]=0;p[3]=1;p[4]=38;p[5]=100;p[6]=127;p[7]=1;
    m=command(TK_REMOTE_SET_BANK_STEP,p,8);
    expect(m.payload[0]==0&&m.length==9&&m.payload[2]==1&&m.payload[5]==38,"inactive step write returns complete accepted chord");
    expect(!settings.pattern[0].step[0].count&&last_flags==TK_REMOTE_CHANGED,"inactive bank cannot modify active flash patch");
    p[2]=2;m=command(TK_REMOTE_SET_BANK_LENGTH,p,3);
    expect(m.payload[0]==0&&settings.length[0]==16&&tk_banks.length[0][1]==64,"inactive bank length isolated");
    transport.mode=TK_MODE_SEQ;transport.running=1;transport.next_step=5;transport.shown_step=4;
    m=command(TK_REMOTE_SELECT_BANK,p,2);
    expect(m.payload[0]==0&&tk_banks.active[0]==0&&tk_banks.pending[0]==1,"playing switch is queued until local loop boundary");
    expect(!tk_banks_boundary(&settings,&transport,15)&&tk_banks.active[0]==0,"pending bank never replaces a mid-loop event");
    prior=releases;
    expect(tk_banks_boundary(&settings,&transport,16)&&tk_banks.active[0]==1,"bank commits on old16step boundary");
    expect(settings.length[0]==64&&tk_transport_local_step(&transport,0,16,64)==0,"16-to64 switch begins at new step one");
    expect(releases==prior+1&&settings.pattern[0].step[0].note[0].pitch==38,"only target generated part released");
    p[1]=0;p[2]=63;m=command(TK_REMOTE_BANK_STEP,p,3);
    expect(m.payload[4]==1&&m.payload[5]==36,"outgoing bank preserves every hidden step");
    before=settings;
    transport.recording=1;transport.selected=0;p[1]=2;
    m=command(TK_REMOTE_SELECT_BANK,p,2);
    expect(m.payload[0]==TK_REMOTE_BUSY&&!memcmp(&before,&settings,sizeof settings),"recording owner cannot change bank");
    p[2]=1;m=command(TK_REMOTE_COPY_BANK,p,3);expect(m.payload[0]==TK_REMOTE_BUSY,"recording owner protects bank copies");
    m=command(TK_REMOTE_SONG_BEGIN,NULL,0);expect(m.payload[0]==TK_REMOTE_BUSY,"song import cannot end active capture implicitly");
    transport.recording=0;p[1]=2;command(TK_REMOTE_SELECT_BANK,p,2);transport_stop(&transport);
    expect(tk_banks.active[0]==2&&!transport.running,"STOP commits queued bank without autoplay");
    before=settings;
    m=command(TK_REMOTE_SONG_BEGIN,NULL,0);
    expect(m.payload[0]==0&&tk_banks.importing&&!transport.running,"BEGIN isolates stage and stays stopped");
    p[0]=0;p[1]=2;p[2]=7;p[3]=1;p[4]=41;p[5]=88;p[6]=4;p[7]=0;
    m=command(TK_REMOTE_SONG_STEP,p,8);
    expect(m.payload[0]==0&&!memcmp(&before,&settings,sizeof settings),"staged edits cannot partially publish to active song");
    m=command(TK_REMOTE_SET_BANK_STEP,p,8);expect(m.payload[0]==TK_REMOTE_BUSY,"normal bank writes blocked during import");
    m=command(TK_REMOTE_RECORD,p,2);expect(m.payload[0]==TK_REMOTE_BUSY,"REC blocked during import");
    command(TK_REMOTE_SONG_ABORT,NULL,0);
    expect(!tk_banks.importing&&!memcmp(&before,&settings,sizeof settings),"ABORT preserves current complete song");
    command(TK_REMOTE_SONG_BEGIN,NULL,0);command(TK_REMOTE_SONG_STEP,p,8);
    p[5]=0;m=command(TK_REMOTE_SONG_STEP,p,8);
    expect(m.payload[0]==TK_REMOTE_BAD_ARGUMENT,"invalid staged velocity rejected");
    m=command(TK_REMOTE_SONG_COMMIT,NULL,0);
    expect(m.payload[0]==TK_REMOTE_BAD_ARGUMENT&&!memcmp(&before,&settings,sizeof settings),"failed stage cannot commit partial accepted import");
    command(TK_REMOTE_SONG_ABORT,NULL,0);
    command(TK_REMOTE_SONG_BEGIN,NULL,0);p[5]=88;command(TK_REMOTE_SONG_STEP,p,8);
    m=command(TK_REMOTE_SONG_COMMIT,NULL,0);
    expect(m.payload[0]==0&&!tk_banks.importing&&!transport.running&&settings.pattern[0].step[7].note[0].pitch==41,"validated stage atomically publishes without restarting");
    expect(settings.pattern[0].step[7].note[0].duration_q4==4,"import preserves musical gates");
    prior=patches;enqueue(TK_REMOTE_SONG_COMMIT,sequence,NULL,0);service();
    expect(patches==prior,"exact COMMIT retry never reapplies song or kills notes twice");
    before=settings;command(TK_REMOTE_SONG_BEGIN,NULL,0);p[4]=42;command(TK_REMOTE_SONG_STEP,p,8);
    mock_generation++;service();
    expect(!tk_banks.importing&&!memcmp(&before,&settings,sizeof settings),"USB reset abandons partial staging without song replacement");
    p[0]=0;m=command(TK_REMOTE_TRACK_PATCH,p,1);
    expect(m.payload[0]==0&&m.length==56&&m.payload[1]==0,"full captured patch has55body bytes");
    expect(!memcmp(m.payload+2,&settings.track[0],14),"captured wire patch follows explicit14byte field order");
}
static void screen_command_checks(void)
{
    tk_remote_message_t m;
    uint8_t enabled = 2;
    uint32_t calls, offset;
    tk_settings_t before;
    initialize(); before = settings;
    m = command(TK_REMOTE_SCREEN_SUBSCRIBE, &enabled, 1);
    expect(m.length == 2 && !m.payload[0] && m.payload[1] == 1 && !last_flags &&
        !memcmp(&before, &settings, sizeof settings), "screen subscribe ACK does not alter musical settings");
    tk_remote_screen_service(1); m = output();
    expect(m.command == TK_REMOTE_SCREEN_TILE && m.payload[0] == 0, "screen event has separate command from request ACK");
    m = command(TK_REMOTE_SCREEN_TILE, NULL, 0);
    expect(m.payload[0] == TK_REMOTE_UNSUPPORTED, "screen reply-event command cannot execute as request");
    fm1_ms += 4; calls = mock_tx_calls; offset = tk_screen.offset;
    enqueue(TK_REMOTE_STATE, (uint8_t)((sequence + 1u) & 127u), NULL, 0);
    tk_remote_screen_service(1);
    expect(mock_tx_calls == calls && tk_screen.offset == offset, "new ordinary request blocks screen traffic without consuming inbox");
    mock_tx_busy = 1; service(); calls = mock_tx_calls;
    tk_remote_screen_service(1);
    expect(remote_reply_pending && mock_tx_calls == calls && tk_screen.offset == offset,
        "unsent ordinary reply takes priority over screen traffic");
    mock_tx_busy = 0; service();
    enabled = 0; sequence = (uint8_t)((sequence + 1u) & 127u);
    m = command(TK_REMOTE_SCREEN_SUBSCRIBE, &enabled, 1);
    expect(m.length == 2 && !m.payload[1] && !tk_screen.enabled, "unsubscribe acknowledged and stream stopped");
    enabled = 5; m = command(TK_REMOTE_SCREEN_SUBSCRIBE, &enabled, 1);
    expect(m.payload[0] == TK_REMOTE_BAD_ARGUMENT && !tk_screen.enabled, "invalid subscription value cannot enable traffic");
    enabled = 3; m = command(TK_REMOTE_SCREEN_SUBSCRIBE, &enabled, 1);
    expect(!m.payload[0] && m.payload[1] == 1 && tk_screen.advanced, "opt-in advanced refresh ACK enables palette/burst");
    enabled = 4; m = command(TK_REMOTE_SCREEN_SUBSCRIBE, &enabled, 1);
    expect(!m.payload[0] && tk_screen.advanced && !last_flags, "advanced heartbeat keeps musical settings untouched");
    enabled = 2; m = command(TK_REMOTE_SCREEN_SUBSCRIBE, &enabled, 1);
    expect(!m.payload[0] && !tk_screen.advanced, "legacy client explicitly selects compatible codecs");
}
int main(void)
{
    wire_checks();app_checks();track_macro_checks();preserved_record_checks();bank_checks();screen_command_checks();
    printf("PASS remote protocol: %u checks\n",checks);
    return 0;
}
