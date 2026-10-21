/* SPDX-License-Identifier: GPL-3.0-only */
/* Real firmware functions on fake hardware; no device timing claim. */
#define TERKINSTEIN_HOST 1
#define main tk_piano_host_main
#include "../firmware/src/terkinstein.c"
#undef main
static tk_settings_t s, before;
static tk_transport_t t;
static uint32_t checks;
static uint16_t cells[TK_PIANO_ROWS][TK_PIANO_COLS];
static void check(int ok, const char *why)
{ checks++; if (!ok) { fprintf(stderr, "FAIL piano %u: %s\n", checks, why); exit(1); } }
static void reset(void)
{
    tk_init(); settings_defaults(&s); memset(&t, 0, sizeof t); tk_banks_init(&s, &t);
    t.selected = 1u; t.mode = TK_MODE_SEQ; s.length[1] = 64u;
    tk_pattern_clear(&s.pattern[1]);
    tk_piano.pitch = 60u; tk_piano.duration = 4u; tk_piano.velocity = 100u; tk_piano.full = 0u;
    memset(&tk_piano_paint, 0, sizeof tk_piano_paint);
}
static void shot(const char *path)
{
    uint32_t n;
    lcd_fill(0, 0, 240, 240, C_BLACK); draw_header(TK_PAGE_PIANO);
    tk_piano_paint.valid = 0u; draw_piano(&s, &t);
    for (n = 0u; n < 300u && tk_piano_upload(); n++) {}
    write_ppm_as(path);
}
int main(void)
{
    uint32_t i, gate, len, onset, cursor, row, col, q, pixels, steps[4] = {0u, 15u, 16u, 63u};
    reset();
    for (i = 0; i < 4u; i++) {
        tk_piano.pitch = (uint8_t)(60u + i * 3u);
        check(tk_piano_button(&s, &t, 1u) == 7, "four distinct chord pitches accepted");
    }
    before = s; tk_piano.pitch = 73u;
    check(tk_piano_button(&s, &t, 1u) == 5 && tk_piano.full && !memcmp(&s, &before, sizeof s), "fifth pitch refused visibly without erasing any chord member");
    tk_piano.pitch = 63u;
    check(tk_piano_encoder(&s, &t, 2u, 60) == 7, "individual note extended to whole note");
    check(tk_pattern_find(&s.pattern[1], 0u, 63u)->duration_q4 == 64u, "whole gate exact");
    check(tk_piano_encoder(&s, &t, 3u, -70) == 7 && tk_pattern_find(&s.pattern[1], 0u, 63u)->velocity == 30u, "individual velocity changes");
    check(s.pattern[1].step[0].note[0].duration_q4 == 4u && s.pattern[1].step[0].note[2].velocity == 100u, "neighbors untouched");
    before = s;
    check(tk_piano_button(&s, &t, 1u) == 5 && !memcmp(&s, &before, sizeof s), "selecting an existing onset preserves recorded gate and velocity");
    check(tk_piano_button(&s, &t, 0u) == 7 && s.pattern[1].step[0].count == 3u, "delete only cursor onset");
    check(s.pattern[1].step[0].note[1].pitch == 66u && !s.pattern[1].step[0].note[3].velocity, "delete retains sorting and clears unused slot");
    before = s;
    check(tk_piano_button(&s, &t, 0u) == 5 && !memcmp(&s, &before, sizeof s), "delete missing onset is harmless");
    for (i = 0; i < 2u; i++) {
        t.recording = (uint8_t)!i; tk_banks.importing = (uint8_t)i; before = s;
        check(tk_piano_encoder(&s, &t, 2u, 50) == 1 && tk_piano_encoder(&s, &t, 4u, 1) == 1 && tk_piano_button(&s, &t, 1u) == 1,
            "recording/import locks note edits and destination navigation");
        check(!memcmp(&s, &before, sizeof s), "locked song unchanged");
    }
    reset(); before = s;
    for (i = 0; i < 4u; i++) tk_piano_encoder(&s, &t, i < 2u ? i : i + 2u, 1);
    check(!memcmp(s.track, before.track, sizeof s.track) && s.preset == before.preset && !t.running, "step/pitch/track/bank browsing preserves patches and stopped transport");
    reset(); tk_piano.duration = 254u;
    check(tk_piano_encoder(&s, &t, 2u, 1) == 5 && tk_piano.duration == 255u, "full-loop endpoint uses existing 255 sentinel");
    check(tk_piano_encoder(&s, &t, 2u, -1) == 5 && tk_piano.duration == 254u, "endpoint exits without unrepresentable 255-quarter gate");
    tk_piano_encoder(&s, &t, 2u, -99999); check(tk_piano.duration == 1u, "large negative detents bounded");
    tk_piano_encoder(&s, &t, 3u, 99999); check(tk_piano.velocity == 127u, "large positive velocity bounded");
    /* Independent pixel oracle: each quarter-step is three pixels, with modular
     * local-loop distance. Covers legacy pulses, page edges and sentinel wraps. */
    for (len = 16u; len <= 64u; len *= 2u) for (onset = 0; onset < 4u; onset++) {
        uint32_t start = steps[onset] % len;
        for (gate = 0; gate <= 255u; gate++) {
            reset(); s.length[1] = (uint8_t)len; tk_pattern_record(&s.pattern[1], start, 60u, 100u);
            s.pattern[1].step[start].note[0].duration_q4 = (uint8_t)gate;
            for (cursor = 0; cursor < len; cursor += 16u) {
                tk_piano_grid(cells, &s.pattern[1], len, cursor, 60u, TK_PATTERN_STEPS);
                for (col = 0; col < 16u; col++) for (q = 0; q < 4u; q++) {
                    uint32_t distance = ((cursor + col) * 4u + q + len * 4u - start * 4u) % (len * 4u);
                    uint32_t expected = distance < tk_piano_gate(gate) ? 7u : 0u;
                    check(((cells[11][col] >> (q * 3u)) & 7u) == expected, "gate geometry matches modular quarter-step oracle");
                }
            }
        }
    }
    for (i = 0; i < 128u; i++) {
        reset(); tk_pattern_record(&s.pattern[1], 63u, i, 100u);
        tk_piano_grid(cells, &s.pattern[1], 64u, 63u, i, 63u);
        row = tk_piano_low(i) + 11u - i;
        check((cells[row][15] & (TK_PIANO_CURSOR | TK_PIANO_ONSET | TK_PIANO_HEAD)) == (TK_PIANO_CURSOR | TK_PIANO_ONSET | TK_PIANO_HEAD), "all 128 pitches map to valid rows including C-1/G9");
    }
    reset(); draw_piano(&s, &t); pixels = host_lcd_pixels;
    for (i = 0; i < 300u; i++) {
        uint32_t previous = host_lcd_pixels;
        if (!tk_piano_upload()) break;
        check(host_lcd_pixels - previous <= 192u, "incremental upload at most 192 pixels");
    }
    check(i < 300u && host_lcd_pixels > pixels, "renderer converges");
    pixels = host_lcd_pixels; draw_piano(&s, &t);
    check(!tk_piano_upload() && host_lcd_pixels == pixels, "unchanged page does no LCD transfers");
    check(!tk_knob_continuous_field(&s, TK_PAGE_PIANO, 0u), "duration stays one detent per unit");
    /* A playing bank change queues, and note deletion cannot cancel it. */
    reset(); t.running = 1u; tk_piano_button(&s, &t, 1u);
    check(tk_piano_encoder(&s, &t, 5u, 1) == 5 && tk_banks.pending[1] == 1u && tk_banks.active[1] == 0u,
        "bank navigation retains quantized launch while playing");
    tk_piano_button(&s, &t, 0u);
    check(tk_banks.pending[1] == 1u && t.running, "single-note deletion preserves pending bank and transport");
    tk_piano_button(&s, &t, 1u); t.selected = 0u;
    tk_piano_encoder(&s, &t, 4u, 1);
    check(t.selected == 1u && tk_banks.pending[1] == 1u, "track navigation returns to original bank request");
    /* Inspect native-sized pages too: endpoints, capacity and guarded modes. */
    reset(); tk_piano.pitch = 0u; tk_piano_button(&s, &t, 1u); shot("piano-low.ppm");
    tk_piano.pitch = 127u; t.edit_step = 63u; tk_piano.duration = 255u; tk_piano_button(&s, &t, 1u); shot("piano-high-wrap.ppm");
    reset(); for (i = 0; i < 4u; i++) { tk_piano.pitch = (uint8_t)(60u + i); tk_piano_button(&s, &t, 1u); }
    tk_piano.pitch = 64u; tk_piano_button(&s, &t, 1u); shot("piano-full.ppm");
    t.recording = 1u; shot("piano-record-locked.ppm");
    t.recording = 0u; tk_banks.importing = 1u; shot("piano-import-locked.ppm");
    tk_banks.importing = 0u; t.running = 1u; t.shown_step = 0u;
    tk_banks.pending[1] = 2u; shot("piano-playing-bank.ppm");
    printf("PASS: %u piano geometry/ownership/render checks (host only)\n", checks);
    return 0;
}
