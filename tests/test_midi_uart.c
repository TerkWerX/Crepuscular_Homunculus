/* SPDX-License-Identifier: GPL-3.0-only */
/* UART stream and DMA/queue checks run the actual rear-input implementation
 * against a fake DMA counter. They do not qualify the physical PH8 circuit. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define TERKINSTEIN_UART_TEST 1
#define RING_PUBLISH() ((void)0)
static uint32_t mock_count, mock_write, mock_length;
static volatile uint8_t *mock_ring;
static void fm1_uart1_midi_init(volatile uint8_t *ring, uint32_t length)
{
    mock_ring = ring;
    mock_length = length;
    mock_count = mock_write = 0;
}
static uint32_t fm1_uart1_rx_take(void)
{
    uint32_t n = mock_count;
    mock_count = 0;
    return n;
}
#include "midi_uart.c"

static uint32_t checks;
static void expect(int ok, const char *what)
{
    checks++;
    if (!ok) { fprintf(stderr, "FAIL: %s\n", what); exit(1); }
}
static uint32_t packet(uint8_t status, uint8_t data1, uint8_t data2)
{
    return (status >= 0xF8u ? 15u : status >> 4) | (uint32_t)status << 8 |
           (uint32_t)data1 << 16 | (uint32_t)data2 << 24;
}
static void expect_none(tk_midi_parser_t *p, uint8_t byte)
{
    uint32_t out = 0xA5A5A5A5u;
    expect(!tk_midi_parser_byte(p, byte, &out), "partial / discarded byte emits no message");
    expect(out == 0xA5A5A5A5u, "no message leaves packet destination untouched");
}
static void expect_packet(tk_midi_parser_t *p, uint8_t byte, uint32_t want)
{
    uint32_t out = 0;
    expect(tk_midi_parser_byte(p, byte, &out), "complete message emits once");
    expect(out == want, "decoded message has exact status, data and length");
}

static void channel_messages(void)
{
    tk_midi_parser_t p;
    uint32_t status, value;
    memset(&p, 0, sizeof p);
    for (status = 0x80u; status <= 0xEFu; status++) {
        uint32_t one = (status & 0xE0u) == 0xC0u;
        expect_none(&p, (uint8_t)status);
        for (value = 0; value < 128u; value += 17u) {
            if (!one) expect_none(&p, (uint8_t)value);
            expect_packet(&p, (uint8_t)(127u - value),
                          packet((uint8_t)status, (uint8_t)(one ? 127u - value : value),
                                 (uint8_t)(one ? 0u : 127u - value)));
        }
    }
    expect_none(&p, 0x90u);
    expect_none(&p, 60u);
    expect_packet(&p, 0u, packet(0x90u, 60u, 0u)); /* keep zero velocity for app note-off handling */
    expect_none(&p, 64u);
    expect_none(&p, 0x81u);                       /* replacement status drops unfinished note */
    expect_none(&p, 64u);
    expect_packet(&p, 127u, packet(0x81u, 64u, 127u));
    expect(p.interrupted == 1u, "new non-realtime status counts the interrupted message");
}

static void realtime_interleaving(void)
{
    tk_midi_parser_t p;
    uint32_t status, realtime, position;
    memset(&p, 0, sizeof p);
    for (status = 0x80u; status <= 0xEFu; status++) {
        for (realtime = 0xF8u; realtime <= 0xFFu; realtime++) {
            for (position = 0; position < 3u; position++) {
                uint32_t one = (status & 0xE0u) == 0xC0u;
                uint32_t emitted = realtime == 0xF8u || realtime == 0xFAu || realtime == 0xFBu ||
                                   realtime == 0xFCu || realtime == 0xFFu;
                expect_none(&p, (uint8_t)status);
                if (position) {
                    if (one) expect_packet(&p, 23u, packet((uint8_t)status, 23u, 0u));
                    else expect_none(&p, 23u);
                }
                if (position == 2u && !one) expect_packet(&p, 77u, packet((uint8_t)status, 23u, 77u));
                if (emitted) expect_packet(&p, (uint8_t)realtime, packet((uint8_t)realtime, 0, 0));
                else expect_none(&p, (uint8_t)realtime);
                if (!position) {
                    if (one) expect_packet(&p, 23u, packet((uint8_t)status, 23u, 0u));
                    else expect_none(&p, 23u);
                }
                if (!one && position != 2u) expect_packet(&p, 77u, packet((uint8_t)status, 23u, 77u));
                if (!one) expect_none(&p, 25u);
                expect_packet(&p, 99u, packet((uint8_t)status, (uint8_t)(one ? 99u : 25u), (uint8_t)(one ? 0u : 99u)));
            }
        }
    }
}

