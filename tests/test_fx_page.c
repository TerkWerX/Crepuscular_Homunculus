/* SPDX-License-Identifier: GPL-3.0-only */
/* Exercise actual main-owned FX controls and LCD painting. These host checks
 * do not qualify the physical buttons, SPI latency or target audio deadlines. */
#define TERKINSTEIN_HOST 1
#define main tk_fx_host_main
#include "../firmware/src/terkinstein.c"
#undef main

static uint32_t fx_checks;
static tk_settings_t fixture, before, expected;
static uint16_t incremental_pixels[240u * 240u];
static void fx_expect(int good, const char *why)
{
    fx_checks++;
    if (!good) { fprintf(stderr, "FAIL FX check %u: %s\n", fx_checks, why); exit(1); }
}

static void button_timing(void)
{
    tk_fx_button_t button = {0};
    uint32_t length, tick, start;
    fx_expect(tk_fx_button_event(&button, 1, 0, 0, 100) == 1,
        "legacy one-shot FX edge toggles immediately");
    fx_expect(!button.pending, "legacy edge does not leave a held action pending");
    fx_expect(!tk_fx_button_event(&button, 0, 1, 0, 180),
        "unmatched release cannot toggle FX");
    for (length = 699; length <= 701; length++) {
        memset(&button, 0, sizeof button);
        fx_expect(!tk_fx_button_event(&button, 1, 0, 1, 100),
            "physical press defers FX toggle until duration is known");
        fx_expect(button.pending && !button.long_press, "press records a pending short gesture");
        fx_expect(tk_fx_button_event(&button, 0, 1, 0, 100 + length)
            == (length < 700u ? 1u : 2u),
            "699-ms release toggles; exact 700-ms and 701-ms releases navigate only");
        fx_expect(!tk_fx_button_event(&button, 0, 0, 0, 900),
            "completed release never repeats its action");
    }
    memset(&button, 0, sizeof button);
    tk_fx_button_event(&button, 1, 0, 1, 100);
    for (tick = 101; tick < 800; tick++)
        fx_expect(!tk_fx_button_event(&button, 0, 0, 1, tick),
            "held FX stays neutral before its threshold");
    fx_expect(tk_fx_button_event(&button, 0, 0, 1, 800) == 2,
        "held FX opens once at 700 ms");
    for (tick = 801; tick <= 1600; tick += 79)
        fx_expect(!tk_fx_button_event(&button, 0, 0, 1, tick),
            "long hold cannot repeatedly navigate");
    fx_expect(!tk_fx_button_event(&button, 0, 1, 0, 1700),
        "release following long navigation never toggles the effect");
    fx_expect(!tk_fx_button_event(&button, 1, 0, 1, 1800)
        && tk_fx_button_event(&button, 0, 1, 0, 1850) == 1,
        "a fresh short tap after a long hold still toggles");
    memset(&button, 0, sizeof button);
    start = UINT32_MAX - 300u;
    tk_fx_button_event(&button, 1, 0, 1, start);
    fx_expect(!tk_fx_button_event(&button, 0, 0, 1, start + 699u),
        "clock wrap preserves the pre-threshold hold");
    fx_expect(tk_fx_button_event(&button, 0, 1, 0, start + 700u) == 2,
        "clock wrap preserves exact-threshold navigation");
    fx_expect(!tk_fx_button_event(&button, 0, 0, 0, start + 701u),
        "wrapped long release is consumed once");
    memset(&button, 0, sizeof button);
    tk_fx_button_event(&button, 1, 0, 1, 100);
    fx_expect(tk_fx_button_event(&button, 0, 0, 0, 180) == 1,
        "a held-state release remains usable if its edge was coalesced");
}

enum { FX_FRACTURE, FX_SHADE, FX_MEMORY, FX_DRIFT, FX_FEEDBACK,
       FX_BLOOM, FX_DEPTH, FX_RATE, FX_SHAPE };
