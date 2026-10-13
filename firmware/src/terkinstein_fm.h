/* SPDX-License-Identifier: GPL-3.0-only */
/* TERKINSTEIN's original six-operator phase-modulation engine. This is not a
 * DX7 implementation. Operator 1 is op[0]; higher numbered operators feed
 * lower numbered ones. Eight bounded acyclic routings share the existing
 * six voice slots. All control, phase, envelope and sample arithmetic is
 * integer; main only publishes guarded atomic-word patch mailboxes. */
#ifndef TERKINSTEIN_FM_H
#define TERKINSTEIN_FM_H
#include <stdint.h>

#define TK_FM_OPERATORS 6u
#define TK_FM_ALGORITHMS 8u
#define TK_FM_BANKS 6u
#define TK_FM_FULL 8388608u
typedef struct { uint8_t ratio, level, attack, decay, sustain, release; } tk_fm_operator_t;
typedef struct {
    tk_fm_operator_t op[TK_FM_OPERATORS];
    uint8_t algorithm, feedback, pitch_depth, pitch_time;
} tk_fm_patch_t;
typedef char tk_fm_patch_size[(sizeof(tk_fm_patch_t) == 40u) ? 1 : -1];
typedef struct {
    uint8_t input[TK_FM_OPERATORS], carriers;
} tk_fm_algorithm_t;
/* CHAIN, TWIN CHAIN, THREE PAIRS, CROWN, BRANCH, FORK, PARALLEL, WEB. */
static const tk_fm_algorithm_t TK_FM_ROUTING[TK_FM_ALGORITHMS] = {
    {{2u,4u,8u,16u,32u,0u},1u},
    {{2u,4u,0u,16u,32u,0u},9u},
    {{2u,0u,8u,0u,32u,0u},21u},
    {{62u,0u,0u,0u,0u,0u},1u},
    {{6u,8u,16u,0u,32u,0u},1u},
    {{4u,4u,32u,16u,32u,0u},11u},
    {{0u,0u,0u,0u,0u,0u},63u},
    {{10u,20u,32u,32u,32u,0u},1u}
};
static const char *const TK_FM_ALGORITHM_NAMES[TK_FM_ALGORITHMS] = {
    "CHAIN","TWIN CHAIN","THREE PAIRS","CROWN","BRANCH","FORK","PARALLEL","WEB"
};
typedef struct {
    volatile uint32_t generation, word[10];
} tk_fm_mailbox_t;
static tk_fm_mailbox_t tk_fm_mailbox[TK_FM_BANKS]; /* main writer */
static tk_fm_patch_t tk_fm_audio_patch[TK_FM_BANKS];
static uint32_t tk_fm_seen[TK_FM_BANKS]; /* audio writer */
typedef struct {
    tk_fm_patch_t patch;
    uint32_t phase[TK_FM_OPERATORS], increment[TK_FM_OPERATORS];
    uint32_t envelope[TK_FM_OPERATORS];
    int32_t gain[TK_FM_OPERATORS], target[TK_FM_OPERATORS], delta[TK_FM_OPERATORS];
    uint8_t stage[TK_FM_OPERATORS];
    uint8_t input_count[TK_FM_OPERATORS], input[TK_FM_OPERATORS][5];
    uint8_t carrier[TK_FM_OPERATORS], carrier_count, routing_seen;
    int32_t carrier_gain;
    uint32_t bank, remaining, previous_gate;
    int32_t feedback_sample, pitch_q8, pitch_step;
} tk_fm_voice_t;

static void tk_fm_defaults(tk_fm_patch_t *p)
{
    uint32_t i;
    if (!p) return;
    for (i=0; i<TK_FM_OPERATORS; ++i) {
        p->op[i].ratio = 1u;
        p->op[i].level = i ? 0u : 110u;
        p->op[i].attack = 0u; p->op[i].decay = 40u;
        p->op[i].sustain = 100u; p->op[i].release = 36u;
    }
    p->algorithm=0u; p->feedback=0u; p->pitch_depth=24u; p->pitch_time=32u;
}
static int tk_fm_valid(const tk_fm_patch_t *p)
{
    uint32_t i;
    if (!p || p->algorithm>=TK_FM_ALGORITHMS || p->feedback>127u ||
        p->pitch_depth>48u || p->pitch_time>127u) return 0;
    for (i=0; i<TK_FM_OPERATORS; ++i) {
        const tk_fm_operator_t *o=&p->op[i];
        if (o->ratio>31u || o->level>127u || o->attack>127u ||
            o->decay>127u || o->sustain>127u || o->release>127u) return 0;
    }
    return 1;
}
/* Four deliberately different playable FM starting points. Metadata names
 * are supplied by the app; all six operators are real oscillators. */
