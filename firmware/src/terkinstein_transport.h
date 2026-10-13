/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TERKINSTEIN_TRANSPORT_H
#define TERKINSTEIN_TRANSPORT_H

#include <stdint.h>

/* Main-loop note transport. No audio ownership, allocation, or sample clock.
 * A caller decides when steps advance and which source plays the notes. */
#define TK_PATTERN_STEPS 64u
#define TK_PATTERN_VIEW_STEPS 16u
#define TK_PATTERN_GATE_Q4 (TK_PATTERN_STEPS * 4u)
#define TK_PATTERN_NOTES 4u
#define TK_ARP_MAX_NOTES 128u

#define TK_ARP_UP 0u
#define TK_ARP_DOWN 1u
#define TK_ARP_PINGPONG 2u
#define TK_ARP_OUTSIDE_IN 3u
#define TK_ARP_RANDOM 4u
#define TK_ARP_LAST_MODE TK_ARP_RANDOM

typedef struct {
    uint8_t pitch, velocity;
    /* Quarter-step gate units. Zero preserves the original 75% step pulse
     * for seeded, step-entered and migrated notes. 255 encodes a full 64-step
     * gate (256 quarter-steps), keeping four patterns inside one flash sector. */
    uint8_t duration_q4;
} tk_pattern_note_t;

typedef struct {
    uint8_t count;
    tk_pattern_note_t note[TK_PATTERN_NOTES];
} tk_pattern_step_t;

typedef struct {
    tk_pattern_step_t step[TK_PATTERN_STEPS];
} tk_pattern_t;

static uint32_t tk_pattern_gate_q4(const tk_pattern_note_t *note)
{
    return note->duration_q4 == 255u ? TK_PATTERN_GATE_Q4 : note->duration_q4;
}

/* Notes in each step are sorted by pitch. Re-recording a pitch updates its
 * velocity in place; a fifth distinct pitch is rejected without mutation. */
static void tk_pattern_clear_step(tk_pattern_t *pattern, uint32_t step)
{
    uint32_t i;
    tk_pattern_step_t *target;
    if (!pattern || step >= TK_PATTERN_STEPS) return;
    target = &pattern->step[step];
    target->count = 0;
    for (i = 0; i < TK_PATTERN_NOTES; i++) {
        target->note[i].pitch = 0;
        target->note[i].velocity = 0;
        target->note[i].duration_q4 = 0;
    }
}

static void tk_pattern_clear(tk_pattern_t *pattern)
{
    uint32_t step;
    if (!pattern) return;
    for (step = 0; step < TK_PATTERN_STEPS; step++)
        tk_pattern_clear_step(pattern, step);
}

static int tk_pattern_record(tk_pattern_t *pattern, uint32_t step,
                             uint32_t pitch, uint32_t velocity)
{
    uint32_t at, i;
    tk_pattern_step_t *target;
    if (!pattern || step >= TK_PATTERN_STEPS || pitch > 127u || !velocity)
        return 0;
    if (velocity > 127u) velocity = 127u;
    target = &pattern->step[step];
    for (at = 0; at < target->count && target->note[at].pitch < pitch; at++) {}
    if (at < target->count && target->note[at].pitch == pitch) {
        target->note[at].velocity = (uint8_t)velocity;
        target->note[at].duration_q4 = 0;
        return 1;
    }
    if (target->count >= TK_PATTERN_NOTES) return 0;
    for (i = target->count; i > at; i--)
        target->note[i] = target->note[i - 1u];
    target->note[at].pitch = (uint8_t)pitch;
    target->note[at].velocity = (uint8_t)velocity;
    target->note[at].duration_q4 = 0;
    target->count++;
    return 1;
}

static tk_pattern_note_t *tk_pattern_find(tk_pattern_t *pattern, uint32_t step,
                                          uint32_t pitch)
{
    uint32_t i;
    tk_pattern_step_t *target;
    if (!pattern || step >= TK_PATTERN_STEPS || pitch > 127u) return 0;
    target = &pattern->step[step];
    for (i = 0; i < target->count; i++)
        if (target->note[i].pitch == pitch) return &target->note[i];
    return 0;
}

static const tk_pattern_step_t *tk_pattern_read(const tk_pattern_t *pattern,
                                                uint32_t step)
{
    return pattern && step < TK_PATTERN_STEPS ? &pattern->step[step] : 0;
}

typedef struct {
    uint8_t held[TK_ARP_MAX_NOTES];
    uint16_t count; /* all 128 MIDI pitches must remain representable */
    uint8_t last_pitch;
    uint8_t last_valid;
    uint8_t rising;
    uint8_t previous_mode;
    uint16_t outside_step;
    uint32_t random_state;
} tk_arp_t;