static uint8_t *fixture_field(tk_settings_t *s, uint32_t field)
{
    switch (field) {
    case FX_FRACTURE: return &s->p.fracture;
    case FX_SHADE: return &s->p.shade;
    case FX_MEMORY: return &s->memory;
    case FX_DRIFT: return &s->p.drift;
    case FX_FEEDBACK: return &s->fm[0].feedback;
    case FX_BLOOM: return &s->p.bloom;
    case FX_DEPTH: return &s->lfo_depth;
    case FX_RATE: return &s->lfo_rate;
    default: return &s->lfo_shape;
    }
}
static void controls_fixture(uint32_t preset)
{
    uint32_t track;
    settings_defaults(&fixture); preset_apply(&fixture, preset);
    for (track = 0; track < TK_TRACKS; track++) {
        tk_pattern_clear(&fixture.pattern[track]);
        tk_pattern_record(&fixture.pattern[track], 63u, 48u + track, 90u + track);
        fixture.pattern[track].step[63].note[0].duration_q4 = (uint8_t)(251u + track);
        fm_for_preset(&fixture.fm[track + 1u], 32u + track);
    }
    fixture.graft_amount = 57u;
    fixture.fx = 0; fixture.lfo_on = 0;
}
static void expected_bake(void)
{
    expected = before;
    expected.graft_amount = 0;
    track_patch_capture(&expected.graft_base, &before);
    expected.fm[5] = before.fm[0];
}
static void exact_live_patch(void)
{
    tk_track_patch_t captured;
    uint8_t bytes[14];
    bytes[0] = fixture.p.shade; bytes[1] = fixture.p.fracture;
    bytes[2] = fixture.p.drift; bytes[3] = fixture.p.bloom;
    bytes[4] = fixture.preset; bytes[5] = fixture.fx; bytes[6] = fixture.memory;
    bytes[7] = fixture.attack; bytes[8] = fixture.release;
    bytes[9] = fixture.lfo_on; bytes[10] = fixture.lfo_shape;
    bytes[11] = fixture.lfo_rate; bytes[12] = fixture.lfo_depth;
    bytes[13] = PRESETS[fixture.preset].instrument;
    track_patch_capture(&captured, &fixture);
    fx_expect(sizeof captured == sizeof bytes && !memcmp(&captured, bytes, sizeof bytes),
        "edited live fields retain the complete fourteen-byte captured patch order");
    /* This is the unchanged main-loop refresh after an edit has baked GRAFT. */
    track_patch_capture(&fixture.graft_base, &fixture); fixture.fm[5] = fixture.fm[0];
    fx_expect(!memcmp(&fixture.graft_base, bytes, sizeof bytes)
        && !memcmp(&fixture.fm[5], &fixture.fm[0], sizeof fixture.fm[0]),
        "the next main pass retains edited live sound as its GRAFT base");
    fx_expect(sizeof fixture == 3700u && fixture.schema == 11u && settings_valid(&fixture),
        "FX controls retain the existing valid schema-eleven settings payload");
}
static void knob_mapping_and_bounds(void)
{
    static const uint8_t preset[4] = {0u, 11u, 32u, 30u};
    static const uint8_t fields[4][4] = {
        {FX_FRACTURE, FX_SHADE, FX_MEMORY, FX_DEPTH},
        {FX_FRACTURE, FX_SHADE, FX_DRIFT, FX_DEPTH},
        {FX_FRACTURE, FX_SHADE, FX_FEEDBACK, FX_DEPTH},
        {FX_BLOOM, FX_FRACTURE, FX_DRIFT, FX_SHADE}
    };
    static const int32_t detents[4] = {7, -7, 32767, -32767};
    uint32_t instrument, knob, pass;
    for (instrument = 0; instrument < 4; instrument++) for (knob = 0; knob < 4; knob++) {
        for (pass = 0; pass < 4; pass++) {
            uint8_t value = pass == 0u ? 71u : pass == 1u ? 57u : pass == 2u ? 127u : 0u;
            controls_fixture(preset[instrument]);
            *fixture_field(&fixture, fields[instrument][knob]) = 64u;
            before = fixture; expected_bake();
            *fixture_field(&expected, fields[instrument][knob]) = value;
            if (!knob) expected.fx = 1;
            if (knob == 3u && instrument < 3u && value) expected.lfo_on = 1;
            fx_expect(tk_fx_knob(&fixture, knob, detents[pass]) == 3,
                "changed FX knob reports consumed and dirty");
            fx_expect(!memcmp(&fixture, &expected, sizeof fixture),
                "knob edits exactly its instrument's live field and bake metadata; other owners survive");
            exact_live_patch();
        }
        for (pass = 0; pass < 3; pass++) {
            controls_fixture(preset[instrument]); fixture.fx = 1; fixture.lfo_on = 1;
            *fixture_field(&fixture, fields[instrument][knob]) = pass == 0u ? 0u : 127u;
            before = fixture;
            fx_expect(tk_fx_knob(&fixture, knob, pass == 0u ? -32767 : pass == 1u ? 32767 : 0) == 1,
                "zero detents and outward bound edits are consumed without dirtying");
            fx_expect(!memcmp(&fixture, &before, sizeof fixture),
                "no-op FX knobs cannot bake an existing GRAFT or change other fields");
        }
    }
    controls_fixture(0); fixture.lfo_on = 1; fixture.lfo_depth = 5;
    fx_expect(tk_fx_knob(&fixture, 3, -32767) == 3
        && !fixture.lfo_depth && fixture.lfo_on,
        "zero motion depth preserves the existing LFO enable setting");
    controls_fixture(30); before = fixture;
    fx_expect(tk_fx_knob(&fixture, 4, 7) == 1
        && !memcmp(&fixture, &before, sizeof fixture), "invalid knob is harmless and consumes no dirty edit");
}
static void rate_and_shape(void)
{
    static const uint8_t presets[3] = {0u, 11u, 32u};
    static const int32_t steps[7] = {-32767, -4, -1, 0, 1, 4, 32767};
    uint32_t instrument, shape, pass;
    for (instrument = 0; instrument < 3; instrument++) {
        for (pass = 0; pass < 4; pass++) {
            int32_t delta = pass == 0u ? 7 : pass == 1u ? -7 : pass == 2u ? 32767 : -32767;
            uint8_t value = pass == 0u ? 71u : pass == 1u ? 57u : pass == 2u ? 127u : 0u;
            controls_fixture(presets[instrument]); fixture.lfo_rate = 64u;
            before = fixture; expected_bake(); expected.lfo_rate = value;
            fx_expect(tk_fx_encoder(&fixture, 0, delta) == 3
                && !memcmp(&fixture, &expected, sizeof fixture),
                "SELECT changes only bounded live motion rate and bake metadata");
        }
        for (shape = 0; shape < 3; shape++) for (pass = 0; pass < 7; pass++) {
            uint8_t next = (uint8_t)(((int32_t)shape + steps[pass] % 3 + 3) % 3);
            controls_fixture(presets[instrument]); fixture.lfo_shape = (uint8_t)shape;
            before = fixture;
            if (next == shape) expected = before;
            else { expected_bake(); expected.lfo_shape = next; }
            fx_expect(tk_fx_encoder(&fixture, 1, steps[pass]) == (next == shape ? 1 : 3),
                "ALGORITHM wraps all three motion shapes and reports only actual changes");
            fx_expect(!memcmp(&fixture, &expected, sizeof fixture),
                "shape wrapping preserves captured sounds, operators and musical settings");
        }
        for (pass = 0; pass < 3; pass++) {
            controls_fixture(presets[instrument]); fixture.lfo_rate = pass ? 127u : 0u;
            before = fixture;
            fx_expect(tk_fx_encoder(&fixture, 0, pass == 0u ? -32767 : pass == 1u ? 32767 : 0) == 1
                && !memcmp(&fixture, &before, sizeof fixture),
                "motion-rate no-op preserves a nonzero GRAFT without dirtying");
        }
    }
    controls_fixture(30); before = fixture;
    fx_expect(tk_fx_encoder(&fixture, 0, 7) == 1
        && tk_fx_encoder(&fixture, 1, -7) == 1
        && !memcmp(&fixture, &before, sizeof fixture),
        "KIT consumes inactive filter-motion encoders without changing sound or GRAFT");
    controls_fixture(0); before = fixture;
    fx_expect(tk_fx_encoder(&fixture, 2, 7) == 1
        && !memcmp(&fixture, &before, sizeof fixture), "invalid FX encoder is harmless");
}

