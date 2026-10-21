/* SPDX-License-Identifier: GPL-3.0-only */
#define TERKINSTEIN_HOST 1
#define main tk_persistence_host_main
#include "../firmware/src/terkinstein.c"
#undef main
static tk_settings_t fixture, expected_core;
static tk_transport_t transport;
static tk_song_stage_t expected_song;
static uint32_t checks;
static void check(int ok, const char *why)
{ checks++; if (!ok) { fprintf(stderr, "FAIL persistence: %s\n", why); exit(1); } }
static void reboot(void)
{
    memset(&transport, 0, sizeof transport); settings_defaults(&fixture);
    settings_load(&fixture, sizeof fixture); tk_init(); tk_metronome_init();
    tk_drums_defaults(tk_drum_mix);
    tk_metronome_clock_init(&metro_clock); tk_banks_init(&fixture, &transport);
    tk_persist_load(&fixture, &transport);
}
static void fill_song(void)
{
    uint32_t track, bank, step, note;
    reboot();
    for (track = 0; track < TK_TRACKS; track++) {
        for (bank = 0; bank < TK_BANK_COUNT; bank++) {
            tk_pattern_t *p = tk_bank_pattern(&fixture, track, bank);
            tk_banks.length[track][bank] = (uint8_t)(16u << ((track + bank) % 3u));
            for (step = 0; step < TK_PATTERN_STEPS; step++) {
                p->step[step].count = TK_PATTERN_NOTES;
                for (note = 0; note < TK_PATTERN_NOTES; note++) {
                    p->step[step].note[note].pitch = (uint8_t)(24u + track * 12u + bank + note * 2u);
                    p->step[step].note[note].velocity = (uint8_t)(20u + step + bank + note);
                    p->step[step].note[note].duration_q4 = (uint8_t)(step + 1u + note);
                }
            }
        }
        fixture.length[track] = tk_banks.length[track][0];
        tk_bank_activate(&fixture, &transport, track, track, 0);
    }
    fixture.section_mutes = 0x4218u; fixture.tempo = 139u; fixture.metro_beats = 7;
    fixture.track_mute = 5u; fixture.fx = 1u; transport.selected = TK_TRACK_BASS;
    tk_drum_mix[0].decay = 112u; tk_drum_mix[2].level = 90u;
    tk_arr_song.step[2][3][63].mask_condition = 127u;
    tk_arr_song.step[2][3][63].value[0] = 121u;
    tk_arr_song.step[2][3][63].probability = 67u;
    tk_arr_song.scene[7].enabled = 1; tk_arr_song.scene[7].tempo = 180u;
    memset(&tk_bank_pool.stage, 0, sizeof tk_bank_pool.stage); tk_song_clone(&fixture, &transport);
    check(settings_valid(&fixture) && tk_song_valid(&tk_bank_pool.stage), "dense fixture is valid");
    expected_song = tk_bank_pool.stage; expected_core = fixture;
}
static int altered_pull(void *context, uint32_t at, uint8_t *out, uint32_t n)
{
    uint32_t bad_offset = *(uint32_t *)context, i;
    if (tk_persist_pull(&expected_core, at, out, n)) return -1;
    for (i = 0; i < n; i++) if (at + i == bad_offset) out[i] = 255u;
    return 0;
}
int main(void)
{
    uint32_t slot, length, generation, bad;
    host_journal_import();
    check(tk_persist_load(&fixture, &transport) != 0, "empty journal uses legacy defaults");
    fill_song();
    transport.running = 1;
    check(tk_persist_save(&fixture, &transport, 0) != 0, "running transport cannot erase flash in a quiet gap");
    check(tk_store_latest(&slot, &length, &generation) != 0, "unsafe save made no journal");
    transport.running = 0;
    check(tk_persist_save(&fixture, &transport, 1) != 0, "held panel keys defer SAVE");
    tk_banks.importing = 1;
    check(tk_persist_save(&fixture, &transport, 0) != 0, "song import defers SAVE");
    tk_banks.importing = 0;
    check(tk_persist_save(&fixture, &transport, 0) == 0, "physical SAVE commits dense sixteen-bank snapshot");
    reboot(); memset(&tk_bank_pool.stage, 0, sizeof tk_bank_pool.stage); tk_song_clone(&fixture, &transport);
    check(!memcmp(&tk_bank_pool.stage, &expected_song, sizeof expected_song), "all banks, lengths, patches, active selections and globals survive reboot");
    check(!memcmp(&fixture, &expected_core, sizeof fixture), "complete settings survive reboot");
    check(!transport.running && !transport.recording, "boot never starts saved playback");
    check(tk_drum_mix[0].decay == 112u && tk_drum_mix[2].level == 90u, "individual drums survive reboot");
    check(tk_arr_song.step[2][3][63].value[0] == 121u && tk_arr_song.scene[7].tempo == 180u, "locks, conditions, probabilities and scenes survive reboot");
    fixture.tempo = 123;
    check(tk_persist_save(&fixture, &transport, 0) == 0, "second snapshot commits");
    check(!tk_store_latest(&slot, &length, &generation) && generation == 2u, "newest generation selected");
    host_journal[slot * TK_STORE_SLOT_BYTES + TK_STORE_HEADER + TK_PERSIST_META + 9u] ^= 1u;
    reboot(); check(fixture.tempo == expected_core.tempo, "CRC corruption falls back to previous full song");
    tk_song_clone(&fixture, &transport);
    bad = 0; check(!tk_store_save(TK_PERSIST_BYTES, altered_pull, &bad), "write CRC-valid unknown-format fixture");
    reboot(); check(fixture.tempo == expected_core.tempo, "future payload format falls back safely");
    check(!tk_store_latest(&slot, &length, &generation), "unknown record remains CRC-valid");
    memset(host_journal + slot * TK_STORE_SLOT_BYTES, 255, TK_STORE_SLOT_BYTES);
    tk_song_clone(&fixture, &transport);
    bad = TK_PERSIST_META + sizeof fixture;
    check(!tk_store_save(TK_PERSIST_BYTES, altered_pull, &bad), "write CRC-valid malformed pattern fixture");
    reboot(); check(fixture.tempo == expected_core.tempo, "semantically invalid snapshot restores previous complete song");
    memset(host_journal, 255, sizeof host_journal); fixture.tempo = 99;
    check(!settings_save(&fixture, sizeof fixture), "legacy save available without journal");
    reboot(); check(fixture.tempo == 99u, "unchanged schema migrates legacy active patterns and settings");
    printf("song persistence: %u checks passed; %u-byte payload\n", checks, (unsigned)TK_PERSIST_BYTES);
    return 0;
}
