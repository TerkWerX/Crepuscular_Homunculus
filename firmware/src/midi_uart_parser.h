/* SPDX-License-Identifier: GPL-3.0-only */
/* Portable MIDI 1.0 byte-stream decoder. System-exclusive data never enters
 * the app or the USB updater. Realtime may interrupt any other message. */
#ifndef TK_MIDI_UART_PARSER_H
#define TK_MIDI_UART_PARSER_H
#include <stdint.h>

typedef struct {
    uint32_t bytes, messages, orphan_bytes, interrupted;
    uint8_t status, need, got, first, sysex, common;
} tk_midi_parser_t;

static void tk_midi_parser_reset(tk_midi_parser_t *p)
{
    p->status = p->need = p->got = p->first = p->sysex = p->common = 0;
}

/* 1 = one complete USB-MIDI-shaped packet in *packet; otherwise unchanged.
 * Running status is retained only for channel voice messages. */
static int tk_midi_parser_byte(tk_midi_parser_t *p, uint8_t byte, uint32_t *packet)
{
    uint32_t data1, data2;
    p->bytes++;
    if (byte >= 0xF8u) {
        if (byte == 0xF8u || byte == 0xFAu || byte == 0xFBu || byte == 0xFCu || byte == 0xFFu) {
            *packet = 0xFu | (uint32_t)byte << 8;
            p->messages++;
            return 1;
        }
        return 0;                              /* undefined / active sensing: ignored */
    }
    if (byte & 0x80u) {
        if (p->got) p->interrupted++;
        tk_midi_parser_reset(p);
        if (byte < 0xF0u) {
            p->status = byte;
            p->need = (byte & 0xE0u) == 0xC0u ? 1u : 2u;
        } else if (byte == 0xF0u) {
            p->sysex = 1;
        } else {
            p->common = 1;
            p->need = byte == 0xF2u ? 2u : byte == 0xF1u || byte == 0xF3u ? 1u : 0u;
        }
        return 0;                              /* every system-common status cancels running status */
    }
    if (p->sysex) return 0;
    if (!p->need) {
        p->orphan_bytes++;
        return 0;
    }
    if (p->need == 2u && !p->got) {
        p->first = byte;
        p->got = 1;
        return 0;
    }
    data1 = p->need == 2u ? p->first : byte;
    data2 = p->need == 2u ? byte : 0u;
    p->got = 0;
    if (p->common) {
        p->need = 0;                            /* consume exactly F1/F2/F3's length, then wait for status */
        return 0;
    }
    *packet = (p->status >> 4) | (uint32_t)p->status << 8 | data1 << 16 | data2 << 24;
    p->messages++;
    return 1;
}
#endif
