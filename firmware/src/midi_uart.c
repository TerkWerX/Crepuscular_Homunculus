/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Rear TRS MIDI receive path, derived from Felucca's PH8/UART1 DMA driver.
 * A dedicated queue preserves USB/TRS identity and producer ownership. The
 * timer polls it at 2 kHz except while nested in app_audio; each pass processes
 * at most 32 bytes. This integration still needs TERKINSTEIN hardware tests. */
#ifndef TERKINSTEIN_UART_TEST
#include "../hal/fm1_uart.h"
#endif
#include "midi_uart_parser.h"

#define UM_RING 256u
/* A full LCD repaint takes about 77 ms of wire time. Keep enough event slots
 * for 256 one-byte running-status pressure messages (~82 ms), or ~246 ms of
 * full three-byte messages. DMA continues being drained by TIMER5 meanwhile. */
#define UM_QUEUE 256u
#define UM_BYTE_BUDGET 32u
#ifdef _MSC_VER
__declspec(align(16)) static volatile uint8_t um_ring[UM_RING];
#else
static volatile uint8_t um_ring[UM_RING] __attribute__((aligned(16)));
#endif
static uint32_t um_queue[UM_QUEUE];
static struct {
    uint32_t rd, pending;
    volatile uint32_t write, read;
    volatile uint32_t dropped_bytes, dropped_messages;
    volatile uint8_t overflow;
    tk_midi_parser_t parser;
} um;

static void uart_midi_init(void)                 /* normal app boot only, before TIMER5/PORTH scanning */
{
    fm1_uart1_midi_init(um_ring, UM_RING);
}

static void uart_midi_fault(void)
{
    tk_midi_parser_reset(&um.parser);
    RING_PUBLISH();
    um.overflow = 1;
}

static void uart_midi_take(uint32_t count)
{
    uint32_t budget = UM_BYTE_BUDGET;
    um.pending += count;
    if (um.pending > UM_RING) {
        uint32_t lost = um.pending - UM_RING;
        um.dropped_bytes += lost;
        um.rd = (um.rd + lost) & (UM_RING - 1u);
        um.pending = UM_RING;
        uart_midi_fault();
    }
    while (um.pending && budget--) {
        uint32_t packet;
        uint8_t byte = um_ring[um.rd];
        um.rd = (um.rd + 1u) & (UM_RING - 1u);
        um.pending--;
        if (um.overflow) {
            um.dropped_bytes++;
            continue;                          /* discard stale bytes until main acknowledges panic */
        }
        if (tk_midi_parser_byte(&um.parser, byte, &packet)) {
            if (um.write - um.read >= UM_QUEUE) {
                um.dropped_messages++;
                uart_midi_fault();
            } else {
                um_queue[um.write % UM_QUEUE] = packet;
                RING_PUBLISH();
                um.write++;
            }
        }
    }
}

static void uart_midi_poll(void)
{
    uart_midi_take(fm1_uart1_rx_take());
}

static int uart_midi_get(uint32_t *packet)       /* main owns the consumer index */
{
    if (um.overflow || um.read == um.write) return 0;
    *packet = um_queue[um.read % UM_QUEUE];
    RING_PUBLISH();
    um.read++;
    return 1;
}

/* Main calls with interrupts disabled, then silences this port's owners. Drop
 * all pre-fault messages and unread DMA bytes: replaying an old note-on after
 * a lost note-off could recreate a hanging note. */
static int uart_midi_overflow_take(void)
{
    uint32_t unread;
    if (!um.overflow) return 0;
    unread = um.pending + fm1_uart1_rx_take();
    um.rd = (um.rd + unread) & (UM_RING - 1u);
    um.dropped_bytes += unread;
    um.pending = 0;
    um.read = um.write;
    tk_midi_parser_reset(&um.parser);
    RING_PUBLISH();
    um.overflow = 0;
    return 1;
}