static void tk_arp_init(tk_arp_t *arp)
{
    uint32_t i;
    if (!arp) return;
    arp->count = arp->last_pitch = arp->last_valid = 0;
    arp->rising = 1;
    arp->previous_mode = TK_ARP_UP;
    arp->outside_step = 0;
    arp->random_state = 0x9E3779B9u;
    for (i = 0; i < TK_ARP_MAX_NOTES; i++) arp->held[i] = 0;
}

/* Accept an unsorted list, discard duplicates and invalid pitches, and keep
 * last_pitch as the cursor. A held-list change therefore continues near the
 * previous pitch rather than jumping unpredictably to a stale index. */
static void tk_arp_set_held(tk_arp_t *arp, const uint8_t *pitches, uint32_t count)
{
    uint32_t i, at, j, new_count = 0, changed;
    uint8_t sorted[TK_ARP_MAX_NOTES];
    if (!arp) return;
    if (!pitches) count = 0;
    for (i = 0; i < count; i++) {
        uint32_t pitch = pitches[i];
        if (pitch > 127u) continue;
        for (at = 0; at < new_count && sorted[at] < pitch; at++) {}
        if (at < new_count && sorted[at] == pitch) continue;
        if (new_count == TK_ARP_MAX_NOTES) break;
        for (j = new_count; j > at; j--) sorted[j] = sorted[j - 1u];
        sorted[at] = (uint8_t)pitch;
        new_count++;
    }
    changed = arp->count != new_count;
    if (!changed) {
        for (i = 0; i < new_count; i++) {
            if (arp->held[i] != sorted[i]) { changed = 1; break; }
        }
    }
    if (changed) arp->outside_step = 0;
    arp->count = (uint16_t)new_count;
    for (i = 0; i < new_count; i++) arp->held[i] = sorted[i];
    if (!new_count) {
        arp->last_valid = 0;
        arp->rising = 1;
    }
}

/* Pick one note per caller-provided tick. Up and down wrap; ping-pong turns at
 * endpoints without repeating them. Outside-in alternates low/high toward the
 * center, then repeats. Random is deterministic but never repeats a pitch
 * immediately while two or more distinct notes are held. */
static int tk_arp_next(tk_arp_t *arp, uint32_t mode, uint8_t *pitch)
{
    uint32_t i, at = 0;
    if (!arp || !pitch || !arp->count) return 0;
    if (mode > TK_ARP_LAST_MODE) mode = TK_ARP_UP;
    if (mode != arp->previous_mode) {
        arp->rising = mode != TK_ARP_DOWN;
        arp->previous_mode = (uint8_t)mode;
        arp->outside_step = 0;
    }
    if (mode == TK_ARP_OUTSIDE_IN) {
        uint32_t step = arp->outside_step;
        at = (step & 1u) ? arp->count - 1u - step / 2u : step / 2u;
        arp->outside_step = (uint16_t)((step + 1u) % arp->count);
    } else if (mode == TK_ARP_RANDOM) {
        /* LCG arithmetic wraps by definition for uint32_t. Use its upper bits
         * because low bits have short cycles. Exclude the last pitch from the
         * draw to make a two-key random arp alternate. */
        arp->random_state = arp->random_state * 1664525u + 1013904223u;
        if (arp->last_valid && arp->count > 1u) {
            for (i = 0; i < arp->count; i++)
                if (arp->held[i] == arp->last_pitch) break;
            if (i < arp->count) {
                at = (arp->random_state >> 16) % (arp->count - 1u);
                if (at >= i) at++;
            } else {
                at = (arp->random_state >> 16) % arp->count;
            }
        } else at = (arp->random_state >> 16) % arp->count;
    } else if (mode == TK_ARP_UP || (mode == TK_ARP_PINGPONG && arp->rising)) {
        if (arp->last_valid) {
            for (i = 0; i < arp->count; i++) {
                if (arp->held[i] > arp->last_pitch) { at = i; break; }
            }
            if (i == arp->count && mode == TK_ARP_PINGPONG && arp->count > 1u) {
                arp->rising = 0;
                for (i = arp->count; i > 0; i--) {
                    if (arp->held[i - 1u] < arp->last_pitch) {
                        at = i - 1u; break;
                    }
                }
                if (!i) at = arp->count - 1u;
            }
        }
    } else {
        at = arp->count - 1u;
        if (arp->last_valid) {
            for (i = arp->count; i > 0; i--) {
                if (arp->held[i - 1u] < arp->last_pitch) {
                    at = i - 1u; break;
                }
            }
            if (!i && mode == TK_ARP_PINGPONG && arp->count > 1u) {
                arp->rising = 1;
                for (i = 0; i < arp->count; i++) {
                    if (arp->held[i] > arp->last_pitch) { at = i; break; }
                }
                if (i == arp->count) at = 0;
            }
        }
    }
    *pitch = arp->held[at];
    arp->last_pitch = *pitch;
    arp->last_valid = 1;
    return 1;
}

#endif