static void tk_fm_factory(tk_fm_patch_t *p, uint32_t index)
{
    static const uint8_t ratios[4][6]={{1,2,3,1,5,2},{1,3,7,2,5,11},{1,2,4,3,8,13},{0,1,2,3,5,7}};
    static const uint8_t levels[4][6]={{108,72,38,85,46,27},{116,112,95,74,82,63},{119,95,110,87,64,80},{99,71,54,94,45,32}};
    static const uint8_t algorithms[4]={1u,7u,2u,5u};
    uint32_t i;
    if (!p) return;
    index%=4u; tk_fm_defaults(p);
    p->algorithm=algorithms[index]; p->feedback=(uint8_t)(index==1u?82u:index==2u?45u:12u);
    for (i=0; i<6u; ++i) {
        p->op[i].ratio=ratios[index][i]; p->op[i].level=levels[index][i];
        p->op[i].attack=(uint8_t)(index==0u?55u+i*5u:index==3u?78u-i*5u:0u);
        p->op[i].decay=(uint8_t)(index==2u?24u+i*3u:58u+i*5u);
        p->op[i].sustain=(uint8_t)(index==2u?12u:index==1u?74u:98u-i*6u);
        p->op[i].release=(uint8_t)(index==2u?23u:index==1u?40u:83u-i*4u);
    }
    p->pitch_depth=(uint8_t)(index==1u?29u:index==2u?31u:24u);
    p->pitch_time=(uint8_t)(index==2u?18u:42u);
}
static uint32_t tk_fm_bank(uint32_t source)
{
    if (source==TK_SOURCE_LIVE_DRUM) return TK_SOURCE_DRUM-32u;
    return source>=33u && source<=37u ? source-32u : 0u;
}
static void tk_fm_publish(uint32_t bank, const tk_fm_patch_t *p)
{
    const uint8_t *bytes=(const uint8_t *)p;
    uint32_t i, generation, words[10], changed=0u;
    if (bank>=TK_FM_BANKS || !tk_fm_valid(p)) return;
    for (i=0; i<10u; ++i) {
        words[i]=(uint32_t)bytes[i*4u] | ((uint32_t)bytes[i*4u+1u]<<8)
            | ((uint32_t)bytes[i*4u+2u]<<16) | ((uint32_t)bytes[i*4u+3u]<<24);
        if (words[i]!=tk_fm_mailbox[bank].word[i]) changed=1u;
    }
    generation=tk_fm_mailbox[bank].generation;
    if (!changed && generation) return;
    tk_fm_mailbox[bank].generation=generation+1u; /* odd: interrupted edit is unreadable */
    for (i=0; i<10u; ++i)
        tk_fm_mailbox[bank].word[i]=words[i];
    tk_fm_mailbox[bank].generation=generation+2u;
}
static void tk_set_live_fm_patch(const tk_fm_patch_t *p) { tk_fm_publish(0u,p); }
static void tk_set_source_fm_patch(uint32_t source, const tk_fm_patch_t *p)
{
    if (source>=33u && source<=37u) tk_fm_publish(tk_fm_bank(source),p);
}
static int tk_fm_read(uint32_t bank)
{
    uint32_t i, generation=tk_fm_mailbox[bank].generation;
    tk_fm_patch_t next;
    uint8_t *bytes=(uint8_t *)&next;
    if ((generation&1u) || generation==tk_fm_seen[bank]) return 0;
    for (i=0; i<10u; ++i) {
        uint32_t word=tk_fm_mailbox[bank].word[i];
        bytes[i*4u]=(uint8_t)word; bytes[i*4u+1u]=(uint8_t)(word>>8);
        bytes[i*4u+2u]=(uint8_t)(word>>16); bytes[i*4u+3u]=(uint8_t)(word>>24);
    }
    if (generation!=tk_fm_mailbox[bank].generation || !tk_fm_valid(&next)) return 0;
    tk_fm_audio_patch[bank]=next; tk_fm_seen[bank]=generation; return 1;
}
static void tk_fm_init(void)
{
    uint32_t i;
    tk_fm_patch_t initial;
    tk_fm_defaults(&initial);
    for (i=0; i<TK_FM_BANKS; ++i) {
        tk_fm_mailbox[i].generation=0u; tk_fm_seen[i]=0u;
        tk_fm_audio_patch[i]=initial; tk_fm_publish(i,&initial); tk_fm_read(i);
    }
}