static void system_messages(void)
{
    tk_midi_parser_t p;
    uint32_t status, i;
    static const uint8_t updater[] = {0xF0u, 0x22u, 0x24u, 0x35u, 0x7Du, 0xF7u,
                                     0xF0u, 0x22u, 0x24u, 0x35u, 0x7Fu, 0xF7u};
    memset(&p, 0, sizeof p);
    for (status = 0xF1u; status <= 0xF7u; status++) {
        uint32_t length = status == 0xF2u ? 2u : status == 0xF1u || status == 0xF3u ? 1u : 0u;
        expect_none(&p, 0x90u);
        expect_none(&p, 64u);
        expect_none(&p, (uint8_t)status);
        expect(p.need == length && p.status == 0u && !p.sysex, "system common has exact length and cancels running status");
        for (i = 0; i < length; i++) {
            expect_packet(&p, 0xF8u, packet(0xF8u, 0, 0));
            expect_none(&p, (uint8_t)(5u + i));
        }
        expect(!p.need, "system common completes at its declared length");
        expect_none(&p, 67u);
        expect_none(&p, 101u);
        expect_none(&p, 0xD4u);
        expect_packet(&p, 80u, packet(0xD4u, 80u, 0));
    }
    for (i = 0; i < sizeof updater; i++) expect_none(&p, updater[i]);
    expect_none(&p, 64u);
    expect_none(&p, 100u);
    expect_none(&p, 0xF0u);
    for (i = 0; i < 4096u; i++) {
        expect_none(&p, (uint8_t)(i & 127u));
        if (i % 113u == 0u) expect_packet(&p, 0xFAu, packet(0xFAu, 0, 0));
    }
    expect_none(&p, 0xF0u);                       /* fresh SysEx replaces a truncated one */
    expect_none(&p, 12u);
    expect_none(&p, 0xB2u);                       /* a channel status recovers even without EOX */
    expect_none(&p, 64u);
    expect_packet(&p, 0u, packet(0xB2u, 64u, 0));
    tk_midi_parser_reset(&p);
    expect_none(&p, 64u);
    expect_none(&p, 0u);
    expect_none(&p, 0xE1u);
    expect_none(&p, 0u);
    expect_packet(&p, 64u, packet(0xE1u, 0u, 64u));
}

static void reset_driver(void)
{
    memset(&um, 0, sizeof um);
    memset((void *)um_ring, 0, sizeof um_ring);
    memset(um_queue, 0, sizeof um_queue);
    uart_midi_init();
    expect(mock_ring == um_ring && mock_length == UM_RING, "DMA init uses expected aligned ring and capacity");
    expect(((uintptr_t)mock_ring & 15u) == 0u, "DMA ring has required 16-byte alignment");
}
static void dma_write(uint8_t byte)
{
    mock_ring[mock_write++ & (UM_RING - 1u)] = byte;
    mock_count++;
}
static void drain_expect(uint32_t want)
{
    uint32_t out = 0;
    expect(uart_midi_get(&out), "queued rear message is available");
    expect(out == want, "rear DMA produces exact message");
}

