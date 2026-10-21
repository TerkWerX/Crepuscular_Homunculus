/* SPDX-License-Identifier: GPL-3.0-only */
/* Main-owned, explicitly leased USB MIDI clock. No DSP, allocation or flash. */
#ifndef TERKINSTEIN_MIDI_CLOCK_H
#define TERKINSTEIN_MIDI_CLOCK_H
#include <stdint.h>
typedef struct {
    uint32_t last_ms;
    uint8_t enabled, running, pulse, due, fault;
} tk_midi_clock_t;
static tk_midi_clock_t tk_midi_clock;
static void tk_clock_disable(void)
{
    tk_midi_clock.enabled = tk_midi_clock.running = tk_midi_clock.due = 0u;
}
static void tk_clock_arm(uint32_t now)
{
    if (!tk_midi_clock.enabled) {
        tk_midi_clock.pulse = tk_midi_clock.due = tk_midi_clock.running = 0u;
        tk_midi_clock.fault = 0u;
    }
    tk_midi_clock.enabled = 1u;
    if (!tk_midi_clock.running) tk_midi_clock.last_ms = now;
}
static int tk_clock_expired(uint32_t now)
{
    if (!tk_midi_clock.enabled) return 0;
    if (tk_midi_clock.fault || now - tk_midi_clock.last_ms > (tk_midi_clock.running ? 500u : 3000u)) {
        tk_clock_disable(); tk_midi_clock.fault = 1u; return 1;
    }
    return 0;
}
static void tk_clock_event(uint8_t status, uint32_t now)
{
    if (!tk_midi_clock.enabled) return;
    if (status == 0xFAu) {
        tk_midi_clock.running = 1u; tk_midi_clock.pulse = tk_midi_clock.due = 0u;
        tk_midi_clock.last_ms = now;
    } else if (status == 0xFBu) {
        tk_midi_clock.running = 1u; tk_midi_clock.last_ms = now;
    } else if (status == 0xFCu) {
        tk_midi_clock.running = tk_midi_clock.due = 0u;
    } else if (status == 0xF8u) {
        tk_midi_clock.last_ms = now;
        if (!tk_midi_clock.running) return;
        if (!tk_midi_clock.pulse) {
            /* Never replay an accumulated burst after a stalled main loop. */
            if (tk_midi_clock.due) { tk_midi_clock.fault = 1u; return; }
            tk_midi_clock.due = 1u;
        }
        tk_midi_clock.pulse = (uint8_t)((tk_midi_clock.pulse + 1u) % 6u);
    }
}
#endif
