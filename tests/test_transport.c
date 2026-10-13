/* SPDX-License-Identifier: GPL-3.0-only */
/* Deterministic note transport checks; no hardware, audio or wall clock. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "terkinstein_transport.h"

static uint32_t checks;

static void expect(int condition, const char *why)
{
    checks++;
    if (!condition) { fprintf(stderr, "FAIL: %s\n", why); exit(1); }
}

static void expect_next(tk_arp_t *arp, uint32_t mode, uint32_t wanted)
{
    uint8_t note = 255;
    expect(tk_arp_next(arp, mode, &note), "a held note produces a step");
    expect(note == wanted, "arpeggio order follows the chosen mode");
}

static void pattern_checks(void)
{
    tk_pattern_t pattern;
    const tk_pattern_step_t *step;
    uint32_t i;
    tk_pattern_clear(&pattern);
    for (i = 0; i < TK_PATTERN_STEPS; i++)
        expect(tk_pattern_read(&pattern, i)->count == 0, "all 64 steps start empty");
    expect(!tk_pattern_read(&pattern, TK_PATTERN_STEPS), "out-of-range step has no view");
    expect(!tk_pattern_record(&pattern, TK_PATTERN_STEPS, 60, 100), "invalid step is rejected");
    expect(!tk_pattern_record(&pattern, 0, 128, 100), "invalid pitch is rejected");
    expect(!tk_pattern_record(&pattern, 0, 60, 0), "note-off is not recorded");
    expect(tk_pattern_record(&pattern, 0, 67, 80), "first note records");
    expect(tk_pattern_record(&pattern, 0, 60, 90), "second note records");
    expect(tk_pattern_record(&pattern, 0, 72, 200), "high velocity is bounded");
    expect(tk_pattern_record(&pattern, 0, 64, 70), "fourth note records");
    step = tk_pattern_read(&pattern, 0);
    expect(step->count == 4, "step holds four distinct notes");
    expect(step->note[0].pitch == 60 && step->note[1].pitch == 64 &&
           step->note[2].pitch == 67 && step->note[3].pitch == 72,
           "chord capture is pitch sorted independent of press order");
    expect(step->note[3].velocity == 127, "recorded velocity stays in MIDI range");
    expect(tk_pattern_record(&pattern, 0, 67, 110), "duplicate updates in full chord");
    expect(step->count == 4 && step->note[2].velocity == 110,
           "duplicate does not consume a note slot");
    expect(!tk_pattern_record(&pattern, 0, 69, 90), "fifth distinct note is rejected");
    expect(step->count == 4 && step->note[3].pitch == 72,
           "overflow leaves the chord unchanged");
    expect(tk_pattern_record(&pattern, 63, 127, 127), "last step and highest pitch are valid");
    tk_pattern_find(&pattern, 63, 127)->duration_q4 = 255u;
    expect(tk_pattern_gate_q4(tk_pattern_find(&pattern, 63, 127)) == 256u,
           "compact full-loop gate decodes to 64 steps");
    tk_pattern_clear_step(&pattern, 0);
    expect(tk_pattern_read(&pattern, 0)->count == 0, "one step clears");
    expect(tk_pattern_read(&pattern, 63)->count == 1, "other step remains intact");
    tk_pattern_clear(&pattern);
    expect(tk_pattern_read(&pattern, 63)->count == 0, "entire pattern clears");
}

static void arp_order_checks(void)
{
    const uint8_t input[] = {67, 60, 64, 67, 255};
    tk_arp_t arp;
    uint8_t unchanged = 42;
    tk_arp_init(&arp);
    expect(!tk_arp_next(&arp, TK_ARP_UP, &unchanged) && unchanged == 42,
           "empty arp emits nothing and leaves output untouched");
    tk_arp_set_held(&arp, input, 5);
    expect(arp.count == 3 && arp.held[0] == 60 && arp.held[2] == 67,
           "held pitches sort and deduplicate");
    expect_next(&arp, TK_ARP_UP, 60);
    expect_next(&arp, TK_ARP_UP, 64);
    expect_next(&arp, TK_ARP_UP, 67);
    expect_next(&arp, TK_ARP_UP, 60);
    tk_arp_init(&arp);
    tk_arp_set_held(&arp, input, 5);
    expect_next(&arp, TK_ARP_DOWN, 67);
    expect_next(&arp, TK_ARP_DOWN, 64);
    expect_next(&arp, TK_ARP_DOWN, 60);
    expect_next(&arp, TK_ARP_DOWN, 67);
    tk_arp_init(&arp);
    tk_arp_set_held(&arp, input, 5);
    expect_next(&arp, TK_ARP_PINGPONG, 60);
    expect_next(&arp, TK_ARP_PINGPONG, 64);
    expect_next(&arp, TK_ARP_PINGPONG, 67);
    expect_next(&arp, TK_ARP_PINGPONG, 64);
    expect_next(&arp, TK_ARP_PINGPONG, 60);
    expect_next(&arp, TK_ARP_PINGPONG, 64);
}

static void arp_change_checks(void)
{
    const uint8_t first[] = {60, 64, 67};
    const uint8_t changed[] = {67, 72};
    const uint8_t lower[] = {48, 55};
    const uint8_t one[] = {59};
    uint8_t all[TK_ARP_MAX_NOTES];
    uint8_t got = 0;
    uint32_t i;
    tk_arp_t arp;
    tk_arp_init(&arp);
    tk_arp_set_held(&arp, first, 3);
    expect_next(&arp, TK_ARP_UP, 60);
    expect_next(&arp, TK_ARP_UP, 64);
    tk_arp_set_held(&arp, changed, 2);
    expect_next(&arp, TK_ARP_UP, 67);
    expect_next(&arp, TK_ARP_UP, 72);
    tk_arp_set_held(&arp, lower, 2);
    expect_next(&arp, TK_ARP_UP, 48);
    tk_arp_set_held(&arp, one, 1);
    expect_next(&arp, TK_ARP_PINGPONG, 59);
    expect_next(&arp, TK_ARP_PINGPONG, 59);
    tk_arp_set_held(&arp, 0, 0);
    expect(!tk_arp_next(&arp, TK_ARP_UP, &got), "empty held set stops arpeggio");
    tk_arp_set_held(&arp, changed, 2);
    expect_next(&arp, TK_ARP_UP, 67);
    for (i = 0; i < TK_ARP_MAX_NOTES; i++) all[i] = (uint8_t)(127u - i);
    tk_arp_init(&arp);
    tk_arp_set_held(&arp, all, TK_ARP_MAX_NOTES);
    expect(arp.count == 128 && arp.held[0] == 0 && arp.held[127] == 127,
           "all MIDI pitches fit without a count overflow");
    expect_next(&arp, TK_ARP_DOWN, 127);
    expect_next(&arp, TK_ARP_DOWN, 126);
}

static void arp_new_modes_checks(void)
{
    const uint8_t chord[] = {69, 60, 72, 64, 67};
    const uint8_t changed[] = {55, 67, 59};
    const uint8_t pair[] = {60, 67};
    const uint8_t lone[] = {64};
    tk_arp_t arp, repeat;
    uint8_t note, twin, previous = 255;
    uint32_t i, seen = 0;

    tk_arp_init(&arp);
    tk_arp_set_held(&arp, chord, 5);
    expect_next(&arp, TK_ARP_OUTSIDE_IN, 60);
    expect_next(&arp, TK_ARP_OUTSIDE_IN, 72);
    expect_next(&arp, TK_ARP_OUTSIDE_IN, 64);
    expect_next(&arp, TK_ARP_OUTSIDE_IN, 69);
    expect_next(&arp, TK_ARP_OUTSIDE_IN, 67);
    expect_next(&arp, TK_ARP_OUTSIDE_IN, 60);
    tk_arp_set_held(&arp, changed, 3);
    expect_next(&arp, TK_ARP_OUTSIDE_IN, 55);
    expect_next(&arp, TK_ARP_OUTSIDE_IN, 67);
    expect_next(&arp, TK_ARP_OUTSIDE_IN, 59);
    expect_next(&arp, TK_ARP_UP, 67);
    expect_next(&arp, TK_ARP_OUTSIDE_IN, 55);

    tk_arp_init(&arp);
    tk_arp_init(&repeat);
    tk_arp_set_held(&arp, chord, 5);
    tk_arp_set_held(&repeat, chord, 5);
    for (i = 0; i < 80; i++) {
        uint32_t j;
        expect(tk_arp_next(&arp, TK_ARP_RANDOM, &note), "random arp emits held pitch");
        expect(tk_arp_next(&repeat, TK_ARP_RANDOM, &twin) && twin == note,
               "random arp is deterministic from its seed");
        expect(note != previous, "random arp avoids immediate repetition");
        for (j = 0; j < 5 && chord[j] != note; j++) {}
        expect(j < 5, "random arp selects only held pitches");
        seen |= 1u << j;
        previous = note;
    }
    expect(seen == 31u, "random arp can reach every held pitch");
    tk_arp_set_held(&arp, pair, 2);
    for (i = 0; i < 8; i++) {
        expect(tk_arp_next(&arp, TK_ARP_RANDOM, &note), "two-key random arp emits");
        expect(note != previous && (note == 60 || note == 67),
               "two-key random arp alternates without repetition");
        previous = note;
    }
    tk_arp_set_held(&arp, lone, 1);
    expect_next(&arp, TK_ARP_RANDOM, 64);
    expect_next(&arp, TK_ARP_RANDOM, 64);
}

int main(void)
{
    pattern_checks();
    arp_order_checks();
    arp_change_checks();
    arp_new_modes_checks();
    printf("transport: %u checks passed\n", checks);
    return 0;
}
