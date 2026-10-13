/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual application telemetry ownership and portable pulse timing checks.
 * These fixtures do not qualify physical USB or target timing. */
#define TERKINSTEIN_HOST 1
#define main tk_panel_activity_host_main
#include "../firmware/src/terkinstein.c"
#undef main

static uint32_t checks, reply_length;
static uint8_t reply[TK_REMOTE_PAYLOAD_MAX], live_shade, track_shade[TK_TRACKS];
static tk_settings_t fixture, before;
static tk_transport_t transport, transport_before;
static tk_lfo_t live_lfo, track_lfo[TK_TRACKS];

static void check(int condition, const char *why)
{
    checks++;
    if (!condition) { fprintf(stderr, "FAIL panel activity %u: %s\n", checks, why); exit(1); }
}

static void reset_fixture(void)
{
    uint32_t track;
    settings_defaults(&fixture); memset(&transport, 0, sizeof transport);
    tk_init(); tk_visual_init(); tk_metronome_init(); tk_metronome_clock_init(&metro_clock);
    tk_panel_activity_init(&tk_panel_activity); tk_banks_init(&fixture, &transport);
    remote_observed_valid = 0; remote_revision = remote_pattern_revision = 0;
    tk_ready = 1; fm1_ms = 0;
    live_shade = fixture.p.shade; tk_lfo_init(&live_lfo, fm1_ms);
    for (track = 0; track < TK_TRACKS; track++) {
        track_shade[track] = fixture.track[track].p.shade;
        tk_lfo_init(&track_lfo[track], fm1_ms); publish_track(&fixture, track);
    }
    publish_params(&fixture, live_shade);
}

static uint32_t command(uint32_t which, const uint8_t *payload, uint32_t count)
{
    tk_remote_message_t request;
    request.direction = TK_REMOTE_REQUEST; request.command = (uint8_t)which;
    request.id = 1u; request.length = (uint8_t)count; request.payload = payload;
    memset(reply, 0xA5, sizeof reply);
    return tk_remote_command(&request, &fixture, &transport, &live_shade,
        &live_lfo, track_lfo, track_shade, TK_PAGE_PAD, 1u, reply, &reply_length);
}

static void expect_activity(uint32_t buttons, uint32_t notes, uint32_t encoders, uint32_t octave)
{
    uint32_t i, decoded = 0u;
    tk_panel_activity_t activity_before = tk_panel_activity;
    before = fixture; transport_before = transport;
    check(!command(TK_REMOTE_PANEL_ACTIVITY, 0, 0), "panel query reports no change or dirty flags");
    check(reply_length == 12u && reply[0] == TK_REMOTE_OK && reply[1] == 2u,
          "status plus exact eleven-byte version-two activity body");
    check(((uint32_t)reply[2] | (uint32_t)reply[3] << 7) == buttons, "fourteen physical button bits");
    for (i = 0; i < 4u; i++) decoded |= (uint32_t)reply[4u + i] << (7u * i);
    check(decoded == notes, "twenty-seven physical held key bits");
    check(reply[8] == encoders && reply[9] == octave, "raw encoder pulses and biased panel octave");
    check(((uint32_t)reply[10] | (uint32_t)reply[11] << 7) == activity_before.buttons,
          "exact held button mask excludes lingering press pulses");
    check(!(activity_before.buttons & ~buttons), "held buttons remain a subset of visible button activity");
    for (i = 0; i < reply_length; i++) check(reply[i] < 128u, "every reply byte is MIDI safe");
    check(!memcmp(&fixture, &before, sizeof fixture) && !memcmp(&transport, &transport_before, sizeof transport),
          "query preserves settings and transport exactly");
    check(!memcmp(&tk_panel_activity, &activity_before, sizeof activity_before),
          "query does not consume, expire or otherwise mutate activity");
}

