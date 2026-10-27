/* SPDX-License-Identifier: GPL-3.0-only */
/* Main-owned complete-song journal. Format3 stamps catalog revision2.
 * Format1/2 migrate through the immutable original64 prefix with unchanged
 * sound IDs, captured patches, LIVE/GRAFT, banks and swing. No implicit remix. */
#ifndef TERKINSTEIN_PERSISTENCE_H
#define TERKINSTEIN_PERSISTENCE_H
#define TK_PERSIST_FORMAT 3u
#define TK_PERSIST_META 16u
static int tk_persist_legacy_valid(const tk_settings_t *s)
{
    uint32_t i;
    if (!settings_valid(s) || s->preset >= 64u || s->graft_base.preset >= 64u || s->graft_target >= 64u) return 0;
    for (i=0;i<TK_TRACKS;i++) if (s->track[i].preset >= 64u) return 0;
    return 1;
}
#define TK_PERSIST_CORE_END (TK_PERSIST_META + sizeof(tk_settings_t))
#define TK_PERSIST_SONG_END (TK_PERSIST_CORE_END + sizeof(tk_song_stage_t))
#define TK_PERSIST_BYTES TK_PERSIST_SONG_END
#define TK_PERSIST_V1_STAGE offsetof(tk_song_stage_t,swing)
#define TK_PERSIST_V1_BYTES (TK_PERSIST_META+TK_SETTINGS_V12_BYTES+TK_PERSIST_V1_STAGE)
typedef char tk_persist_fits[(TK_PERSIST_BYTES <= TK_STORE_MAX) ? 1 : -1];
#ifdef TERKINSTEIN_HOST
static tk_settings_t tk_persist_fallback;
#else
static tk_settings_t tk_persist_fallback __attribute__((section(".pool")));
#endif
static int tk_persist_pull(void *context,uint32_t at,uint8_t *out,uint32_t n)
{
    uint8_t meta[TK_PERSIST_META];uint32_t i;
    const tk_settings_t *s=(const tk_settings_t*)context;
    tk_store_put32(meta,TK_PERSIST_FORMAT);tk_store_put32(meta+4,sizeof *s);
    tk_store_put32(meta+8,sizeof tk_bank_pool.stage);tk_store_put32(meta+12,TK_CATALOG_REVISION);
    if(at>TK_PERSIST_BYTES||n>TK_PERSIST_BYTES-at)return -1;
    for(i=0;i<n;i++,at++){
        if(at<TK_PERSIST_META)out[i]=meta[at];
        else if(at<TK_PERSIST_CORE_END)out[i]=((const uint8_t*)s)[at-TK_PERSIST_META];
        else out[i]=((const uint8_t*)&tk_bank_pool.stage)[at-TK_PERSIST_CORE_END];
    }
    return 0;
}
static int tk_persist_safe(const tk_transport_t *t,uint32_t keys)
{
    return !t->running&&!t->recording&&!tk_banks.importing&&!keys&&tk_quiet()&&tk_metronome_quiet();
}
static int tk_persist_save(tk_settings_t *s,tk_transport_t *t,uint32_t keys)
{
    if(!tk_persist_safe(t,keys)||!settings_valid(s)||!tk_drums_valid(tk_drum_mix))return -1;
    memset(&tk_bank_pool.stage,0,sizeof tk_bank_pool.stage);tk_song_clone(s,t);
    if(!tk_song_valid(&tk_bank_pool.stage)||tk_store_save(TK_PERSIST_BYTES,tk_persist_pull,s))return -1;
    /* Older firmware can read this schema12 active-pattern projection.
     * Swing/full latest banks require the new journal. Reuse main scratch. */
    if (!tk_persist_legacy_valid(s)) return 0; /* Keep the last valid legacy projection. */
    tk_persist_fallback=*s;tk_persist_fallback.schema=12u;
    return settings_save(&tk_persist_fallback,TK_SETTINGS_V12_BYTES);
}
static int tk_persist_load_slot(tk_settings_t *s,tk_transport_t *t,uint32_t slot)
{
    uint8_t meta[TK_PERSIST_META];uint32_t length,generation,core,stage,format;
    if(tk_store_probe(slot,&length,&generation)||
        tk_store_slice(slot,TK_STORE_HEADER,meta,sizeof meta,0))return -1;
    format=tk_store_u32(meta);core=tk_store_u32(meta+4);stage=tk_store_u32(meta+8);
    if (tk_store_u32(meta+12) != (format==TK_PERSIST_FORMAT ? TK_CATALOG_REVISION : 0u)) return -1;
    if(format==1u){
        if(length!=TK_PERSIST_V1_BYTES||core!=TK_SETTINGS_V12_BYTES||stage!=TK_PERSIST_V1_STAGE)return -1;
    }else if(format==2u||format==TK_PERSIST_FORMAT){
        if(length!=TK_PERSIST_BYTES||core!=sizeof *s||stage!=sizeof tk_bank_pool.stage)return -1;
    }else return -1;
    memset(s,0,sizeof *s);memset(&tk_bank_pool.stage,0,sizeof tk_bank_pool.stage);
    if(tk_store_slice(slot,TK_STORE_HEADER+TK_PERSIST_META,s,core,0))return -1;
    if(format==1u){if(s->schema!=12u)return -1;s->schema=TK_SCHEMA;}
    if(!settings_valid(s)||tk_store_slice(slot,TK_STORE_HEADER+TK_PERSIST_META+core,
        &tk_bank_pool.stage,stage,0)||!tk_song_valid(&tk_bank_pool.stage))return -1;
    if(s->swing!=tk_bank_pool.stage.swing)return -1;
    if(format!=TK_PERSIST_FORMAT){
        uint32_t i;
        if(!tk_persist_legacy_valid(s))return -1;
        for(i=0;i<TK_TRACKS;i++)if(tk_bank_pool.stage.patch[i].preset>=64u)return -1;
    }
    tk_song_commit(s,t);return 0;
}
static int tk_persist_load(tk_settings_t *s,tk_transport_t *t)
{
    uint32_t slot,length,generation;
    if(tk_store_latest(&slot,&length,&generation))return -1;
    tk_persist_fallback=*s;
    if(!tk_persist_load_slot(s,t,slot))return 0;
    *s=tk_persist_fallback;
    if(!tk_persist_load_slot(s,t,slot^1u))return 0;
    *s=tk_persist_fallback;return -1;
}
#endif
