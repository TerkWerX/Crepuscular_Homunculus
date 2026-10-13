/* SPDX-License-Identifier: GPL-3.0-only */
/* PCM references captured before the routing-cache/gain-idle optimization.
 * These event trajectories check sound compatibility independently of the
 * implementation's cached routing lists and envelope interpolation state. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "terkinstein_engine.h"
static const tk_params_t MACROS={52u,30u,15u,75u};
static uint32_t hash,checks;
static const uint32_t FACTORY[4]={0x1F8A6A5Du,0x12A70FD1u,0xA1EDA6D7u,0x07D1027Bu};
static const uint32_t ROUTE[4][8]={
    {0x152BED3Fu,0x1F8A6A5Du,0xB6A95FF5u,0x10B718ADu,0xB8E901C9u,0x192A02FBu,0xC90A3017u,0xEBFF8503u},
    {0xEAA3842Du,0x83A851D9u,0xBF092ADFu,0xC5328923u,0x0E43EC9Bu,0xFDB065E5u,0xEFEAC9B9u,0x12A70FD1u},
    {0x00F85279u,0xB25DC0EBu,0xA1EDA6D7u,0x342311CBu,0xF72C3D55u,0xD64D50DBu,0x1711A3FFu,0x53AE987Fu},
    {0xFC54B8CBu,0x98F8CE51u,0x6D48B1B5u,0xF9D4016Fu,0x1B9EEFFBu,0x07D1027Bu,0xC2CB5C65u,0x6743F7C7u}
};
static const uint32_t HELD[4]={0xBDFB8981u,0x3BBC0437u,0xE6F4D9DFu,0x1BF8AC89u};
static void expect(uint32_t expected,uint32_t factory,uint32_t route,const char *scenario)
{
    ++checks;
    if(hash!=expected) {
        fprintf(stderr,"FAIL: %s factory %u route %u PCM %08X expected %08X\n",scenario,factory,route,hash,expected);
        exit(1);
    }
}
static void run(uint32_t blocks)
{
    int32_t pcm[512]; uint32_t i,j;
    for(i=0;i<blocks;++i) {
        memset(pcm,0,sizeof pcm); tk_render(pcm,256u,&MACROS);
        for(j=0;j<512u;++j) hash=(hash^(uint32_t)pcm[j])*16777619u;
    }
}
static void start(tk_fm_patch_t *patch,uint32_t factory,uint32_t algorithm)
{
    tk_init(); hash=2166136261u; tk_fm_factory(patch,factory);
    if(algorithm<8u) patch->algorithm=(uint8_t)algorithm;
    tk_set_instrument(TK_INSTRUMENT_FM); tk_set_live_fm_patch(patch);
    tk_note_on(48u,127u,1u); tk_note_on(55u,110u,1u); tk_note_on(60u,100u,1u); tk_note_on(64u,90u,1u);
}
int main(void)
{
    uint32_t factory,algorithm; tk_fm_patch_t patch;
    for(factory=0;factory<4u;++factory) {
        start(&patch,factory,99u); run(600u); expect(FACTORY[factory],factory,99u,"factory chord");
        for(algorithm=0;algorithm<8u;++algorithm) {
            start(&patch,factory,algorithm); run(600u);
            expect(ROUTE[factory][algorithm],factory,algorithm,"routing chord");
        }
        start(&patch,factory,99u); run(90u);
        tk_sustain(1u,127u); tk_note_off(48u,1u); tk_note_off(55u,1u); run(40u);
        tk_sostenuto(1u,127u); tk_note_on(67u,127u,17u); run(90u);
        tk_modulation(1u,107u); tk_channel_pressure(17u,91u); tk_poly_pressure(60u,1u,97u);
        tk_pitch_bend(1u,15000u); tk_expression(17u,85u); run(80u);
        patch.algorithm=(uint8_t)((patch.algorithm+3u)%8u); patch.feedback=127u;
        patch.op[0].ratio=3u; patch.op[1].level=97u; patch.op[3].sustain=43u;
        tk_set_live_fm_patch(&patch); run(50u);
        tk_note_off(60u,1u); tk_note_off(64u,1u); run(50u);
        tk_sustain(1u,0u); tk_sostenuto(1u,0u); tk_note_off(67u,17u); run(1100u);
        expect(HELD[factory],factory,99u,"held edit/controller/pedal trajectory");
    }
    printf("PASS: %u FM PCM references unchanged across factories, routings and held edits/pedals\n",checks);
    return 0;
}
