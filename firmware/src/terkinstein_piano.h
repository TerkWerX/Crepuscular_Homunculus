/* SPDX-License-Identifier: GPL-3.0-only */
/* Main-owned piano-roll geometry and exact single-note edits. No DSP state. */
#ifndef TERKINSTEIN_PIANO_H
#define TERKINSTEIN_PIANO_H
#include "terkinstein_transport.h"
#define TK_PIANO_ROWS 12u
#define TK_PIANO_COLS 16u
#define TK_PIANO_ONSET 0x1000u
#define TK_PIANO_CURSOR 0x2000u
#define TK_PIANO_HEAD 0x4000u
typedef struct { uint8_t pitch, duration, velocity, full; } tk_piano_t;
static tk_piano_t tk_piano = {60u, 4u, 100u, 0u};
static uint32_t tk_piano_gate(uint32_t wire)
{ return !wire ? 3u : wire == 255u ? 256u : wire; }
static uint32_t tk_piano_low(uint32_t pitch)
{ uint32_t low = pitch / 12u * 12u; return low > 116u ? 116u : low; }
static void tk_piano_read(tk_piano_t *view, tk_pattern_t *pattern, uint32_t step)
{
    const tk_pattern_note_t *note = tk_pattern_find(pattern, step, view->pitch);
    view->full = 0u;
    if (note) { view->duration = note->duration_q4; view->velocity = note->velocity; }
}
/* Adding an existing onset is selection only: never reset its recorded gate. */
static int tk_piano_add(tk_piano_t *view, tk_pattern_t *pattern, uint32_t step)
{
    tk_pattern_note_t *note = tk_pattern_find(pattern, step, view->pitch);
    if (note) { tk_piano_read(view, pattern, step); return 0; }
    if (!tk_pattern_record(pattern, step, view->pitch, view->velocity)) {
        view->full = 1u; return 0;
    }
    tk_pattern_find(pattern, step, view->pitch)->duration_q4 = view->duration;
    view->full = 0u; return 1;
}
static int tk_piano_delete(tk_piano_t *view, tk_pattern_t *pattern, uint32_t step)
{
    tk_pattern_step_t *part; uint32_t at, i;
    view->full = 0u;
    if (!pattern || step >= TK_PATTERN_STEPS) return 0;
    part = &pattern->step[step];
    for (at = 0; at < part->count; at++) if (part->note[at].pitch == view->pitch) {
        for (i = at + 1u; i < part->count; i++) part->note[i - 1u] = part->note[i];
        part->count--; memset(&part->note[part->count], 0, sizeof part->note[0]); return 1;
    }
    return 0;
}
/* Sparse segments become twelve horizontal pixels per step. Quarter-step gates
 * therefore retain three-pixel precision, including legacy 75% pulses. Wraps
 * are clipped to one local loop for display only; stored gates stay untouched. */
static void tk_piano_segment(uint16_t cells[TK_PIANO_ROWS][TK_PIANO_COLS],
    uint32_t row, uint32_t start, uint32_t end, uint32_t first)
{
    uint32_t col;
    for (col = 0; col < TK_PIANO_COLS; col++) {
        uint32_t left = (first + col) * 4u, lo = start > left ? start : left;
        uint32_t right = left + 4u, hi = end < right ? end : right;
        if (lo < hi) cells[row][col] |= (uint16_t)(((1u << ((hi - lo) * 3u)) - 1u) << ((lo - left) * 3u));
    }
}
static void tk_piano_grid(uint16_t cells[TK_PIANO_ROWS][TK_PIANO_COLS],
    const tk_pattern_t *pattern, uint32_t length, uint32_t cursor,
    uint32_t pitch, uint32_t head)
{
    uint32_t step, i, row, col, first = cursor / 16u * 16u;
    uint32_t low = tk_piano_low(pitch), loop = length * 4u;
    memset(cells, 0, sizeof(uint16_t) * TK_PIANO_ROWS * TK_PIANO_COLS);
    if (!pattern || !length || length > TK_PATTERN_STEPS || cursor >= length || pitch > 127u) return;
    for (step = 0; step < length; step++) for (i = 0; i < pattern->step[step].count; i++) {
        const tk_pattern_note_t *note = &pattern->step[step].note[i];
        uint32_t start, end, gate;
        if (note->pitch < low || note->pitch >= low + TK_PIANO_ROWS) continue;
        row = low + TK_PIANO_ROWS - 1u - note->pitch;
        start = step * 4u; gate = tk_piano_gate(note->duration_q4);
        if (gate > loop) gate = loop;
        end = start + gate;
        tk_piano_segment(cells, row, start, end < loop ? end : loop, first);
        if (end > loop) tk_piano_segment(cells, row, 0u, end - loop, first);
        if (step >= first && step < first + TK_PIANO_COLS) cells[row][step - first] |= TK_PIANO_ONSET;
    }
    row = low + TK_PIANO_ROWS - 1u - pitch;
    cells[row][cursor - first] |= TK_PIANO_CURSOR;
    if (head >= first && head < first + TK_PIANO_COLS)
        for (row = 0; row < TK_PIANO_ROWS; row++) cells[row][head - first] |= TK_PIANO_HEAD;
    for (row = 0; row < TK_PIANO_ROWS; row++) for (col = 0; col < TK_PIANO_COLS; col++)
        if (first + col >= length) cells[row][col] = 0u;
}
#endif
