/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual application protocol and PCM ownership; no hardware qualification. */
#define TERKINSTEIN_HOST 1
#define main tk_compact_host_main
#include "../firmware/src/terkinstein.c"
#undef main

static uint32_t checks, reply_length;
static uint8_t reply[TK_REMOTE_PAYLOAD_MAX], live_shade, track_shade[TK_TRACKS];
static tk_settings_t fixture, before;
static tk_transport_t transport, transport_before;
static tk_lfo_t live_lfo, track_lfo[TK_TRACKS];
static void check(int ok, const char *why)
{ checks++; if (!ok) { fprintf(stderr, "FAIL compact app: %s\n", why); exit(1); } }
static void reset(void)
{
    uint32_t track;
    settings_defaults(&fixture); memset(&transport, 0, sizeof transport);
    tk_init(); tk_visual_init(); tk_metronome_init(); tk_metronome_clock_init(&metro_clock);
    tk_banks_init(&fixture, &transport); tk_ready = 1; fm1_ms = 1000u;
    memset(&tk_screen, 0, sizeof tk_screen); memset(&tk_screen_pool, 0, sizeof tk_screen_pool);
    tk_screen_set_home(1u);
    live_shade = fixture.p.shade; tk_lfo_init(&live_lfo, fm1_ms);
    for (track = 0; track < TK_TRACKS; track++) {
        track_shade[track] = fixture.track[track].p.shade;
        tk_lfo_init(&track_lfo[track], fm1_ms); publish_track(&fixture, track);
    }
    publish_params(&fixture, live_shade);
}
static uint32_t command(const uint8_t *payload, uint32_t n)
{
    tk_remote_message_t request;
    request.direction = TK_REMOTE_REQUEST; request.command = TK_REMOTE_SCREEN_SUBSCRIBE;
    request.id = 1; request.length = (uint8_t)n; request.payload = payload;
    memset(reply, 0xA5, sizeof reply);
    return tk_remote_command(&request, &fixture, &transport, &live_shade,
        &live_lfo, track_lfo, track_shade, TK_PAGE_PAD, 0u, reply, &reply_length);
}
static void protocol_checks(void)
{
    uint32_t action, i;
    uint8_t request[3], stream_before[sizeof tk_screen];
    reset(); before = fixture; transport_before = transport;
    for (action = 0; action <= 4u; action++) {
        request[0] = (uint8_t)action;
        check(!command(request, 1u) && reply_length == 2u && reply[0] == TK_REMOTE_OK &&
            reply[1] == !!action && !tk_screen.compact, "legacy action and exact ACK remain unchanged");
    }
    request[0] = 3u; request[1] = 1u;
    check(!command(request, 2u) && reply_length == 3u && reply[0] == TK_REMOTE_OK &&
        reply[1] == 1u && reply[2] == 1u && tk_screen.compact,
        "extended advanced refresh explicitly confirms profile one");
    tk_screen_prepare(76u); tk_screen.offset = 5u;
    request[0] = 4u; memcpy(stream_before, &tk_screen, sizeof tk_screen);
    check(!command(request, 2u) && reply_length == 3u && !memcmp(stream_before, &tk_screen, sizeof tk_screen),
        "same-millisecond extended heartbeat preserves immutable transfer exactly");
    for (action = 0; action <= 7u; action++) for (i = 0; i <= 3u; i++) {
        if ((action == 3u || action == 4u) && i == 1u) continue;
        request[0] = (uint8_t)action; request[1] = (uint8_t)i;
        memcpy(stream_before, &tk_screen, sizeof tk_screen);
        check(!command(request, 2u) && reply_length == 1u && reply[0] == TK_REMOTE_BAD_ARGUMENT,
            "wrong action/profile pair rejected strictly");
        check(!memcmp(stream_before, &tk_screen, sizeof tk_screen), "invalid profile cannot change stream ownership");
    }
    for (i = 0; i < 2u; i++) {
        request[0] = 3u; request[1] = 1u; request[2] = 0u;
        memcpy(stream_before, &tk_screen, sizeof tk_screen);
        check(!command(request, i ? 3u : 0u) && reply_length == 1u && reply[0] == TK_REMOTE_BAD_ARGUMENT &&
            !memcmp(stream_before, &tk_screen, sizeof tk_screen), "wrong payload size cannot alter subscription");
    }
    tk_banks.importing = 1u; request[0] = 0u;
    check(!command(request, 1u) && !tk_screen.enabled, "song-transfer unsubscribe remains available while importing");
    request[0] = 3u; request[1] = 1u;
    check(!command(request, 2u) && tk_screen.enabled && tk_screen.compact,
        "profile subscription retains main-owned import behavior without song mutation");
    check(!memcmp(&before, &fixture, sizeof fixture) && !memcmp(&transport_before, &transport, sizeof transport),
        "every profile request preserves complete settings and transport");
}
static void pcm_checks(void)
{
    static int32_t pcm[2][16u * 512u];
    uint32_t pass, block, voice;
    uint8_t request[2] = {3u, 1u};
    for (pass = 0; pass < 2u; pass++) {
        reset(); preset_apply(&fixture, 32u); publish_params(&fixture, fixture.p.shade);
        tk_note_on(48u, 100u, 0u); tk_note_on(55u, 95u, 0u); tk_note_on(60u, 90u, 0u);
        for (block = 0; block < 16u; block++) {
            uint32_t words[TK_VOICES], click;
            for (voice = 0; voice < TK_VOICES; voice++) words[voice] = tk_voice[voice].command;
            click = tk_metronome_command;
            if (pass) {
                command(request, 2u);
                tk_screen_set_home(block % 2u); tk_screen_prepare(76u);
                request[0] = 4u;
            }
            for (voice = 0; voice < TK_VOICES; voice++)
                check(words[voice] == tk_voice[voice].command, "compact preparation never publishes synth commands");
            check(click == tk_metronome_command, "compact preparation never publishes metronome commands");
            app_audio(pcm[pass] + block * 512u, 256u);
        }
    }
    check(!memcmp(pcm[0], pcm[1], sizeof pcm[0]), "actual PCM is identical with compact negotiation and context flips");
}
int main(void)
{
    protocol_checks(); pcm_checks();
    printf("PASS compact screen actual app: %u checks; host only\n", checks);
    return 0;
}
