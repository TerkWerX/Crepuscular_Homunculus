/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual-app incremental paint against a forced complete redraw of the same
 * state. Pixel traffic is host evidence, not measured target SPI latency. */
#define TERKINSTEIN_HOST 1
#define main tk_display_host_main
#include "../firmware/src/terkinstein.c"
#undef main

static uint32_t checks;
static uint16_t painted[240u * 240u];
static tk_settings_t fixture;
static tk_transport_t clock_state;
static void expect(int good, const char *why)
{
    checks++;
    if (!good) { fprintf(stderr, "FAIL display check %u: %s\n", checks, why); exit(1); }
}
static uint32_t seq_paint(void)
{
    uint32_t pixels;
    host_lcd_pixels = 0; draw_seq(&fixture, &clock_state); pixels = host_lcd_pixels;
    memcpy(painted, fb, sizeof fb);
    memset(&seq_drawn, 0, sizeof seq_drawn); draw_seq(&fixture, &clock_state);
    if (memcmp(painted, fb, sizeof fb)) {
        uint32_t at;
        for (at = 0; at < 240u * 240u; at++) if (painted[at] != fb[at]) {
            fprintf(stderr, "SEQ pixel mismatch at %u,%u: incremental %04X full %04X\n",
                at % 240u, at / 240u, painted[at], fb[at]); break;
        }
    }
    expect(!memcmp(painted, fb, sizeof fb), "incremental SEQ matches complete same-state repaint");
    return pixels;
}
static uint32_t diagnostic_paint(void)
{
    uint32_t pixels;
    host_lcd_pixels = 0; draw_diagnostics(); pixels = host_lcd_pixels;
    memcpy(painted, fb, sizeof fb); diagnostics_valid = 0; draw_diagnostics();
    expect(!memcmp(painted, fb, sizeof fb), "incremental GLO matches complete same-state repaint");
    return pixels;
}
int main(void)
{
    uint32_t i, pixels, previous;
    memset(&clock_state, 0, sizeof clock_state); settings_defaults(&fixture);
    tk_banks_init(&fixture, &clock_state); tk_init();
    draw_header(TK_PAGE_SEQ);
    expect(seq_paint() > 30000u, "SEQ first visit paints its complete contents");
    expect(!seq_paint(), "unchanged SEQ does no LCD work");
    fixture.p.fracture++;
    expect(!seq_paint(), "live timbre-only edits do no SEQ LCD work");
    fixture.track[2].p.shade++;
    expect(!seq_paint(), "captured timbre-only edits do no SEQ LCD work");
    fixture.tempo++; fixture.ghost_mask = 21;
    expect(seq_paint() == 1536u, "BPM and MASK redraw only two value boxes");
    fixture.track_mute = 1u;
    expect(seq_paint() == 7200u, "mute redraws only its lane");
    clock_state.selected = 1u;
    expect(seq_paint() == 14400u, "selection redraws only old and new lane");
    fixture.length[2] = 64;
    expect(seq_paint() == 7200u, "length redraws only its lane");
    tk_banks.pending[3] = 1;
    expect(seq_paint() == 7200u, "queued bank redraws only its label's lane");
    tk_banks.active[3] = 1; tk_banks.pending[3] = TK_BANK_NONE;
    expect(seq_paint() == 7200u, "active bank redraws only its lane");
    clock_state.editing = 1;
    /* Existing 0.22 and current layouts: one 7,200-pixel lane plus three
     * 224x16 status/help boxes. Same-state pixel parity remains exact. */
    expect(seq_paint() == 17952u, "entry mode repaints status/help and focused lane only");
    clock_state.edit_step = 7;
    expect(seq_paint() == 7200u, "edit cursor redraws its lane");
    tk_pattern_record(&fixture.pattern[1], 7, 60, 100);
    host_lcd_pixels = 0; draw_seq_cell(&fixture, &clock_state, 1, 7);
    expect(host_lcd_pixels == 88u, "single recorded cell stays a small partial paint");
    expect(!seq_paint(), "partial cell paint accepts its occupancy cache");
    clock_state.editing = 0; clock_state.mode = TK_MODE_SEQ; clock_state.running = 1;
    expect(seq_paint() > 30000u, "starting transport paints new playheads");
    previous = clock_state.shown_step; clock_state.shown_step = 1;
    host_lcd_pixels = 0; draw_seq_playhead(&fixture, &clock_state, previous, 1);
    expect(host_lcd_pixels == 176u, "ordinary four-lane tick retains small playhead strips");
    expect(!seq_paint(), "partial playhead accepts row cache");
    /* Main leaves a tick for draw_seq when a timbre control changed. */
    clock_state.shown_step = 2; fixture.p.drift++;
    expect(seq_paint() == 176u, "timbre edit coinciding with tick paints only playheads");
    for (i = 0; i < TK_TRACKS; i++) fixture.length[i] = 64;
    seq_paint(); clock_state.shown_step = 15; seq_paint();
    previous = clock_state.shown_step; clock_state.shown_step = 16;
    host_lcd_pixels = 0; draw_seq_playhead(&fixture, &clock_state, previous, 16);
    /* Four rows each repaint a 224x2 head strip, sixteen 11x8 cells,
     * one scar pixel, a 40x16 page label and the next 11x2 playhead. */
    expect(host_lcd_pixels == 10076u, "sixteen-step group transition stays partial");
    expect(!seq_paint(), "group transition retains complete screen parity");
    clock_state.running = 0;
    expect(seq_paint() > 30000u, "stopping removes old playheads");
    draw_header(TK_PAGE_GLO); diagnostic_paint();
    expect(!diagnostic_paint(), "idle GLO redraws no static counters or artwork");
    tk_voice[0].command = TK_GATE;
    expect(diagnostic_paint() == 6656u, "changed voice count redraws only value and glyph strip");
    expect(!diagnostic_paint(), "unchanged active count does no GLO LCD work");
    tk_voice[0].command = 0;
    expect(diagnostic_paint() == 6656u, "voice release updates count and glyph strip");
    draw_header(TK_PAGE_SEQ); expect(seq_paint() > 30000u, "SEQ revisit invalidates all page content");
    draw_header(TK_PAGE_GLO); pixels = diagnostic_paint();
    expect(pixels > 30000u, "GLO revisit invalidates its static page content");
    printf("PASS incremental display: %u parity/traffic checks; no target timing claim\n", checks);
    return 0;
}