/* Generated integer tables follow: a quarter sine, linear-envelope step per
 * 32-frame tick, and semitone pitch factors. No floating point is used by firmware. */
#include "terkinstein_fm_tables.h"
static int32_t tk_fm_sine(uint32_t phase)
{
    uint32_t quadrant=phase>>30, position=phase&1073741823u;
    uint32_t fraction, index;
    int32_t a,b,value;
    if (quadrant&1u) position=1073741824u-position;
    index=position>>22;
    if (index==256u) return quadrant>=2u ? -32760 : 32760;
    fraction=(position>>10)&4095u;
    a=tk_fm_sine_quarter[index]; b=tk_fm_sine_quarter[index+1u];
    value=a+(((b-a)*(int32_t)fraction)>>12);
    return quadrant>=2u ? -value : value;
}
static uint32_t tk_fm_pitch_factor(int32_t pitch_q8)
{
    uint32_t position=(uint32_t)(pitch_q8+6144), index=position>>8, fraction=position&255u;
    if (index>=48u) return tk_fm_pitch_factors[48];
    return tk_fm_pitch_factors[index]+
        (((tk_fm_pitch_factors[index+1u]-tk_fm_pitch_factors[index])*fraction)>>8);
}
/* Expand the canonical graph only when its algorithm changes. The sample
 * path visits actual edges and carriers, rather than testing every possible
 * edge six times per frame. The UI still reads the same canonical bitmasks. */
