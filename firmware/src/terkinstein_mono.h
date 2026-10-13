/* SPDX-License-Identifier: GPL-3.0-only */
/* Main-owned last-note priority. Physical keys, pedal captures and order are
 * retained independently for panel and each of the 32 external MIDI owners.
 * Generated tracks are excluded and keep their ordinary polyphonic allocator.
 * No stack search or ownership mutation runs in the audio callback. */
#ifndef TERKINSTEIN_MONO_H
#define TERKINSTEIN_MONO_H
#define TK_MONO_KEY 1u
#define TK_MONO_SUSTAIN 2u
#define TK_MONO_SOSTENUTO 4u
typedef struct {
    uint8_t velocity[128], flags[128], order[128];
    uint16_t count;
} tk_mono_state_t;
static tk_mono_state_t tk_mono_state[TK_SOURCE_SEQUENCE];
static uint32_t tk_mono; /* main-only mode; audio receives ordinary note commands */
static int tk_note_on_raw(uint32_t note,uint32_t vel,uint32_t source);
static void tk_note_off_raw(uint32_t note,uint32_t source);
static uint32_t tk_revision(uint32_t old);

static void tk_mono_clear(uint32_t source)
{
    uint32_t note;
    if (source>=TK_SOURCE_SEQUENCE) return;
    tk_mono_state[source].count=0u;
    for (note=0; note<128u; ++note) {
        tk_mono_state[source].velocity[note]=0u;
        tk_mono_state[source].flags[note]=0u;
    }
}
static void tk_mono_init(void)
{
    uint32_t source;
    tk_mono=0u;
    for (source=0; source<TK_SOURCE_SEQUENCE; ++source) tk_mono_clear(source);
}
static void tk_mono_prune(uint32_t source)
{
    tk_mono_state_t *s=&tk_mono_state[source];
    uint32_t i, count=0u;
    for (i=0; i<s->count; ++i) if (s->flags[s->order[i]]) s->order[count++]=s->order[i];
    s->count=(uint16_t)count;
}
static void tk_mono_push(uint32_t note,uint32_t velocity,uint32_t source)
{
    tk_mono_state_t *s=&tk_mono_state[source];
    uint32_t i, count=0u;
    for (i=0; i<s->count; ++i) if (s->order[i]!=note) s->order[count++]=s->order[i];
    s->order[count++]=(uint8_t)note; s->count=(uint16_t)count;
    s->flags[note]=TK_MONO_KEY; s->velocity[note]=(uint8_t)velocity;
}
static int tk_mono_priority(uint32_t source)
{
    const tk_mono_state_t *s=&tk_mono_state[source];
    uint32_t i;
    for (i=s->count; i-- >0u;) if (s->flags[s->order[i]]&TK_MONO_KEY) return s->order[i];
    for (i=s->count; i-- >0u;) if (s->flags[s->order[i]]) return s->order[i];
    return -1;
}
static void tk_mono_metadata(uint32_t note,uint32_t source)
{
    uint32_t v, flags=tk_mono_state[source].flags[note];
    for (v=0; v<TK_VOICES; ++v) {
        uint32_t command=tk_voice[v].command;
        if ((command&TK_GATE) && (command&127u)==note && ((command>>14)&63u)==source) {
            tk_voice[v].key_down=!!(flags&TK_MONO_KEY);
            tk_voice[v].sostenuto_latched=!!(flags&TK_MONO_SOSTENUTO);
        }
    }
}
static int tk_mono_refresh(uint32_t source)
{
    uint32_t v, pick=TK_VOICES;
    int note;
    if (!tk_mono || source>=TK_SOURCE_SEQUENCE) return 0;
    note=tk_mono_priority(source);
    /* Prefer the already sounding winner; otherwise reuse this owner's voice.
     * Other old gates from switching POLY to MONO receive the normal panic fade. */
    for (v=0; v<TK_VOICES; ++v) {
        uint32_t command=tk_voice[v].command;
        if (!(command&TK_GATE) || ((command>>14)&63u)!=source) continue;
        if (pick==TK_VOICES || (note>=0 && (command&127u)==(uint32_t)note)) pick=v;
    }
    for (v=0; v<TK_VOICES; ++v) {
        uint32_t command=tk_voice[v].command;
        if (!(command&TK_GATE) || ((command>>14)&63u)!=source || (v==pick && note>=0)) continue;
        tk_voice[v].key_down=tk_voice[v].sostenuto_latched=0u;
        tk_voice[v].command=tk_revision(command)|(command&(TK_GATE-1u))|
            (note>=0?TK_PANIC:0u);
    }
    if (note<0) return 0;
    if (pick==TK_VOICES || (tk_voice[pick].command&127u)!=(uint32_t)note)
        if (!tk_note_on_raw((uint32_t)note,tk_mono_state[source].velocity[note],source)) return 0;
    tk_mono_metadata((uint32_t)note,source);
    return 1;
}
static void tk_set_mono(uint32_t enabled)
{
    uint32_t source,i;
    enabled=!!enabled;
    if (tk_mono==enabled) return;
    tk_mono=enabled;
    for (source=0; source<TK_SOURCE_SEQUENCE; ++source) {
        if (enabled) tk_mono_refresh(source);
        else {
            const tk_mono_state_t *s=&tk_mono_state[source];
            for (i=0; i<s->count; ++i) {
                uint32_t note=s->order[i],v;
                int existing=0;
                for (v=0; v<TK_VOICES; ++v) {
                    uint32_t command=tk_voice[v].command;
                    if ((command&TK_GATE) && (command&127u)==note && ((command>>14)&63u)==source)
                        existing=1;
                }
                if (!existing) tk_note_on_raw(note,s->velocity[note],source);
                tk_mono_metadata(note,source);
            }
        }
    }
}
static void tk_mono_key_up(uint32_t note,uint32_t source)
{
    tk_mono_state_t *s=&tk_mono_state[source];
    if (!(s->flags[note]&TK_MONO_KEY)) return;
    s->flags[note]&=6u;
    if (tk_source_control[source]&TK_SUSTAIN) s->flags[note]|=TK_MONO_SUSTAIN;
    tk_mono_prune(source);
}
static void tk_mono_pedal(uint32_t source,uint32_t pedal,uint32_t enabled)
{
    uint32_t note,v;
    if (source>=TK_SOURCE_SEQUENCE) return;
    for (note=0; note<128u; ++note) {
        uint8_t *flags=&tk_mono_state[source].flags[note];
        if (!enabled) *flags&=(uint8_t)~pedal;
        else if ((pedal==TK_MONO_SOSTENUTO && (*flags&TK_MONO_KEY)) ||
                 (pedal==TK_MONO_SUSTAIN && *flags && !(*flags&TK_MONO_KEY))) {
            /* Sostenuto captures sounding keys, not an older physically held
             * key that MONO currently suppresses or POLY has already stolen. */
            for (v=0; v<TK_VOICES; ++v) {
                uint32_t command=tk_voice[v].command;
                if ((command&TK_GATE) && (command&127u)==note && ((command>>14)&63u)==source
                    && (pedal==TK_MONO_SUSTAIN || tk_voice[v].key_down)) *flags|=(uint8_t)pedal;
            }
        }
    }
    tk_mono_prune(source);
}
#endif