static void dma_and_queue(void)
{
    uint32_t i, out = 0xDEADBEEFu;
    reset_driver();
    dma_write(0x90u); dma_write(60u); dma_write(100u);
    uart_midi_poll();
    drain_expect(packet(0x90u, 60u, 100u));
    expect(!uart_midi_get(&out) && out == 0xDEADBEEFu, "empty queue leaves destination untouched");
    for (i = 0; i < 2000u; i++) {
        dma_write((uint8_t)(i & 127u)); dma_write((uint8_t)((i * 13u) & 127u));
        uart_midi_poll();
        drain_expect(packet(0x90u, (uint8_t)(i & 127u), (uint8_t)((i * 13u) & 127u)));
    }
    expect(!um.overflow && um.rd == (mock_write & (UM_RING - 1u)), "DMA and event queue wrap without losing running status");

    reset_driver();
    dma_write(0xD0u);                          /* fastest channel-message burst: one running-status byte/event */
    for (i = 0; i < 256u; i++) {
        dma_write((uint8_t)(i & 127u));
        uart_midi_poll();                     /* main is busy drawing; TIMER5 continues receiving */
    }
    expect(!um.overflow && um.write - um.read == 256u,
           "queue survives more than a full-screen wire interval of continuous running pressure");
    for (i = 0; i < 256u; i++) drain_expect(packet(0xD0u, (uint8_t)(i & 127u), 0u));
    expect(!uart_midi_get(&out), "full pressure burst drains without missing or extra events");

    reset_driver();
    for (i = 0; i < UM_RING; i++) dma_write(0xFEu);
    uart_midi_poll();
    expect(um.pending == UM_RING - UM_BYTE_BUDGET && um.parser.bytes == UM_BYTE_BUDGET,
           "one timer pass respects the fixed byte budget");
    for (i = 0; i < UM_RING / UM_BYTE_BUDGET - 1u; i++) uart_midi_poll();
    expect(!um.pending && um.parser.bytes == UM_RING && !um.overflow, "deferred complete DMA ring drains without false overflow");
    expect(!uart_midi_overflow_take(), "no overflow acknowledgment is a no-op");

    reset_driver();
    dma_write(0x90u); dma_write(60u); dma_write(100u);
    uart_midi_poll();                         /* undrained note-on must be discarded on later loss */
    for (i = 0; i < UM_RING + 17u; i++) dma_write(0x7Fu);
    uart_midi_poll();
    expect(um.overflow && um.dropped_bytes == 17u + UM_BYTE_BUDGET, "DMA loss signals fault and suppresses stale data");
    expect(!uart_midi_get(&out), "faulted queue cannot replay stale note-ons");
    dma_write(60u); dma_write(0u);             /* also flush bytes arriving after fault but before acknowledgment */
    expect(uart_midi_overflow_take(), "main acknowledges one DMA loss");
    expect(!um.overflow && !um.pending && um.read == um.write && um.rd == (mock_write & (UM_RING - 1u)),
           "acknowledgment realigns DMA, drops old events and resets status");
    dma_write(62u); dma_write(101u); uart_midi_poll();
    expect(!uart_midi_get(&out), "lost running status cannot manufacture a note");
    dma_write(0x91u); dma_write(62u); dma_write(101u); uart_midi_poll();
    drain_expect(packet(0x91u, 62u, 101u));

    reset_driver();
    for (i = 0; i <= UM_QUEUE; i++) {
        dma_write(0x90u); dma_write((uint8_t)(i & 127u)); dma_write(100u);
        uart_midi_poll();
    }
    expect(um.overflow && um.dropped_messages == 1u, "event queue overflow reports a lost message");
    expect(!uart_midi_get(&out), "queue overflow suppresses stale queued notes");
    expect(uart_midi_overflow_take() && um.read == um.write, "queue loss acknowledgment discards complete backlog");
    dma_write(0x80u); dma_write(60u); dma_write(0u); uart_midi_poll();
    drain_expect(packet(0x80u, 60u, 0u));
    expect(!uart_midi_get(&out) && !uart_midi_overflow_take(), "recovered queue drains normally");

    reset_driver();
    um.write = um.read = 0xFFFFFFF0u;
    for (i = 0; i < 128u; i++) {
        dma_write(0xD0u); dma_write((uint8_t)(i & 127u)); uart_midi_poll();
        drain_expect(packet(0xD0u, (uint8_t)(i & 127u), 0u));
    }
    expect(!um.overflow && um.read == um.write, "SPSC indexes safely wrap uint32_t");
}

static void arbitrary_bytes(void)
{
    tk_midi_parser_t p;
    uint32_t i, random = 0xABCD1234u;
    memset(&p, 0, sizeof p);
    for (i = 0; i < 100000u; i++) {
        uint32_t out = 0, status, cin, one;
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        if (!tk_midi_parser_byte(&p, (uint8_t)random, &out)) continue;
        status = (out >> 8) & 255u;
        cin = out & 15u;
        one = cin == 12u || cin == 13u;
        expect((status >= 0x80u && status < 0xF0u && cin == status >> 4) ||
               (cin == 15u && (status == 0xF8u || status == 0xFAu || status == 0xFBu || status == 0xFCu || status == 0xFFu)),
               "arbitrary bytes emit only valid channel or supported realtime packets");
        expect(!(out & 0x80800000u), "emitted channel data bytes are always seven-bit");
        if (one || cin == 15u) expect(!(out & 0xFF000000u), "one-byte / realtime packet zeroes unused data");
    }
    expect(p.bytes == 100000u, "parser work is one bounded step per supplied byte");
}

int main(void)
{
    channel_messages();
    realtime_interleaving();
    system_messages();
    dma_and_queue();
    arbitrary_bytes();
    printf("MIDI UART checks passed: %u\n", checks);
    return 0;
}
