/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "terkinstein_engine.h"
static uint32_t checks;
static const tk_params_t CLEAN={0u,0u,0u,64u};
static void expect(int ok,const char *why)
{
    ++checks; if (!ok) { fprintf(stderr,"FAIL: %s\n",why); exit(1); }
}
static uint32_t render(uint32_t blocks,const tk_params_t *p)
{
    uint32_t block,i,hash=2166136261u;
    int32_t pcm[514];
    for (block=0; block<blocks; ++block) {
        memset(pcm,0,sizeof pcm); pcm[0]=pcm[513]=0x12345678;
        tk_render(pcm+1,256u,p);
        expect(pcm[0]==0x12345678 && pcm[513]==0x12345678,"FM respects render buffer bounds");
        for (i=1; i<513u; ++i) {
            expect(pcm[i]>=-(int32_t)(TK_VOICES*TK_VOICE_PCM_BOUND) && pcm[i]<=(int32_t)(TK_VOICES*TK_VOICE_PCM_BOUND),"FM mix stays within the eight-voice output ceiling");
            hash=(hash^(uint32_t)pcm[i])*16777619u;
        }
    }
    return hash;
}
static tk_voice_t *voice(uint32_t note,uint32_t source)
{
    uint32_t i;
    for (i=0; i<TK_VOICES; ++i)
        if (tk_voice[i].audio_note==note && tk_voice[i].source==source && (tk_voice[i].env || tk_voice[i].gate))
            return &tk_voice[i];
    expect(0,"expected FM voice exists"); return &tk_voice[0];
}
static uint32_t sound(const tk_fm_patch_t *p)
{
    tk_init(); tk_set_instrument(TK_INSTRUMENT_FM); tk_set_live_fm_patch(p);
    tk_note_on(57u,127u,1u); return render(150u,&CLEAN);
}
static void patches_and_tables(void)
{
    uint32_t i,j,previous=0u;
    tk_fm_patch_t p;
    tk_fm_defaults(&p); expect(sizeof p==40u && tk_fm_valid(&p),"compact 40-byte FM patch validates");
    for (i=0; i<8u; ++i) {
        expect(TK_FM_ROUTING[i].carriers && TK_FM_ROUTING[i].carriers<64u,"every algorithm has real carriers");
        for (j=0; j<6u; ++j)
            expect(!(TK_FM_ROUTING[i].input[j]&((1u<<(j+1u))-1u)),"operator routing is acyclic and ordered");
    }
    for (i=0; i<4u; ++i) { tk_fm_factory(&p,i); expect(tk_fm_valid(&p),"every factory FM patch validates"); }
    tk_fm_defaults(&p); p.op[5].ratio=32u; expect(!tk_fm_valid(&p),"invalid ratio is rejected");
    tk_fm_defaults(&p); p.op[2].sustain=128u; expect(!tk_fm_valid(&p),"invalid envelope level is rejected");
    tk_fm_defaults(&p); p.algorithm=8u; expect(!tk_fm_valid(&p),"invalid routing is rejected");
    tk_fm_defaults(&p); p.pitch_depth=49u; expect(!tk_fm_valid(&p),"invalid pitch envelope is rejected");
    for (i=0; i<65536u; ++i) {
        int32_t sine=tk_fm_sine(i<<16);
        expect(sine>=-32760 && sine<=32760,"FM interpolated sine stays bounded");
        expect(sine==-tk_fm_sine((i<<16)+2147483648u),"FM sine is half-cycle antisymmetric");
    }
    expect(tk_fm_sine(0u)==0 && tk_fm_sine(1073741824u)==32760 &&
        tk_fm_sine(3221225472u)==-32760,"FM sine has expected cardinal phase values");
    for (i=0; i<=12288u; ++i) {
        uint32_t factor=tk_fm_pitch_factor((int32_t)i-6144);
        expect(factor>=previous && factor>=4194304u && factor<=67108864u,"pitch envelope is monotonic and bounded");
        previous=factor;
    }
}
static void patch_mailboxes(void)
{
    tk_fm_patch_t a,b;
    uint32_t generation;
    tk_init(); tk_fm_factory(&a,0u); tk_fm_factory(&b,1u);
    tk_set_live_fm_patch(&a); generation=tk_fm_mailbox[0].generation;
    tk_set_live_fm_patch(&a); expect(tk_fm_mailbox[0].generation==generation,"unchanged main publication has no revision churn");
    expect(tk_fm_read(0u) && !memcmp(&tk_fm_audio_patch[0],&a,sizeof a),"completed atomic-word mailbox copies exact patch");
    tk_fm_mailbox[0].generation|=1u; tk_fm_mailbox[0].word[0]=0u;
    expect(!tk_fm_read(0u) && !memcmp(&tk_fm_audio_patch[0],&a,sizeof a),"interrupted odd generation cannot tear audio patch");
    tk_fm_mailbox[0].generation=generation;
    tk_set_source_fm_patch(TK_SOURCE_ATMOS,&b); tk_fm_read(tk_fm_bank(TK_SOURCE_ATMOS));
    expect(!memcmp(&tk_fm_audio_patch[tk_fm_bank(TK_SOURCE_ATMOS)],&b,sizeof b),"captured atmosphere owns a separate FM bank");
    b.op[0].level=255u; generation=tk_fm_mailbox[tk_fm_bank(TK_SOURCE_ATMOS)].generation;
    tk_set_source_fm_patch(TK_SOURCE_ATMOS,&b);
    expect(tk_fm_mailbox[tk_fm_bank(TK_SOURCE_ATMOS)].generation==generation,"invalid patch cannot be published");
}
static void operators_and_algorithms(void)
{
    tk_fm_patch_t p,changed;
    uint32_t i,base,hash[8];
    tk_fm_defaults(&p); p.algorithm=6u;
    for (i=0; i<6u; ++i) { p.op[i].level=110u; p.op[i].ratio=(uint8_t)(i+1u); p.op[i].sustain=127u; }
    base=sound(&p);
    for (i=0; i<6u; ++i) { changed=p; changed.op[i].level=0u; expect(sound(&changed)!=base,"each of six real operators contributes to sound"); }
    for (i=0; i<8u; ++i) {
        uint32_t j;
        p.algorithm=(uint8_t)i; hash[i]=sound(&p);
        for (j=0; j<i; ++j) expect(hash[i]!=hash[j],"eight real routing graphs produce distinct PCM");
    }
    p.algorithm=0u; p.feedback=0u; base=sound(&p); p.feedback=127u;
    expect(sound(&p)!=base,"feedback changes real operator phase modulation");
    for (i=0; i<4u; ++i) { tk_fm_factory(&p,i); base=sound(&p); expect(sound(&p)==base,"factory FM sound is deterministic after reset"); }
}
static void envelope_and_controllers(void)
{
    tk_fm_patch_t p;
    tk_voice_t *v;
    uint32_t base,i;
    tk_fm_defaults(&p); p.op[0].sustain=127u; p.op[0].decay=0u; p.op[0].release=20u;
    tk_init(); tk_set_instrument(TK_INSTRUMENT_FM); tk_set_live_fm_patch(&p); tk_note_on(60u,127u,17u);
    render(8u,&CLEAN); v=voice(60u,17u); base=v->fm.increment[0];
    expect(v->fm.gain[0]>0 && v->fm.envelope[0]==TK_FM_FULL,"real carrier reaches sustain");
    tk_pitch_bend(17u,16383u); render(40u,&CLEAN);
    expect(v->fm.increment[0]>base,"external pitch bend reaches all FM operators");
    tk_modulation(17u,127u); tk_channel_pressure(17u,127u); tk_expression(17u,64u);
    render(40u,&CLEAN);
    expect(v->pressure_q8>0 && v->performance_gain_q15<32768,"FM retains external pressure and expression");
    tk_sustain(17u,127u); tk_note_off(60u,17u); render(100u,&CLEAN);
    expect(tk_note_is_held(60u,17u),"FM pedal extends real gate");
    tk_sustain(17u,0u); render(2400u,&CLEAN); expect(tk_quiet(),"all FM operator releases terminate and permit silence saves");
    p.pitch_depth=36u; p.pitch_time=64u;
    tk_init(); tk_set_instrument(TK_INSTRUMENT_FM); tk_set_live_fm_patch(&p); tk_note_on(60u,127u,1u);
    render(1u,&CLEAN); v=voice(60u,1u); expect(v->fm.increment[0]>v->base_inc,"PIT envelope starts above key pitch");
    render(520u,&CLEAN); expect(v->fm.pitch_q8==0 && v->fm.increment[0]==v->base_inc,"PIT envelope settles exactly to key pitch");
    tk_init(); tk_fm_factory(&p,1u); tk_set_instrument(TK_INSTRUMENT_FM); tk_set_live_fm_patch(&p);
    for (i=0; i<6u; ++i) tk_note_on(115u+i,127u,i);
    render(100u,&(tk_params_t){127u,127u,127u,127u});
    tk_panic(); render(1u,&CLEAN); expect(tk_quiet(),"FM panic terminates every oscillator within existing fade budget");
}
static void frozen_and_live(void)
{
    tk_fm_patch_t a,b;
    tk_voice_t *live,*track;
    uint32_t phase,bank;
    tk_init(); tk_fm_factory(&a,1u); tk_fm_factory(&b,3u);
    tk_set_instrument(TK_INSTRUMENT_FM); tk_set_live_fm_patch(&a);
    tk_set_source_instrument(TK_SOURCE_ATMOS,TK_INSTRUMENT_FM);
    tk_set_source_patch(TK_SOURCE_ATMOS,&CLEAN,64u,64u); tk_set_source_fm_patch(TK_SOURCE_ATMOS,&b);
    tk_note_on(60u,127u,1u); tk_note_on(48u,127u,TK_SOURCE_ATMOS); render(100u,&CLEAN);
    live=voice(60u,1u); track=voice(48u,TK_SOURCE_ATMOS); bank=track->fm.bank;
    expect(!memcmp(&track->fm.patch,&b,sizeof b),"captured track uses its six saved FM operators");
    phase=live->fm.phase[0]; a.op[0].ratio=4u; tk_set_live_fm_patch(&a); render(1u,&CLEAN);
    expect(live->fm.patch.op[0].ratio==4u && live->fm.phase[0]!=phase,"live operator edits preserve progressing phase");
    expect(track->fm.bank==bank && !memcmp(&track->fm.patch,&b,sizeof b),"live FM edits do not alter captured operators");
    tk_set_instrument(TK_INSTRUMENT_PAD); tk_fm_defaults(&a); tk_set_live_fm_patch(&a); render(2u,&CLEAN);
    expect(live->fm.patch.op[0].ratio==4u,"departing FM voice retains its old patch after switching engine");
}
static void qualified_voice_cap(void)
{
    uint32_t i,count=0u;
    tk_fm_patch_t p;
    tk_init(); tk_fm_factory(&p,1u); tk_set_instrument(TK_INSTRUMENT_FM); tk_set_live_fm_patch(&p);
    for (i=0; i<6u; ++i) expect(tk_note_on(60u+i,127u,1u),"fifth/sixth live FM note reuses eligible FM voice");
    for (i=0; i<TK_VOICES; ++i) if (tk_voice[i].command&TK_GATE) {
        ++count; expect((tk_voice[i].command&127u)>=62u,"FM cap keeps four newest chord notes");
    }
    expect(count==TK_FM_VOICE_LIMIT && tk_fm_voice_count()==TK_FM_VOICE_LIMIT,"four-FM cap leaves two of six slots available");
    tk_set_source_instrument(TK_SOURCE_DRUM,TK_INSTRUMENT_DRUM_KIT);
    tk_set_source_instrument(TK_SOURCE_SEQUENCE,TK_INSTRUMENT_PAD);
    expect(tk_note_on(36u,127u,TK_SOURCE_DRUM) && tk_note_on(48u,127u,TK_SOURCE_SEQUENCE),"legacy/drum can occupy remaining two slots");
    tk_set_source_instrument(TK_SOURCE_ATMOS,TK_INSTRUMENT_FM); tk_set_source_fm_patch(TK_SOURCE_ATMOS,&p);
    expect(!tk_note_on(72u,127u,TK_SOURCE_ATMOS),"generated FM cannot evict four held live FM owners at cap");
    expect(tk_note_is_held(36u,TK_SOURCE_DRUM) && tk_note_is_held(48u,TK_SOURCE_SEQUENCE),"FM cap never chooses unrelated engine to make fifth FM voice");
    render(1u,&CLEAN); tk_note_off(62u,1u);
    expect(tk_note_on(72u,127u,TK_SOURCE_ATMOS),"generated FM may reuse released live FM tail within cap");
    expect(tk_fm_voice_count()==TK_FM_VOICE_LIMIT,"FM tail reuse never consumes a fifth resident slot");
    expect(tk_note_on(77u,127u,17u),"new rear FM note may preempt generated FM at cap");
    expect(tk_note_is_held(77u,17u) && !tk_note_is_held(72u,TK_SOURCE_ATMOS),"live priority still beats generated FM under cap");
    expect(tk_note_is_held(63u,1u) && tk_note_is_held(64u,1u) && tk_note_is_held(65u,1u),"cap preserves other owners when generated victim is available");
    render(80u,&CLEAN);
}
static void captured_filter_lfo(void)
{
    tk_fm_patch_t p;
    tk_voice_t reference={0};
    uint32_t pass,unchanged=0u,modulated=0u;
    tk_fm_factory(&p,1u);
    for (pass=0; pass<2u; ++pass) {
        tk_voice_t *live,*track;
        tk_init(); tk_set_instrument(TK_INSTRUMENT_FM); tk_set_live_fm_patch(&p);
        tk_set_source_instrument(TK_SOURCE_ATMOS,TK_INSTRUMENT_FM);
        tk_set_source_patch(TK_SOURCE_ATMOS,&CLEAN,64u,64u); tk_set_source_fm_patch(TK_SOURCE_ATMOS,&p);
        tk_note_on(60u,127u,1u); tk_note_on(48u,127u,TK_SOURCE_ATMOS); render(100u,&CLEAN);
        if (pass) tk_set_source_shade(TK_SOURCE_ATMOS,120u);
        if (pass) modulated=render(40u,&CLEAN); else unchanged=render(40u,&CLEAN);
        live=voice(60u,1u); track=voice(48u,TK_SOURCE_ATMOS);
        if (!pass) reference=*live;
        else {
            expect(live->filter_l==reference.filter_l && live->filter_r==reference.filter_r &&
                !memcmp(live->fm.phase,reference.fm.phase,sizeof live->fm.phase) &&
                !memcmp(live->fm.envelope,reference.fm.envelope,sizeof live->fm.envelope),
                "captured FM SHADE LFO leaves separate live voice exactly unchanged");
            expect(track->color_shade>30000 && track->color_alpha<1000,
                "captured FM SHADE publication smoothly updates real postfilter");
        }
        expect(!memcmp(&track->fm.patch,&p,sizeof p),"captured filter motion never rewrites six FM operators");
    }
    expect(modulated!=unchanged,"captured FM filter LFO changes actual rendered PCM");
}
int main(void)
{
    patches_and_tables(); patch_mailboxes(); operators_and_algorithms(); envelope_and_controllers(); frozen_and_live(); qualified_voice_cap(); captured_filter_lfo();
    printf("PASS: %u FM checks; six operators, eight routings, ADSR/PIT and expressive owners\n",checks); return 0;
}
