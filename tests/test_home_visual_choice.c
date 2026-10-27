/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual app controls, journal and both cooperative renderers. Host only. */
#define TERKINSTEIN_HOST 1
#define main tk_choice_original_main
#include "../firmware/src/terkinstein.c"
#undef main
static uint32_t checks;
static tk_settings_t state, expected, reloaded;
static tk_settings_v11_t old11;
static tk_transport_t clock_state;
static tk_lfo_t lfo_state;
static uint16_t reference[TK_RIFT_W * TK_RIFT_H];
static void check(int ok, const char *why)
{
    checks++;
    if (!ok) { fprintf(stderr, "FAIL HOME choice: %s\n", why); exit(1); }
}
static int turn(uint32_t knob, int32_t delta)
{
    uint32_t hold = 0;
    uint8_t shade = state.p.shade;
    return tk_page_knob(&state, &clock_state, TK_PAGE_CONTROLS, knob,
                        delta, &hold, &shade, &lfo_state);
}
static void complete(const tk_visual_snapshot_t *sound, uint32_t now, int direct)
{
    uint32_t i;
    tk_visual_snapshot_t immutable = *sound;
    for (i = 0; i < 256u; i++) {
        if (direct == 1) tk_rift_prepare(sound, now, state.preset);
        else if (direct == 2) tk_homunculus_visual_prepare(sound, now, state.preset);
        else tk_home_visual_prepare(sound, now, state.preset);
        if (!tk_rift_phase && !tk_rift_force) break;
    }
    check(i < 256u, "frame construction stays bounded");
    for (i = 0; tk_home_visual_pending() && i < 256u; i++) {
        uint32_t before = host_lcd_pixels, sent = tk_home_visual_upload();
        check(sent > 0u && sent <= 512u && host_lcd_pixels - before == sent,
              "each upload keeps the existing 512-pixel budget");
    }
    check(!tk_home_visual_pending(), "dirty uploads finish");
    check(!memcmp(sound, &immutable, sizeof immutable), "sound snapshot is read-only");
}
static void save_screen(const char *folder, const char *name)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s.ppm", folder, name);
    write_ppm_as(path);
}
int main(int argc, char **argv)
{
    uint32_t i, x, y;
    tk_visual_snapshot_t sound = {0};
    check(argc == 2, "preview output folder supplied");
    settings_defaults(&state);
    check(sizeof state == 3704u && offsetof(tk_settings_t, home_visual) == 3697u
          && offsetof(tk_settings_t, section_mutes) == 3698u
          && offsetof(tk_settings_t, swing) == 3700u,
          "schema13 appends swing after the unchanged HOME choice and mutes");
    check(!state.home_visual && !tk_home_visual_is_rift(state.home_visual), "default is Homunculus");
    for (i = 0; i < 256u; i++) {
        state.home_visual = (uint8_t)i;
        check(settings_valid(&state), "cosmetic legacy padding cannot reject a valid song");
        check(tk_home_visual_is_rift(i) == (i == TK_HOME_VISUAL_RIFT), "only the explicit tag selects Rift");
    }
    settings_defaults(&state);expected = state;
    check(turn(1, 1) == 7, "Knob 2 marks a saved display change");
    expected.home_visual = TK_HOME_VISUAL_RIFT;
    check(!memcmp(&state, &expected, sizeof state), "K2 changes no musical or response setting");
    check(turn(1, 32767) == 1 && state.home_visual == TK_HOME_VISUAL_RIFT, "Rift upper bound is a no-op");
    check(turn(1, -32767) == 7 && !state.home_visual, "negative detents restore Homunculus");
    check(turn(1, -1) == 1 && !state.home_visual, "Homunculus lower bound is a no-op");
    check(turn(2, 1) == 1 && turn(3, -1) == 1, "K3/K4 remain unused");
    tk_banks.importing = 1;
    check(turn(1, 1) == 1 && !state.home_visual, "staged import blocks the cosmetic edit");
    tk_banks.importing = 0;
    check(turn(0, 1) == 7 && state.panel_response == TK_ENCODER_ADAPTIVE, "K1 keeps its response choice");
    check(text_w(&FONT_S, "HOMUNCULUS") <= 112 && text_w(&FONT_S, "THORN RIFT") <= 112,
          "both choice labels fit their LCD column");
    lcd_fill(0, 0, 240, 240, 0);draw_header(TK_PAGE_CONTROLS);draw_control_feel(&state);
    save_screen(argv[1], "controls-homunculus");
    turn(1, 1);draw_control_feel(&state);save_screen(argv[1], "controls-thorn-rift");

    /* Old migration padding is explicitly reset; all old musical fields stay. */
    memcpy(&old11, &state, sizeof old11);old11.schema = 11u;
    ((uint8_t *)&old11)[3697] = TK_HOME_VISUAL_RIFT;
    check(settings_upgrade_v11(&reloaded, &old11) && !reloaded.home_visual
          && !reloaded.section_mutes, "schema-11 padding never becomes a new preference");
    tk_init();tk_metronome_init();tk_drums_defaults(tk_drum_mix);
    tk_banks_init(&state, &clock_state);host_journal_import();
    state.section_mutes = 0x8421u; state.swing = 51u;
    check(!tk_persist_save(&state, &clock_state, 0), "normal silent SAVE stores choice and complete song");
    settings_defaults(&reloaded);
    check(!tk_persist_load(&reloaded, &clock_state)
          && reloaded.home_visual == TK_HOME_VISUAL_RIFT && reloaded.section_mutes == 0x8421u
          && reloaded.swing == 51u,
          "journal reload retains preference, section mutes and swing");
    memset(&reloaded, 0, sizeof reloaded);
    check(!settings_load(&old11, TK_SETTINGS_V12_BYTES) && settings_upgrade_v12(&reloaded, &old11)
          && settings_valid(&reloaded) && reloaded.home_visual == TK_HOME_VISUAL_RIFT
          && reloaded.swing == 0u, "legacy schema12 projection preserves HOME and migrates to straight timing");
    tk_song_clone(&state, &clock_state);state.home_visual = 0u;
    tk_song_commit(&state, &clock_state);
    check(!state.home_visual, "song imports preserve this device's display preference");

    sound.peak = 16000u;sound.rough = 9000u;
    sound.voice[0] = 60u | (190u << 8) | (1u << 24);
    for (i = 0; i < TK_VIS_SAMPLES; i++) sound.mono[i] = (int16_t)((int32_t)(i % 9u) * 4000 - 16000);
    for (i = 0; i < 2u; i++) {
        uint32_t now = 2000u + i * 1000u;
        memset(fb, 0x6B, sizeof fb);
        tk_home_visual_select(i ? TK_HOME_VISUAL_RIFT : 0u);tk_home_visual_reset();
        complete(&sound, now, 0);memcpy(reference, tk_rift_front, sizeof reference);
        for (y = 0; y < 240u; y++) for (x = 0; x < 240u; x++)
            if (x < 8u || x >= 232u || y < 70u || y >= 214u)
                check(fb[y * 240u + x] == 0x6B6Bu, "animation preserves all header/footer pixels");
        tk_home_visual_reset();complete(&sound, now, i ? 1 : 2);
        check(!memcmp(reference, tk_rift_front, sizeof reference), "choice matches its original production renderer");
        lcd_fill(0, 0, 240, 70, 0);lcd_fill(0, 214, 240, 26, 0);
        home_preset_drawn = home_modified_drawn = home_status_drawn = home_graft_drawn = ~0u;
        draw_header(TK_PAGE_PAD);draw_home(&state, 0, 0, 0);
        save_screen(argv[1], i ? "home-thorn-rift" : "home-homunculus");
    }
    tk_home_visual_reset();tk_home_visual_prepare(&sound, 6000u, 0);
    check(tk_rift_phase, "partial geometry exists before a choice change");
    tk_home_visual_select(0u);
    check(!tk_rift_phase && !tk_home_visual_pending() && tk_rift_force,
          "switch discards incomplete old geometry and forces a fresh frame");
    complete(&sound, 6100u, 0);
    printf("HOME choice: %u checks passed; controls, compatibility, SAVE, bounded renderers\n", checks);
    return 0;
}
