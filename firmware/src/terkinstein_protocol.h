/* SPDX-License-Identifier: GPL-3.0-only */
/* TERKINSTEIN application MIDI protocol. No hardware, allocation or DSP. */
#ifndef TERKINSTEIN_PROTOCOL_H
#define TERKINSTEIN_PROTOCOL_H
#include <stdint.h>

#define TK_REMOTE_VERSION 1u
#define TK_REMOTE_PAYLOAD_MAX 80u
#define TK_REMOTE_DATA_MAX (9u + TK_REMOTE_PAYLOAD_MAX)
#define TK_REMOTE_WIRE_MAX (2u + TK_REMOTE_DATA_MAX)
#define TK_REMOTE_REQUEST 0u
#define TK_REMOTE_REPLY 1u
/* Only an explicitly prepared specialist app defines this header. Ordinary
 * Crepuscular Homunculus builds retain the original namespace and unsupported 0x3D. */
#define TK_INSTRUMENT_NAMESPACE_A 0x54u
#define TK_INSTRUMENT_NAMESPACE_B 0x4Bu
typedef char tk_instrument_namespace_safe[(TK_INSTRUMENT_NAMESPACE_A <= 127u && TK_INSTRUMENT_NAMESPACE_B <= 127u) ? 1 : -1];
enum {
    TK_REMOTE_HELLO = 0x01, TK_REMOTE_STATE = 0x02, TK_REMOTE_STEP = 0x03,
    TK_REMOTE_FM = 0x04, TK_REMOTE_LOAD = 0x05, TK_REMOTE_TRACK_MACROS = 0x06,
    TK_REMOTE_BANK_STATE = 0x07, TK_REMOTE_BANK_STEP = 0x08, TK_REMOTE_TRACK_PATCH = 0x09,
    TK_REMOTE_SCREEN_SUBSCRIBE = 0x0A, TK_REMOTE_SCREEN_TILE = 0x0B,
    TK_REMOTE_UI_STATE = 0x0C, TK_REMOTE_METRONOME = 0x0D,
    TK_REMOTE_PANEL_ACTIVITY = 0x0E, TK_REMOTE_PAD_ROUTE = 0x0F,
    TK_REMOTE_SET_STEP = 0x10, TK_REMOTE_TRACK = 0x11,
    TK_REMOTE_LENGTH = 0x12, TK_REMOTE_MUTE = 0x13,
    TK_REMOTE_TEMPO = 0x14, TK_REMOTE_TRANSPORT = 0x15,
    TK_REMOTE_LIVE_PRESET = 0x16, TK_REMOTE_TRACK_PRESET = 0x17,
    TK_REMOTE_CAPTURE = 0x18, TK_REMOTE_MACRO = 0x19,
    TK_REMOTE_RECORD = 0x1A, TK_REMOTE_OPERATOR = 0x1B,
    TK_REMOTE_FM_GLOBAL = 0x1C, TK_REMOTE_VOICE_MODE = 0x1D,
    TK_REMOTE_SET_TRACK_MACRO = 0x1E,
    TK_REMOTE_SELECT_BANK = 0x1F, TK_REMOTE_SET_BANK_STEP = 0x20,
    TK_REMOTE_SET_BANK_LENGTH = 0x21, TK_REMOTE_COPY_BANK = 0x22, TK_REMOTE_CLEAR_BANK = 0x23,
    TK_REMOTE_SONG_BEGIN = 0x24, TK_REMOTE_SONG_STEP = 0x25,
    TK_REMOTE_SONG_LENGTH = 0x26, TK_REMOTE_SONG_PATCH = 0x27,
    TK_REMOTE_SONG_GLOBAL = 0x28, TK_REMOTE_SONG_COMMIT = 0x29, TK_REMOTE_SONG_ABORT = 0x2A,
    TK_REMOTE_SET_METRONOME = 0x2B, TK_REMOTE_SET_PAD_ROUTE = 0x2C,
    TK_REMOTE_SOLO = 0x2D,
    TK_REMOTE_SECTION_MUTES = 0x2E, TK_REMOTE_SET_SECTION_MUTE = 0x2F,
    TK_REMOTE_DRUM_MIX = 0x30, TK_REMOTE_SET_DRUM_MIX = 0x31,
    TK_REMOTE_ARR_STEP = 0x32, TK_REMOTE_SET_ARR_STEP = 0x33,
    TK_REMOTE_SCENE = 0x34, TK_REMOTE_SET_SCENE = 0x35, TK_REMOTE_LAUNCH_SCENE = 0x36,
    TK_REMOTE_FILL = 0x37, TK_REMOTE_ARR_STATE = 0x38, TK_REMOTE_AUTO_ARM = 0x39,
    TK_REMOTE_SONG_ARR_STEP = 0x3A, TK_REMOTE_SONG_DRUM_MIX = 0x3B, TK_REMOTE_SONG_SCENE = 0x3C,
    TK_REMOTE_INSTRUMENT = 0x3D,
    TK_REMOTE_SYNC = 0x3E, TK_REMOTE_FIRE_ROUTE = 0x3F, TK_REMOTE_KEYBOARD_ROUTE = 0x40,
    TK_REMOTE_PANEL_INPUT = 0x41, TK_REMOTE_SWING = 0x42
};
enum {
    TK_REMOTE_OK = 0, TK_REMOTE_BAD_FRAME = 1, TK_REMOTE_BAD_ARGUMENT = 2,
    TK_REMOTE_BUSY = 3, TK_REMOTE_UNSUPPORTED = 4, TK_REMOTE_ID_CONFLICT = 5
};