static void graft_effective_rebase(void)
{
    uint32_t mode;
    static const uint8_t presets[3] = {0u, 11u, 32u};
    for (mode = 0; mode < 3; mode++) {
        settings_defaults(&fixture); preset_apply(&fixture, presets[mode]);
        fixture.graft_amount = 64u; graft_apply(&fixture);
        before = fixture; expected_bake();
        /* DARK FILTER is shared by all three non-kit instruments. */
        expected.p.shade = before.p.shade > 7u ? (uint8_t)(before.p.shade - 7u) : 0u;
        fx_expect(tk_fx_knob(&fixture, 1u, -7) == 3
            && !memcmp(&fixture, &expected, sizeof fixture),
            "an actual intermediate GRAFT bakes its effective sound before one FX edit");
        fx_expect(!memcmp(&fixture.fm[5], &before.fm[0], sizeof fixture.fm[0]),
            "GRAFT baking captures current effective FM operators and routing without an endpoint jump");
        exact_live_patch();
    }
}

static uint32_t paint_parity(void)
{
    uint32_t pixels;
    host_lcd_pixels = 0; draw_fx(&fixture); pixels = host_lcd_pixels;
    memcpy(incremental_pixels, fb, sizeof fb);
    /* A clean complete page is the reference, not a repaint over stale pixels. */
    lcd_fill(0, 0, 240, 240, C_BLACK); draw_header(TK_PAGE_FX); draw_fx(&fixture);
    if (memcmp(incremental_pixels, fb, sizeof fb)) {
        uint32_t at;
        for (at = 0; at < 240u * 240u; at++) if (incremental_pixels[at] != fb[at]) {
            fprintf(stderr, "FX pixel mismatch %u,%u: incremental %04X fresh %04X\n",
                at % 240u, at / 240u, incremental_pixels[at], fb[at]); break;
        }
    }
    fx_expect(!memcmp(incremental_pixels, fb, sizeof fb),
        "incremental FX screen exactly matches a fresh complete page");
    return pixels;
}
static void screen_painting(void)
{
    static const uint8_t presets[4] = {0u, 11u, 32u, 30u};
    uint32_t mode, knob, pixels, complete;
    settings_defaults(&fixture); tk_init();
    for (mode = 0; mode < 4; mode++) {
        preset_apply(&fixture, presets[mode]);
        lcd_fill(0, 0, 240, 240, C_BLACK); draw_header(TK_PAGE_FX);
        fx_expect(!tk_fx_drawn_valid, "FX page entry invalidates its draw cache");
        complete = paint_parity();
        fx_expect(complete > 20000u, "initial FX visit paints page labels, values and signal chain");
        fx_expect(!paint_parity(), "unchanged FX page transmits no LCD pixels");
        for (knob = 0; knob < 4; knob++) {
            tk_fx_knob(&fixture, knob, 1);
            pixels = paint_parity();
            fx_expect(pixels && pixels < complete / 2u,
                "a changed FX knob paints a bounded fraction of the complete page");
            fx_expect(!paint_parity(), "knob repaint accepts its new cached value");
        }
        fixture.fx ^= 1u;
        pixels = paint_parity();
        fx_expect(pixels && pixels < complete,
            "CORROSION or BITE bypass updates status and chain without a complete page");
        fixture.lfo_on ^= 1u; paint_parity();
        if (mode < 3u) {
            tk_fx_encoder(&fixture, 0, 1); paint_parity();
            tk_fx_encoder(&fixture, 1, 1); paint_parity();
        }
        fixture.attack++; fixture.release++;
        fx_expect(!paint_parity(), "unshown envelope changes do not redraw FX");
        draw_header(TK_PAGE_ENV); draw_header(TK_PAGE_FX);
        fx_expect(!tk_fx_drawn_valid, "leaving and returning to FX invalidates its page contents");
        fx_expect(paint_parity() > 20000u, "FX revisit paints complete contents after another page");
    }
    /* Presets can also change while the FX page remains open. */
    for (mode = 0; mode < 4; mode++) {
        preset_apply(&fixture, presets[mode]); paint_parity();
        fx_expect(!paint_parity(), "on-page instrument transition keeps a stable complete cache");
    }
}
int main(void)
{
    button_timing(); knob_mapping_and_bounds(); rate_and_shape(); graft_effective_rebase(); screen_painting();
    printf("PASS FX page: %u actual-app timing, controls, ownership and pixel checks; host only\n", fx_checks);
    return 0;
}