static void pulse_checks(void)
{
    uint32_t bit, octave;
    static const uint8_t invalid[] = {0u};
    reset_fixture(); expect_activity(0u, 0u, 0u, 1u);
    check(!command(TK_REMOTE_HELLO, 0, 0) && reply[0] == TK_REMOTE_OK && reply[3] == 127u,
          "HELLO high-byte bit six explicitly advertises panel feedback");
    check(!command(TK_REMOTE_PANEL_ACTIVITY, invalid, 1u) && reply_length == 1u
          && reply[0] == TK_REMOTE_BAD_ARGUMENT, "read-only query rejects request payload");
    tk_banks.importing = 1u;
    expect_activity(0u, 0u, 0u, 1u);
    tk_banks.importing = 0u;
    for (bit = 0; bit < TK_PANEL_BUTTONS; bit++) {
        tk_panel_activity_init(&tk_panel_activity);
        fm1_ms = 1000u + bit * 500u;
        tk_panel_activity_inputs(&tk_panel_activity, 1u << bit, 0u, fm1_ms);
        tk_panel_activity_press(&tk_panel_activity, 1u << bit, fm1_ms);
        expect_activity(1u << bit, 0u, 0u, 1u);
        fm1_ms += 160u;
        tk_panel_activity_inputs(&tk_panel_activity, 0u, 0u, fm1_ms);
        expect_activity(1u << bit, 0u, 0u, 1u);
        expect_activity(1u << bit, 0u, 0u, 1u); /* A second client read cannot eat it. */
        fm1_ms++;
        tk_panel_activity_inputs(&tk_panel_activity, 0u, 0u, fm1_ms);
        expect_activity(0u, 0u, 0u, 1u);
        fm1_ms += 500u;
        tk_panel_activity_inputs(&tk_panel_activity, 1u << bit, 0u, fm1_ms);
        expect_activity(1u << bit, 0u, 0u, 1u); /* Held state does not need fresh press edges. */
    }
    for (bit = 0; bit < TK_PANEL_ENCODERS; bit++) {
        tk_panel_activity_init(&tk_panel_activity); fm1_ms = 1000u;
        tk_panel_activity_encoder(&tk_panel_activity, bit, bit & 1u ? -1 : 1, fm1_ms);
        expect_activity(0u, 0u, 1u << bit, 1u);
        fm1_ms += 160u; expect_activity(0u, 0u, 1u << bit, 1u);
        fm1_ms++; expect_activity(0u, 0u, 0u, 1u);
        tk_panel_activity_inputs(&tk_panel_activity, 0u, 0u, fm1_ms);
        fm1_ms = 1000u; expect_activity(0u, 0u, 0u, 1u); /* Stale pulse cannot revive. */
        tk_panel_activity_encoder(&tk_panel_activity, bit, 0, fm1_ms);
        tk_panel_activity_encoder(&tk_panel_activity, TK_PANEL_ENCODERS, 1, fm1_ms);
        expect_activity(0u, 0u, 0u, 1u);
    }
    for (bit = 0; bit < 27u; bit++) {
        tk_panel_activity_init(&tk_panel_activity);
        tk_panel_activity_inputs(&tk_panel_activity, 0u, 1u << bit, fm1_ms);
        expect_activity(0u, 1u << bit, 0u, 1u);
        tk_panel_activity_inputs(&tk_panel_activity, 0u, 0u, fm1_ms);
        expect_activity(0u, 0u, 0u, 1u); /* Note release has no fake sustain/pulse. */
    }
    for (octave = 0; octave < 5u; octave++) {
        fixture.octave = (int8_t)((int32_t)octave - 2);
        expect_activity(0u, 0u, 0u, octave);
        check(KEY0 + 12 * fixture.octave == 29u + 12u * octave,
              "biased octave converts back to actual panel note base");
    }
    tk_panel_activity_init(&tk_panel_activity); fixture.octave = -1;
    tk_panel_activity_inputs(&tk_panel_activity, ~0u, ~0u, fm1_ms);
    expect_activity(16383u, TK_PANEL_NOTES_MASK, 0u, 1u);
    tk_panel_activity_inputs(&tk_panel_activity, 0u, 0u, fm1_ms);
    fm1_ms = 0xFFFFFFB0u;
    tk_panel_activity_press(&tk_panel_activity, 1u << BTN_REC, fm1_ms);
    tk_panel_activity_encoder(&tk_panel_activity, ENC_KNOB4, 1, fm1_ms);
    fm1_ms += 160u; expect_activity(1u << BTN_REC, 0u, 1u << ENC_KNOB4, 1u);
    fm1_ms++; expect_activity(0u, 0u, 0u, 1u);
    tk_panel_activity_inputs(&tk_panel_activity, 0u, 0u, fm1_ms);
    check(!tk_panel_activity.button_pulses && !tk_panel_activity.encoder_pulses,
          "wrapped pulses expire permanently in the main loop");
}

