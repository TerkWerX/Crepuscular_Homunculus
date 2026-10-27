/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual app guard, LCD and dense fake-journal fixture; no hardware access. */
#define TERKINSTEIN_HOST 1
#define main tk_reset_host_main
#include "../firmware/src/terkinstein.c"
#undef main
static uint32_t checks;
static void check(int yes, const char *why)
{ ++checks; if (!yes) { fprintf(stderr, "FAIL HOME reset: %s\n", why); exit(1); } }
static uint32_t event(tk_home_reset_t *r, uint32_t pressed, uint32_t held,
                      uint32_t any, uint32_t cancel, uint32_t now)
{ return tk_home_reset_event(r, pressed, held, any, cancel, now); }
static void guard_cases(void)
{
    const uint32_t h = 1u << BTN_HOME, sel = 1u << BTN_SEL;
    tk_home_reset_t r = {0}; uint32_t now;
    check(!event(&r, h, h, h, 0, 10), "HOME press is navigation only");
    check(!event(&r, 0, h, 0, 0, 709), "699ms never opens or resets");
    check(event(&r, 0, h, 0, 0, 710) == TK_RESET_OPEN, "700ms opens confirmation only");
    check(r.prompt && !r.ready, "held HOME cannot confirm");
    check(!event(&r, sel, h | sel, sel, 0, 711), "HOME plus SEL chord cannot confirm");
    check(!event(&r, 0, sel, 0, 0, 712), "preheld SEL cannot arm confirmation");
    check(event(&r, 0, 0, 0, 0, 713) == TK_RESET_READY, "all physical buttons must first release");
    check(event(&r, sel, sel, sel, 0, 714) == TK_RESET_CONFIRM, "fresh physical SEL alone confirms");
    check(!r.prompt && !event(&r, 0, sel, 0, 0, 715), "one confirmation per gesture");
    memset(&r, 0, sizeof r);
    for (now = 0; now < 2000u; ++now)
        check(!(event(&r, 0, h, 0, 0, now) & TK_RESET_CONFIRM) && !r.prompt,
              "unmatched/stale held HOME cannot even arm a reset");
    check(!event(&r, h, 0, h, 0, 2001) && !r.tracking, "one-shot HOME edge is never a reset hold");
    event(&r, h, h, h, 0, 3000); event(&r, 0, h, 0, 0, 3700);
    event(&r, 0, 0, 0, 0, 3701);
    check(event(&r, 0, 0, sel, 0, 3702) == TK_RESET_CANCEL,
          "virtual SEL cancels instead of confirming a physical reset");
    event(&r, h, h, h, 0, 4000); event(&r, 0, h, 0, 0, 4700);
    check(event(&r, 0, h, 0, 0, 14700) == TK_RESET_CANCEL, "prompt expires after10sec without resetting");
    check(!event(&r, 0, h, 0, 0, 18000), "continued hold cannot reopen expired prompt");
    event(&r, h, h, h, 0, 0xfffffe00u);
    check(event(&r, 0, h, 0, 0, 188u) == TK_RESET_OPEN, "hold threshold survives32bit time wrap");
    check(event(&r, 0, 0, 0, 1, 189) == TK_RESET_CANCEL, "SAVE/import or other activity cancels prompt");
    check(!event(&r, h, h, h, 1, 500) && !r.prompt, "blocked input cannot arm reset");
    event(&r, h, h, h, 0, 20000); event(&r, 0, h, 0, 0, 20700);
    check(event(&r, 1u << BTN_PLAY, 0, 1u << BTN_PLAY, 0, 20701) == TK_RESET_CANCEL,
          "PLAY cancels prompt so normal transport handling stays available");
    event(&r, h, h, h, 0, 21000); event(&r, 0, h, 0, 0, 21700);
    check(event(&r, h, h, h, 0, 21701) == TK_RESET_CANCEL, "HOME cancels");
}
static void fixture(void)
{
    static tk_settings_t s;
    static tk_transport_t t;
    uint32_t track, bank, step;
    settings_defaults(&s); tk_init(); tk_metronome_init(); tk_drums_defaults(tk_drum_mix);
    tk_metronome_clock_init(&metro_clock); tk_banks_init(&s, &t);
    s.tempo = 90u; s.swing = 50u; s.section_mutes = 0x4218u;
    s.home_visual = TK_HOME_VISUAL_RIFT; s.panel_response = 1u;
    s.track_mute = 5u; s.metro_beats = 7u;
    for (track = 0; track < TK_TRACKS; ++track) {
        track_factory_apply(&s, track, 32u + track);
        s.track[track].p.shade = (uint8_t)(20u + track);
        s.fm[track + 1u].op[0].level = (uint8_t)(90u + track);
        for (bank = 0; bank < TK_BANK_COUNT; ++bank) {
            tk_pattern_t *p = tk_bank_pattern(&s, track, bank);
            tk_pattern_clear(p);
            for (step = 0; step < TK_PATTERN_STEPS; ++step)
                tk_pattern_record(p, step, 36u + track * 12u + step % 12u, 70u + bank);
        }
    }
    tk_arr_song.step[2][3][63].mask_condition = 127u;
    tk_arr_song.step[2][3][63].value[0] = 121u;
    tk_arr_song.step[2][3][63].probability = 67u;
    tk_arr_song.scene[7].enabled = 1u; tk_arr_song.scene[7].tempo = 130u;
    tk_drum_mix[0].decay = 112u; t.selected = TK_TRACK_BASS;
    check(settings_valid(&s), "dense nondefault settings valid");
    check(!tk_persist_save(&s, &t, 0u), "dense fixture saves only to fake host files");
}
int main(int argc, char **argv)
{
    guard_cases();
    if (argc > 1) {
        char path[1024];
        draw_home_reset(0u); while (lcd_cooperative_upload()) {}
        snprintf(path, sizeof path, "%s/reset-release.ppm", argv[1]); write_ppm_as(path);
        draw_home_reset(1u); while (lcd_cooperative_upload()) {}
        snprintf(path, sizeof path, "%s/reset-confirm.ppm", argv[1]); write_ppm_as(path);
        fixture();
    }
    printf("PASS HOME reset guard/LCD/dense fixture: %u checks; host only\n", checks);
    return 0;
}
