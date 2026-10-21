/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "terkinstein_engine.h"
static uint32_t checks;
static const tk_params_t NORMAL={38u,46u,0u,76u};
static void expect(int ok,const char *why)
{
    ++checks; if (!ok) { fprintf(stderr,"FAIL: %s\n",why); exit(1); }
}
static uint32_t held(uint32_t source)
{
    uint32_t i,n=0u;
    for (i=0; i<TK_VOICES; ++i)
        if ((tk_voice[i].command&TK_GATE) && ((tk_voice[i].command>>14)&63u)==source) ++n;
    return n;
}
static int winner(uint32_t note,uint32_t source)
{
    return held(source)==1u && tk_note_is_held(note,source);
}
static void render(uint32_t count)
{
    int32_t pcm[512];
    uint32_t i,j;
    for (i=0; i<count; ++i) {
        memset(pcm,0,sizeof pcm); tk_render(pcm,256u,&NORMAL);
        for (j=0; j<512u; ++j) expect(pcm[j]>=-(int32_t)(TK_VOICES*TK_VOICE_PCM_BOUND) && pcm[j]<=(int32_t)(TK_VOICES*TK_VOICE_PCM_BOUND),"MONO retains bounded eight-voice output headroom");
    }
}
static void basic_priority(void)
{
    uint32_t i,revision=0u;
    tk_init(); expect(!tk_mono,"factory starts POLY"); tk_set_mono(1u);
    tk_note_on(60u,100u,0u); tk_note_on(64u,110u,0u); tk_note_on(67u,120u,0u);
    expect(winner(67u,0u),"MONO plays last key with one owner gate");
    tk_note_off(64u,0u); expect(winner(67u,0u),"release of suppressed key leaves latest winner");
    tk_note_off(67u,0u); expect(winner(60u,0u),"release of latest key returns to prior held key");
    for (i=0; i<TK_VOICES; ++i) if (tk_voice[i].command&TK_GATE) revision=tk_voice[i].command;
    tk_note_on(60u,127u,0u);
    for (i=0; i<TK_VOICES; ++i) if (tk_voice[i].command&TK_GATE)
        expect(tk_voice[i].command!=revision && ((tk_voice[i].command>>7)&127u)==127u,"same note retriggers and updates velocity");
    tk_note_on(60u,0u,0u); expect(!held(0u),"zero velocity releases mono winner");
    render(400u); expect(tk_quiet(),"mono note release eventually becomes silent");
    tk_init(); tk_set_mono(1u);
    for (i=0; i<128u; ++i) {
        tk_note_on(i,127u,16u);
        expect(winner(i,16u) && tk_mono_state[16].count==i+1u,"all 128 physical MIDI notes fit unique last-note stack");
    }
    for (i=128u; i-- >0u;) {
        tk_note_off(i,16u);
        expect(i?winner(i-1u,16u):!held(16u),"full last-note stack falls back without losing old held keys");
    }
}
static void owners_and_tracks(void)
{
    tk_init(); tk_set_mono(1u);
    tk_note_on(60u,100u,1u); tk_note_on(64u,100u,1u);
    tk_note_on(60u,100u,17u); tk_note_on(67u,100u,17u);
    tk_note_on(48u,100u,TK_SOURCE_ATMOS); tk_note_on(52u,100u,TK_SOURCE_ATMOS);
    expect(winner(64u,1u) && winner(67u,17u),"USB and rear channel1 have separate mono winners");
    expect(held(TK_SOURCE_ATMOS)==2u,"captured atmosphere chords remain polyphonic under live MONO");
    tk_note_off(64u,1u); expect(winner(60u,1u) && winner(67u,17u),"one port fallback leaves other port unchanged");
    tk_note_off(67u,17u); expect(winner(60u,17u),"rear source has its own prior-key fallback");
    tk_note_off(60u,1u); expect(!held(1u) && winner(60u,17u),"same pitch on another input survives note release");
    render(50u);
}
static void sustain_and_sostenuto(void)
{
    tk_init(); tk_set_mono(1u);
    tk_note_on(60u,100u,1u); tk_sustain(1u,127u); tk_note_off(60u,1u);
    expect(winner(60u,1u),"sustain holds released mono key");
    tk_note_on(64u,100u,1u); expect(winner(64u,1u),"new physical key overrides pedal-held winner");
    tk_note_off(64u,1u); expect(winner(64u,1u),"latest sustain-held note remains winner");
    tk_sustain(1u,0u); expect(!held(1u),"pedal lift clears every released key and mono gate");
    tk_note_on(60u,100u,1u); tk_note_on(64u,100u,1u); tk_sustain(1u,127u);
    tk_note_off(64u,1u); expect(winner(60u,1u),"older physical key wins over latest pedal-only key");
    tk_note_off(60u,1u); expect(winner(64u,1u),"last pressed pedal-owned note returns when physical keys release");
    tk_sustain(1u,0u);
    tk_note_on(60u,100u,17u); tk_note_on(64u,100u,17u); tk_sostenuto(17u,127u);
    expect(!(tk_mono_state[17].flags[60]&TK_MONO_SOSTENUTO) &&
        (tk_mono_state[17].flags[64]&TK_MONO_SOSTENUTO),"sostenuto captures sounding winner only");
    tk_note_off(64u,17u); expect(winner(60u,17u),"held prior key can override sostenuto winner");
    tk_note_off(60u,17u); expect(winner(64u,17u),"sostenuto returns captured winner after physical keys release");
    tk_note_on(67u,100u,17u); tk_note_off(67u,17u);
    expect(winner(64u,17u),"new key pressed after sostenuto is not captured");
    tk_sustain(17u,127u); tk_sostenuto(17u,0u);
    expect(winner(64u,17u),"sustain can continue a sounding sostenuto-held note");
    tk_sustain(17u,0u); expect(!held(17u),"lifting both pedals releases captured note");
    tk_note_on(72u,100u,17u); tk_sustain(17u,127u); tk_sostenuto(17u,127u); tk_note_off(72u,17u);
    tk_sostenuto(17u,0u); expect(winner(72u,17u),"sustain survives sostenuto lift when both captured key");
    tk_sustain(17u,0u); expect(!held(17u),"last owning pedal releases mono key");
}
static void mode_switch_and_resets(void)
{
    uint32_t i;
    tk_init(); tk_note_on(60u,100u,0u); tk_note_on(64u,100u,0u); tk_note_on(67u,100u,0u);
    expect(held(0u)==3u,"ordinary POLY starts chord");
    tk_set_mono(1u); expect(winner(67u,0u),"switching to MONO keeps last physical key");
    tk_set_mono(0u); expect(held(0u)==3u,"switching to POLY restores held chord");
    tk_set_mono(1u); tk_sustain(0u,127u); tk_all_notes_off(0u);
    expect(winner(67u,0u),"CC123 respects pedal in MONO");
    tk_all_sound_off(0u); expect(!held(0u) && !tk_mono_state[0].count,"CC120 clears hidden held-key stack");
    tk_sustain(0u,0u); tk_set_mono(0u); tk_set_mono(1u);
    expect(!held(0u),"mode or pedal changes cannot revive a killed mono gesture");
    tk_note_on(60u,100u,1u); tk_note_on(64u,100u,1u); tk_sustain(1u,127u); tk_note_off(64u,1u);
    tk_reset_controllers(1u); expect(winner(60u,1u),"CC121 keeps physical fallback and clears pedal-only keys");
    tk_note_on(67u,100u,17u); tk_all_sound_off(1u);
    expect(winner(67u,17u),"input loss clears only affected owner's mono notes");
    tk_panic(); expect(tk_mono,"panic preserves selected MONO mode"); render(1u);
    expect(tk_quiet(),"panic silences all mono owners and generated tails");
    for (i=0; i<TK_SOURCE_SEQUENCE; ++i) expect(!tk_mono_state[i].count,"panic clears every prior-key stack");
    tk_set_mono(0u); expect(!held(17u),"POLY cannot revive pre-panic held keys");
    tk_note_on(60u,100u,1u); tk_set_mono(1u); tk_note_off(60u,1u); expect(!held(1u),"new gesture works after panic");
}
int main(void)
{
    basic_priority(); owners_and_tracks(); sustain_and_sostenuto(); mode_switch_and_resets();
    printf("PASS: %u MONO checks; owner priority/fallback, mode switches, pedals and panic\n",checks); return 0;
}
