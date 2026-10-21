/* SPDX-License-Identifier: GPL-3.0-only */
/* Captured track macro following on the actual portable engine. These offline
 * checks do not qualify Fire hardware or the target audio interrupt budget. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "terkinstein_engine.h"

static const tk_params_t LIVE = {38u,46u,34u,76u};
static const tk_params_t CAPTURED = {64u,64u,64u,64u};
static uint32_t checks;
static void expect(int good, const char *message)
{ checks++;if(!good){fprintf(stderr,"FAIL: %s\n",message);exit(1);} }
static void block(int32_t *out)
{
    int32_t samples[514];uint32_t i;
    samples[0]=samples[513]=0x13579BDF;memset(samples+1,0,512u*sizeof(int32_t));
    tk_render(samples+1,256u,&LIVE);
    expect(samples[0]==0x13579BDF&&samples[513]==0x13579BDF,"track edits preserve render bounds");
    for(i=1;i<513;i++)expect(samples[i]>=-(int32_t)(TK_VOICES*TK_VOICE_PCM_BOUND)&&samples[i]<=(int32_t)(TK_VOICES*TK_VOICE_PCM_BOUND),"track edits preserve bounded eight-voice output headroom");
    if(out)memcpy(out,samples+1,512u*sizeof(int32_t));
}
static void setup(uint32_t instrument)
{
    uint32_t i;tk_fm_patch_t fm;
    tk_init();tk_set_instrument(TK_INSTRUMENT_PAD);
    tk_set_source_instrument(TK_SOURCE_ATMOS,instrument);
    tk_set_source_instrument(TK_SOURCE_SEQUENCE,TK_INSTRUMENT_PAD);
    tk_set_source_patch(TK_SOURCE_ATMOS,&CAPTURED,64u,64u);
    tk_set_source_patch(TK_SOURCE_SEQUENCE,&CAPTURED,64u,64u);
    tk_fm_factory(&fm,0u);tk_set_source_fm_patch(TK_SOURCE_ATMOS,&fm);
    for(i=0;i<64;i++)block(NULL);
}
static tk_voice_t *voice(uint32_t source)
{
    uint32_t i;
    for(i=0;i<TK_VOICES;i++)if(tk_voice[i].source==source&&tk_voice[i].gate)return &tk_voice[i];
    expect(0,"fixture has sounding source-owned voice");return NULL;
}
static void continuity_and_isolation(void)
{
    static const tk_params_t changed={7u,115u,121u,9u};
    uint32_t mode,i,phase_a,phase_b,command,other_command,live_command,fm_phase[6];
    int32_t other_shade,other_fracture;
    tk_voice_t *edited,*other,*live;
    for(mode=0;mode<=TK_INSTRUMENT_FM;mode++) {
        setup(mode);
        expect(tk_note_on(70u,120u,TK_SOURCE_ATMOS),"target track starts held voice");
        expect(tk_note_on(48u,100u,TK_SOURCE_SEQUENCE),"other captured track starts independent voice");
        expect(tk_note_on(60u,100u,0),"live keyboard remains independent");
        block(NULL);edited=voice(TK_SOURCE_ATMOS);other=voice(TK_SOURCE_SEQUENCE);live=voice(0);
        phase_a=edited->phase_a;phase_b=edited->phase_b;command=edited->command;
        other_command=other->command;live_command=live->command;
        other_shade=other->color_shade;other_fracture=other->color_fracture;
        memcpy(fm_phase,edited->fm.phase,sizeof fm_phase);
        tk_set_source_patch(TK_SOURCE_ATMOS,&changed,64u,64u);
        tk_follow_source_macros(TK_SOURCE_ATMOS);
        tk_set_source_shade(TK_SOURCE_ATMOS,7u);
        for(i=0;i<128;i++)tk_control(&LIVE);
        expect(edited->command==command&&edited->gate&&edited->key_down&&!edited->pending&&!edited->fading,"held track macro edits preserve note gate and event revision");
        expect(edited->phase_a==phase_a&&edited->phase_b==phase_b&&!memcmp(fm_phase,edited->fm.phase,sizeof fm_phase),"macro control changes never reset oscillator phases");
        expect(edited->color_shade==7*256&&edited->color_fracture==115*256&&edited->patch_drift==121&&edited->patch_bloom==9,"all four held captured macros reach the complete atomic source target");
        expect(other->command==other_command&&other->color_shade==other_shade&&other->color_fracture==other_fracture&&other->patch_drift==64&&other->patch_bloom==64,"another captured track keeps its own complete macro patch");
        expect(live->command==live_command&&tk_macro[0]==38*256&&tk_macro[1]==46*256&&tk_macro[2]==34*256&&tk_macro[3]==76*256,"track edits leave live patch and note ownership unchanged");
        expect(edited->patch_attack==((72u+(128u-9u)*10u)*tk_envelope_rate(64u))>>8,"held BLOOM updates attack scaling without envelope restart");
        expect(edited->patch_release==((20u+(127u-9u)*3u)*tk_envelope_rate(64u))>>8,"held BLOOM updates release scaling");
        for(i=0;i<6;i++)block(NULL);
    }
}
static uint32_t macro_audio(uint32_t instrument,uint32_t macro,uint32_t edit)
{
    tk_params_t changed=CAPTURED;
    int32_t samples[512];uint32_t i,j,hash=2166136261u;
    setup(instrument);expect(tk_note_on(70u,120u,TK_SOURCE_ATMOS),"audio fixture starts captured note");
    for(i=0;i<64;i++) {
        if(i==4u&&edit) {
            if(!macro)changed.shade=0;
            else if(macro==1u)changed.fracture=127;
            else if(macro==2u)changed.drift=127;
            else changed.bloom=0;
            tk_set_source_patch(TK_SOURCE_ATMOS,&changed,64u,64u);
            tk_follow_source_macros(TK_SOURCE_ATMOS);
        }
        /* METAL's independent ATTACK can already be at full sustain when
         * BLOOM changes; exercise its updated release coefficient too. */
        if(i==32u&&instrument==TK_INSTRUMENT_METAL)tk_note_off(70u,TK_SOURCE_ATMOS);
        block(samples);
        for(j=0;j<512;j++){hash^=(uint32_t)samples[j];hash*=16777619u;}
    }
    return hash;
}
static void audible_macros(void)
{
    uint32_t mode,macro;
    for(mode=0;mode<=TK_INSTRUMENT_FM;mode++)for(macro=0;macro<4;macro++) {
        uint32_t still=macro_audio(mode,macro,0),changed=macro_audio(mode,macro,1);
        if(still==changed)fprintf(stderr,"unchanged held audio: instrument %u, macro %u\n",mode,macro);
        expect(still!=changed,"each captured macro changes sounding or release audio in every instrument mode");
    }
}
static void guarded_mailboxes(void)
{
    uint32_t i;
    tk_init();tk_follow_source_macros(0);tk_follow_source_macros(TK_SOURCES);
    for(i=0;i<TK_SOURCES;i++)expect(!tk_source_macro_control[i],"invalid/live sources cannot enable captured macro following");
    tk_set_source_patch(TK_SOURCE_ATMOS,&CAPTURED,64u,64u);tk_follow_source_macros(TK_SOURCE_ATMOS);
    expect(tk_source_macro_control[TK_SOURCE_ATMOS]==0xC0404040u,"follow mailbox publishes valid bit and all four macros atomically");
    tk_clear_source_patch(TK_SOURCE_ATMOS);
    expect(!tk_source_macro_control[TK_SOURCE_ATMOS],"clearing captured patch clears its macro override");
}
int main(void)
{
    continuity_and_isolation();audible_macros();guarded_mailboxes();
    printf("PASS captured track macros: %u checks\n",checks);return 0;
}