static void tk_fm_cache_routing(tk_fm_voice_t *s)
{
    static const uint16_t reciprocal[7]={0u,32768u,16384u,10922u,8192u,6553u,5461u};
    const tk_fm_algorithm_t *route;
    uint32_t i,j;
    if (s->routing_seen==s->patch.algorithm) return;
    route=&TK_FM_ROUTING[s->patch.algorithm];
    s->carrier_count=0u;
    for (i=0; i<6u; ++i) {
        s->input_count[i]=0u;
        for (j=i+1u; j<6u; ++j) if (route->input[i]&(1u<<j))
            s->input[i][s->input_count[i]++]=(uint8_t)j;
        if (route->carriers&(1u<<i)) s->carrier[s->carrier_count++]=(uint8_t)i;
    }
    s->carrier_gain=reciprocal[s->carrier_count];
    s->routing_seen=s->patch.algorithm;
}
static void tk_fm_start(tk_fm_voice_t *s, uint32_t source)
{
    uint32_t i, ticks;
    s->bank=tk_fm_bank(source); tk_fm_read(s->bank);
    s->patch=tk_fm_audio_patch[s->bank];
    s->routing_seen=255u; tk_fm_cache_routing(s);
    for (i=0; i<6u; ++i) {
        s->phase[i]=s->increment[i]=s->envelope[i]=0u;
        s->gain[i]=s->target[i]=s->delta[i]=0; s->stage[i]=1u;
    }
    s->remaining=0u; s->previous_gate=1u; s->feedback_sample=0;
    s->pitch_q8=((int32_t)s->patch.pitch_depth-24)*256;
    ticks=2u+(uint32_t)s->patch.pitch_time*s->patch.pitch_time;
    s->pitch_step=(s->pitch_q8<0 ? -s->pitch_q8 : s->pitch_q8)+(int32_t)ticks-1;
    s->pitch_step/=(int32_t)ticks;
}
static uint32_t tk_fm_tick(tk_fm_voice_t *s, uint32_t gate, uint32_t base,
                            uint32_t refresh, uint32_t attack_scale, uint32_t decay_scale,
                            uint32_t release_scale, uint32_t drift)
{
    uint32_t i, active=0u, moving=0u;
    if (refresh) s->patch=tk_fm_audio_patch[s->bank];
    tk_fm_cache_routing(s);
    if (!gate && s->previous_gate)
        for (i=0; i<6u; ++i) if (s->stage[i]) s->stage[i]=4u;
    s->previous_gate=gate;
    if (s->pitch_q8>0) {
        s->pitch_q8-=s->pitch_step; if (s->pitch_q8<0) s->pitch_q8=0;
    } else if (s->pitch_q8<0) {
        s->pitch_q8+=s->pitch_step; if (s->pitch_q8>0) s->pitch_q8=0;
    }
    if (s->pitch_q8) {
        uint64_t scaled=((uint64_t)base*tk_fm_pitch_factor(s->pitch_q8))>>24;
        base=scaled>2147483647u ? 2147483647u : (uint32_t)scaled;
    }
    for (i=0; i<6u; ++i) {
        const tk_fm_operator_t *o=&s->patch.op[i];
        uint32_t env=s->envelope[i], step, sustain=o->sustain==127u?TK_FM_FULL:(uint32_t)o->sustain<<16;
        uint64_t inc=o->ratio ? (uint64_t)base*o->ratio : base>>1;
        if (i && drift) {
            uint32_t detune=(base>>16)*drift*(i+1u);
            inc=(i&1u)?inc+detune:inc-detune;
        }
        s->increment[i]=inc>2147483647u?2147483647u:(uint32_t)inc;
        if (s->stage[i]==1u) {
            step=(tk_fm_envelope_steps[o->attack]*attack_scale)>>8;
            if (!o->attack || step>=TK_FM_FULL-env) { env=TK_FM_FULL; s->stage[i]=2u; }
            else env+=step;
        } else if (s->stage[i]==2u) {
            step=(tk_fm_envelope_steps[o->decay]*decay_scale)>>8;
            if (env<=sustain || step>=env-sustain) { env=sustain; s->stage[i]=3u; }
            else env-=step;
        } else if (s->stage[i]==3u) env=sustain;
        else if (s->stage[i]==4u) {
            step=(tk_fm_envelope_steps[o->release]*release_scale)>>8;
            if (!o->release || step>=env) { env=0u; s->stage[i]=0u; }
            else env-=step;
        }
        s->envelope[i]=env;
        s->target[i]=(int32_t)((env*(uint32_t)o->level)>>15);
        s->delta[i]=(s->target[i]-s->gain[i])/32;
        if (s->target[i]!=s->gain[i]) moving=1u;
        if (s->stage[i] || s->gain[i]) active=1u;
    }
    s->remaining=moving?32u:0u;
    return active || gate;
}
static int32_t tk_fm_wave(tk_fm_voice_t *s)
{
    int32_t output[6], sum=0;
    uint32_t i,j;
    if (s->remaining) {
        if (s->remaining==1u) for (i=0; i<6u; ++i) s->gain[i]=s->target[i];
        else for (i=0; i<6u; ++i) s->gain[i]+=s->delta[i];
        --s->remaining;
    }
    for (i=6u; i-- >0u;) {
        int32_t modulation=0;
        uint32_t phase;
        for (j=0; j<s->input_count[i]; ++j) modulation+=output[s->input[i][j]];
        if (i==5u && s->patch.feedback)
            modulation+=(s->feedback_sample*(int32_t)s->patch.feedback)>>7;
        s->phase[i]+=s->increment[i];
        /* Unsigned phase wrapping is intentional, including negative PM. */
        phase=s->phase[i]+((uint32_t)modulation<<16);
        output[i]=(tk_fm_sine(phase)*s->gain[i])>>15;
    }
    s->feedback_sample=output[5];
    for (i=0; i<s->carrier_count; ++i) sum+=output[s->carrier[i]];
    return (sum*s->carrier_gain)>>15;
}
#endif