static void edit_hold_checks(void)
{
    uint32_t edit = 1u << BTN_EDIT, buttons;
    reset_fixture(); fm1_ms = 1000u;
    tk_panel_activity_inputs(&tk_panel_activity, edit, 0u, fm1_ms);
    tk_panel_activity_press(&tk_panel_activity, edit, fm1_ms);
    expect_activity(edit, 0u, 0u, 1u);
    check((reply[10] | (uint32_t)reply[11] << 7) == edit, "physical EDIT hold is exact");
    fm1_ms++;
    tk_panel_activity_inputs(&tk_panel_activity, 0u, 0u, fm1_ms);
    expect_activity(edit, 0u, 0u, 1u);
    check(!reply[10] && !reply[11], "quick EDIT release clears held state after one millisecond");
    fm1_ms += 159u;
    expect_activity(edit, 0u, 0u, 1u);
    check(!reply[10] && !reply[11], "EDIT pulse remains visible without pretending it is held");
    fm1_ms++;
    tk_panel_activity_inputs(&tk_panel_activity, 0u, 0u, fm1_ms);
    expect_activity(0u, 0u, 0u, 1u);
    fm1_ms += 10u;
    buttons = edit | (1u << BTN_FX) | (1u << BTN_REC);
    tk_panel_activity_inputs(&tk_panel_activity, buttons, 0u, fm1_ms);
    tk_panel_activity_press(&tk_panel_activity, buttons, fm1_ms);
    fm1_ms += 1000u;
    tk_panel_activity_inputs(&tk_panel_activity, buttons, 0u, fm1_ms);
    expect_activity(buttons, 0u, 0u, 1u);
    check((reply[10] | (uint32_t)reply[11] << 7) == buttons,
          "simultaneous held EDIT and transport buttons survive pulse expiry");
    tk_panel_activity_inputs(&tk_panel_activity, buttons & ~edit, 0u, fm1_ms);
    expect_activity(buttons & ~edit, 0u, 0u, 1u);
    check(!(reply[10] & edit), "releasing EDIT preserves other physically held buttons");
}

static void pcm_checks(void)
{
    static int32_t baseline[16u * 512u], observed[16u * 512u];
    uint32_t pass, block, voice;
    for (pass = 0; pass < 2u; pass++) {
        reset_fixture(); preset_apply(&fixture, 32u); publish_params(&fixture, fixture.p.shade);
        tk_note_on(48u, 100u, 0u); tk_note_on(55u, 95u, 0u); tk_note_on(60u, 90u, 0u);
        for (block = 0; block < 16u; block++) {
            uint32_t commands[TK_VOICES], word;
            fm1_ms = block * 6u;
            for (voice = 0; voice < TK_VOICES; voice++) commands[voice] = tk_voice[voice].command;
            word = tk_metronome_command;
            if (pass) {
                tk_panel_activity_inputs(&tk_panel_activity, 1u << BTN_ENV, 0x4001u, fm1_ms);
                tk_panel_activity_press(&tk_panel_activity, 1u << BTN_ENV, fm1_ms);
                tk_panel_activity_encoder(&tk_panel_activity, ENC_KNOB1, -1, fm1_ms);
                expect_activity(1u << BTN_ENV, 0x4001u, 1u << ENC_KNOB1, 1u);
            }
            for (voice = 0; voice < TK_VOICES; voice++)
                check(commands[voice] == tk_voice[voice].command, "telemetry cannot publish synth commands");
            check(word == tk_metronome_command, "telemetry cannot publish metronome commands");
            app_audio((pass ? observed : baseline) + block * 512u, 256u);
        }
    }
    check(!memcmp(baseline, observed, sizeof baseline), "actual audio PCM is byte-identical with queries and capture");
}

int main(void)
{
    pulse_checks(); edit_hold_checks(); pcm_checks();
    printf("PASS panel activity app: %u checks\n", checks);
    return 0;
}
