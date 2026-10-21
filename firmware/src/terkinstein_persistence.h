/* SPDX-License-Identifier: GPL-3.0-only */
/* Versioned full-song snapshot. Main owns the bank stage; the journal streams
 * it without allocating another copy of all sixteen patterns. */
#ifndef TERKINSTEIN_PERSISTENCE_H
#define TERKINSTEIN_PERSISTENCE_H
#define TK_PERSIST_FORMAT 1u
#define TK_PERSIST_META 16u
/* Preserve the original boot validation and serialized bytes exactly. */
#define tk_persist_legacy_valid settings_valid
#define TK_PERSIST_CORE_END (TK_PERSIST_META + sizeof(tk_settings_t))
#define TK_PERSIST_SONG_END (TK_PERSIST_CORE_END + sizeof(tk_song_stage_t))
#define TK_PERSIST_BYTES TK_PERSIST_SONG_END
typedef char tk_persist_fits[(TK_PERSIST_BYTES <= TK_STORE_MAX) ? 1 : -1];
#ifdef TERKINSTEIN_HOST
static tk_settings_t tk_persist_fallback;
#else
static tk_settings_t tk_persist_fallback __attribute__((section(".pool")));
#endif
static int tk_persist_pull(void *context, uint32_t at, uint8_t *out, uint32_t n)
{
    uint8_t meta[TK_PERSIST_META]; uint32_t i;
    const tk_settings_t *s = (const tk_settings_t *)context;
    tk_store_put32(meta, TK_PERSIST_FORMAT); tk_store_put32(meta + 4, sizeof *s);
    tk_store_put32(meta + 8, sizeof tk_bank_pool.stage);
    tk_store_put32(meta + 12, 0u);
    if (at > TK_PERSIST_BYTES || n > TK_PERSIST_BYTES - at) return -1;
    for (i = 0; i < n; i++, at++) {
        if (at < TK_PERSIST_META) out[i] = meta[at];
        else if (at < TK_PERSIST_META + sizeof *s) out[i] = ((const uint8_t *)s)[at - TK_PERSIST_META];
        else out[i] = ((const uint8_t *)&tk_bank_pool.stage)[at - TK_PERSIST_CORE_END];
    }
    return 0;
}
static int tk_persist_safe(const tk_transport_t *t, uint32_t keys)
{
    return !t->running && !t->recording && !tk_banks.importing && !keys
        && tk_quiet() && tk_metronome_quiet();
}
static int tk_persist_save(tk_settings_t *s, tk_transport_t *t, uint32_t keys)
{
    if (!tk_persist_safe(t, keys) || !settings_valid(s) || !tk_drums_valid(tk_drum_mix)) return -1;
    memset(&tk_bank_pool.stage, 0, sizeof tk_bank_pool.stage);
    tk_song_clone(s, t);
    if (!tk_song_valid(&tk_bank_pool.stage) || tk_store_save(TK_PERSIST_BYTES, tk_persist_pull, s)) return -1;
    /* Older versions still find a compatible four-active-pattern snapshot.
     * The complete journal is already verified before this legacy write. */
    return settings_save(s, sizeof *s);
}
static int tk_persist_load_slot(tk_settings_t *s, tk_transport_t *t, uint32_t slot)
{
    uint8_t meta[TK_PERSIST_META]; uint32_t length, generation;
    if (tk_store_probe(slot, &length, &generation) || length != TK_PERSIST_BYTES
        || tk_store_slice(slot, TK_STORE_HEADER, meta, sizeof meta, 0)
        || tk_store_u32(meta) != TK_PERSIST_FORMAT || tk_store_u32(meta + 4) != sizeof *s
        || tk_store_u32(meta + 8) != sizeof tk_bank_pool.stage || tk_store_u32(meta + 12)) return -1;
    if (tk_store_slice(slot, TK_STORE_HEADER + TK_PERSIST_META, s, sizeof *s, 0)
        || !settings_valid(s)
        || tk_store_slice(slot, TK_STORE_HEADER + TK_PERSIST_META + sizeof *s,
            &tk_bank_pool.stage, sizeof tk_bank_pool.stage, 0)
        || !tk_song_valid(&tk_bank_pool.stage)) return -1;
    tk_song_commit(s, t); return 0;
}
static int tk_persist_load(tk_settings_t *s, tk_transport_t *t)
{
    uint32_t slot, length, generation;
    if (tk_store_latest(&slot, &length, &generation)) return -1;
    tk_persist_fallback = *s;
    if (!tk_persist_load_slot(s, t, slot)) return 0;
    *s = tk_persist_fallback;
    if (!tk_persist_load_slot(s, t, slot ^ 1u)) return 0;
    *s = tk_persist_fallback;
    return -1;
}
#endif
