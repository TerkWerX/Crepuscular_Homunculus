/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TERKINSTEIN_MEMORY_H
#define TERKINSTEIN_MEMORY_H

#include <stdint.h>

/* Compact timbre history, owned entirely by audio. No phases, samples, held
 * notes or extra voices are retained. Source 0 is panel, 1..16 are USB MIDI,
 * 17..32 are TRS MIDI and 33 is melody; nearest-pitch lookup never borrows from
 * another source.
 *
 * Cells run oldest to newest. Remembering an existing note/source refreshes
 * that cell; a fifth distinct note evicts the oldest. Equal pitch distances
 * favor the newest cell, giving repeated progressions deterministic behavior.
 * Four six-byte cells plus a count use 28 bytes on the target.
 */
#define TK_MEMORY_CELLS 4u
#define TK_MEMORY_SOURCES 45u
#define TK_MEMORY_MAX_Q8 (127 * 256)
#define TK_MEMORY_ENV_FULL (1u << 23)

typedef struct {
    uint16_t shade_q8, fracture_q8;
    uint8_t note, source;
} tk_memory_cell_t;

typedef struct {
    uint32_t count;
    tk_memory_cell_t cell[TK_MEMORY_CELLS];
} tk_memory_state_t;

static int32_t tk_memory_clamp_q8(int32_t value)
{
    return value < 0 ? 0 : value > TK_MEMORY_MAX_Q8 ? TK_MEMORY_MAX_Q8 : value;
}

static void tk_memory_init(tk_memory_state_t *state)
{
    uint32_t i;
    if (!state) return;
    state->count = 0;
    for (i = 0; i < TK_MEMORY_CELLS; ++i) {
        state->cell[i].shade_q8 = state->cell[i].fracture_q8 = 0;
        state->cell[i].note = state->cell[i].source = 0;
    }
}

static void tk_memory_remember(tk_memory_state_t *state, uint32_t note,
                               uint32_t source, int32_t shade_q8,
                               int32_t fracture_q8)
{
    uint32_t i, discard;
    if (!state || note > 127u || source >= TK_MEMORY_SOURCES) return;
    discard = state->count;
    for (i = 0; i < state->count; ++i) {
        if (state->cell[i].note == note && state->cell[i].source == source) {
            discard = i;
            break;
        }
    }
    if (discard == state->count && state->count == TK_MEMORY_CELLS)
        discard = 0;
    if (discard < state->count) {
        for (i = discard + 1u; i < state->count; ++i)
            state->cell[i - 1u] = state->cell[i];
        --state->count;
    }
    i = state->count++;
    state->cell[i].shade_q8 = (uint16_t)tk_memory_clamp_q8(shade_q8);
    state->cell[i].fracture_q8 = (uint16_t)tk_memory_clamp_q8(fracture_q8);
    state->cell[i].note = (uint8_t)note;
    state->cell[i].source = (uint8_t)source;
}

/* A miss leaves output values untouched. The history is reusable; a chord
 * starting over several audio blocks still sees the same remembered timbre.
 */
static int tk_memory_pick(const tk_memory_state_t *state, uint32_t note,
                          uint32_t source, int32_t *shade_q8,
                          int32_t *fracture_q8)
{
    uint32_t i, best = TK_MEMORY_CELLS, best_distance = 128u;
    if (!state || !shade_q8 || !fracture_q8 || note > 127u || source >= TK_MEMORY_SOURCES)
        return 0;
    for (i = 0; i < state->count; ++i) {
        uint32_t old_note = state->cell[i].note;
        uint32_t distance = note > old_note ? note - old_note : old_note - note;
        if (state->cell[i].source == source && distance <= best_distance) {
            best = i;
            best_distance = distance;
        }
    }
    if (best == TK_MEMORY_CELLS) return 0;
    *shade_q8 = (int32_t)state->cell[best].shade_q8;
    *fracture_q8 = (int32_t)state->cell[best].fracture_q8;
    return 1;
}

/* Called at voice start, not per sample. Both endpoints are exact; signed
 * 32-bit products stay below 4.2 million and division is 32-bit only.
 */
static int32_t tk_memory_mix_q8(int32_t current_q8, int32_t remembered_q8,
                               uint32_t amount)
{
    int32_t current = tk_memory_clamp_q8(current_q8);
    int32_t remembered = tk_memory_clamp_q8(remembered_q8);
    if (amount > 127u) amount = 127u;
    return current + ((remembered - current) * (int32_t)amount) / 127;
}

/* Once each 32-frame control tick, relax a voice's remembered offset toward
 * its current macros. The minimum step avoids a permanent one-unit residue.
 * At 44.1 kHz even a full-range offset reaches exactly zero within 4 s;
 * normal imprinted offsets clear in about 2 s. No per-sample history work.
 */
static int32_t tk_memory_decay_q8(int32_t offset_q8)
{
    int32_t magnitude, step;
    if (offset_q8 > TK_MEMORY_MAX_Q8) offset_q8 = TK_MEMORY_MAX_Q8;
    if (offset_q8 < -TK_MEMORY_MAX_Q8) offset_q8 = -TK_MEMORY_MAX_Q8;
    magnitude = offset_q8 < 0 ? -offset_q8 : offset_q8;
    if (!magnitude) return 0;
    step = magnitude >> 10;
    if (!step) step = 1;
    return offset_q8 < 0 ? offset_q8 + step : offset_q8 - step;
}

/* A mature envelope leaves a modest darker/corroded imprint, so an unchanged
 * macro setting can still produce an afterimage. Imprint only the snapshot
 * being remembered, never the fresh voice's macro target. An early release
 * leaves less; full envelope adds at most 12 shade and 16 fracture units.
 * Zero fracture remains clean. FX bypass must also force the voice's effective
 * fracture to zero in the engine; history never overrides that live bypass.
 */
static void tk_memory_imprint_q8(int32_t shade_q8, int32_t fracture_q8,
                                 uint32_t envelope_q23, int32_t *shade_out,
                                 int32_t *fracture_out)
{
    int32_t maturity = envelope_q23 >= TK_MEMORY_ENV_FULL ? 256 :
                       (int32_t)(envelope_q23 >> 15);
    int32_t shade = tk_memory_clamp_q8(shade_q8);
    int32_t fracture = tk_memory_clamp_q8(fracture_q8);
    if (shade_out) *shade_out = tk_memory_clamp_q8(shade + maturity * 12);
    if (fracture_out)
        *fracture_out = fracture ? tk_memory_clamp_q8(fracture + maturity * 16) : 0;
}

#endif