typedef struct {
    uint8_t direction, command, id, length;
    const uint8_t *payload;
} tk_remote_message_t;

/* Frame data excludes F0/F7. A namespace match deliberately ignores the
 * version, so unsupported versions cannot fall through to the updater. */
static int tk_remote_active_namespace(const uint8_t *data, uint32_t n)
{
    return data && n >= 3u && data[0] == 0x7Du &&
        data[1] == TK_INSTRUMENT_NAMESPACE_A && data[2] == TK_INSTRUMENT_NAMESPACE_B;
}
static int tk_remote_namespace(const uint8_t *data, uint32_t n)
{
    /* A specialist also quarantines the reserved original namespace in the
     * app inbox. Its commands are foreign to decode and never reach OTA. */
    return tk_remote_active_namespace(data, n) ||
        (data && n >= 3u && data[0] == 0x7Du && data[1] == 0x54u && data[2] == 0x4Bu);
}
/* 0 foreign, -1 malformed, -2 unsupported version, 1 request, 2 reply.
 * Invalid input leaves the output message untouched. */
static int tk_remote_decode(const uint8_t *data, uint32_t n, tk_remote_message_t *message)
{
    uint32_t i, sum = 0;
    tk_remote_message_t next;
    if (!tk_remote_active_namespace(data, n)) return 0;
    if (n < 9u || n > TK_REMOTE_DATA_MAX || !message) return -1;
    for (i = 0; i < n; i++) {
        if (data[i] & 0x80u) return -1;
        sum += data[i];
    }
    if ((sum & 127u) || data[7] > TK_REMOTE_PAYLOAD_MAX || n != 9u + data[7]
        || data[4] > TK_REMOTE_REPLY) return -1;
    if (data[3] != TK_REMOTE_VERSION) return -2;
    next.direction = data[4]; next.command = data[5]; next.id = data[6];
    next.length = data[7]; next.payload = data + 8;
    *message = next;
    return next.direction == TK_REMOTE_REQUEST ? 1 : 2;
}
/* Complete F0/F7 frame, 0 on failure. Destination is untouched on failure. */
static uint32_t tk_remote_encode(uint8_t *wire, uint32_t capacity, uint8_t direction,
                                uint8_t command, uint8_t id,
                                const uint8_t *payload, uint32_t n)
{
    uint32_t i, sum = 0;
    if (!wire || n > TK_REMOTE_PAYLOAD_MAX || capacity < n + 11u
        || direction > TK_REMOTE_REPLY || command > 127u || id > 127u || (n && !payload)) return 0;
    for (i = 0; i < n; i++) if (payload[i] & 0x80u) return 0;
    wire[0] = 0xF0u; wire[1] = 0x7Du;
    wire[2] = TK_INSTRUMENT_NAMESPACE_A; wire[3] = TK_INSTRUMENT_NAMESPACE_B;
    wire[4] = TK_REMOTE_VERSION; wire[5] = direction; wire[6] = command;
    wire[7] = id; wire[8] = (uint8_t)n;
    for (i = 0; i < n; i++) wire[9u + i] = payload[i];
    for (i = 1u; i < n + 9u; i++) sum += wire[i];
    wire[9u + n] = (uint8_t)((128u - (sum & 127u)) & 127u);
    wire[10u + n] = 0xF7u;
    return n + 11u;
}

/* A complete bounded frame must fit the TX ring before any packet publishes.
 * Keeping packet formation portable gives tests the actual CIN framing. */
static uint32_t tk_remote_packet_count(uint32_t wire_length)
{
    return wire_length >= 2u && wire_length <= TK_REMOTE_WIRE_MAX ? (wire_length + 2u) / 3u : 0u;
}
static uint32_t tk_remote_packet(const uint8_t *wire, uint32_t n, uint32_t at)
{
    uint32_t k, cin;
    if (!wire || at >= n || n > TK_REMOTE_WIRE_MAX) return 0;
    k = n - at >= 3u ? 3u : n - at;
    cin = at + k < n ? 4u : 4u + k;
    return cin | (uint32_t)wire[at] << 8 |
        (k > 1u ? (uint32_t)wire[at + 1u] << 16 : 0u) |
        (k > 2u ? (uint32_t)wire[at + 2u] << 24 : 0u);
}
#endif
