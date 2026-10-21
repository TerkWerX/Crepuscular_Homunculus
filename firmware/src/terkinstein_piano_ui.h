/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TERKINSTEIN_PIANO_UI_H
#define TERKINSTEIN_PIANO_UI_H
static struct {
    uint16_t wanted[TK_PIANO_ROWS][TK_PIANO_COLS], drawn[TK_PIANO_ROWS][TK_PIANO_COLS];
    uint8_t labels[96];
    uint32_t low, key_row, scan, valid;
} tk_piano_paint;
static void tk_piano_note_name(char *out, uint32_t pitch)
{
    static const char *const name[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    const char *p = name[pitch % 12u]; uint32_t at = 0u;
    while (*p) out[at++] = *p++;
    if (pitch < 12u) { out[at++] = '-'; out[at++] = '1'; }
    else out[at++] = (char)('0' + pitch / 12u - 1u);
    out[at] = 0;
}
static void tk_piano_length_name(char *out, uint32_t wire)
{
    uint32_t i, n = tk_piano_gate(wire), at = 0u; const char *text = 0;
    char number[4];
    if (!wire) text = "PULSE";
    for (i = 1u; i < TK_NOTE_VALUE_COUNT; i++) if (TK_NOTE_VALUES[i] == wire) text = TK_NOTE_VALUE_NAMES[i];
    if (text) { while (*text) out[at++] = *text++; }
    else {
        draw_number(number, n); text = number; while (*text) out[at++] = *text++;
        out[at++] = '/'; out[at++] = '6'; out[at++] = '4';
    }
    out[at] = 0;
}
static int tk_piano_encoder(tk_settings_t *s, tk_transport_t *t, uint32_t encoder, int32_t delta)
{
    tk_pattern_note_t *note; uint32_t old, next; int action;
    if (!delta || encoder > 5u || t->recording || tk_banks.importing) return 1;
    if (encoder == 0u) {
        t->edit_step = (uint8_t)(((int32_t)t->edit_step + delta % s->length[t->selected] + s->length[t->selected]) % s->length[t->selected]);
    } else if (encoder == 1u) tk_piano.pitch = adjust_u8(tk_piano.pitch, delta, 0u, 127u);
    else if (encoder == 4u || encoder == 5u) {
        action = tk_seq_encoder(s, t, encoder == 4u ? 0u : 1u, delta);
        tk_piano_read(&tk_piano, &s->pattern[t->selected], t->edit_step); return action;
    } else {
        note = tk_pattern_find(&s->pattern[t->selected], t->edit_step, tk_piano.pitch);
        if (note) tk_piano_read(&tk_piano, &s->pattern[t->selected], t->edit_step);
        old = encoder == 2u ? tk_piano_gate(tk_piano.duration) : tk_piano.velocity;
        next = (uint32_t)((int32_t)old + (delta > 256 ? 256 : delta < -256 ? -256 : delta));
        if ((int32_t)next < 1) next = 1u;
        if (next > (encoder == 2u ? 256u : 127u)) next = encoder == 2u ? 256u : 127u;
        if (encoder == 2u && next == 255u) next = delta > 0 ? 256u : 254u;
        if (next == old) return 1;
        if (encoder == 2u) tk_piano.duration = (uint8_t)(next == 256u ? 255u : next);
        else tk_piano.velocity = (uint8_t)next;
        if (note) {
            note->duration_q4 = tk_piano.duration; note->velocity = tk_piano.velocity;
            tk_banks_observe(s); return 7;
        }
        tk_piano.full = 0u; return 5;
    }
    tk_piano_read(&tk_piano, &s->pattern[t->selected], t->edit_step); return 5;
}
static int tk_piano_button(tk_settings_t *s, tk_transport_t *t, uint32_t add)
{
    int changed;
    if (t->recording || tk_banks.importing) return 1;
    changed = add ? tk_piano_add(&tk_piano, &s->pattern[t->selected], t->edit_step) :
        tk_piano_delete(&tk_piano, &s->pattern[t->selected], t->edit_step);
    if (changed) tk_banks_observe(s);
    return changed ? 7 : 5;
}
/* Readouts change only when their content changes; the grid is diffed and sent
 * one 12x8 cell (96 pixels) per main pass under the transport slack guard. */
static void draw_piano(const tk_settings_t *s, const tk_transport_t *t)
{
    char labels[96], *a = labels, *b = labels + 32, *c = labels + 64;
    char number[4], pitch[5], length[12]; uint32_t at = 0u, i;
    const char *text = TK_SOUND_CONTEXT[t->selected];
    const tk_pattern_step_t *part = &s->pattern[t->selected].step[t->edit_step];
    for (i = 0; i < part->count; i++) if (part->note[i].pitch == tk_piano.pitch) {
        tk_piano.duration = part->note[i].duration_q4; tk_piano.velocity = part->note[i].velocity;
    }
    memset(labels, 0, sizeof labels);
    while (*text) a[at++] = *text++;
    a[at++] = ' '; a[at++] = (char)('A' + tk_banks.active[t->selected]);
    if (tk_banks.pending[t->selected] < TK_BANK_COUNT) { a[at++] = '>'; a[at++] = (char)('A' + tk_banks.pending[t->selected]); }
    a[at++] = ' '; draw_number(number, t->edit_step + 1u); text = number; while (*text) a[at++] = *text++;
    a[at++] = '/'; draw_number(number, s->length[t->selected]); text = number; while (*text) a[at++] = *text++;
    tk_piano_note_name(pitch, tk_piano.pitch); a[at++] = ' '; text = pitch; while (*text) a[at++] = *text++;
    tk_piano_length_name(length, tk_piano.duration);
    text = "K1 "; at = 0u; while (*text) b[at++] = *text++; text = length; while (*text) b[at++] = *text++;
    text = " K2 V"; while (*text) b[at++] = *text++; draw_number(number, tk_piano.velocity); text = number; while (*text) b[at++] = *text++;
    text = tk_banks.importing ? "IMPORT: EDITS LOCKED" : t->recording ? "RECORDING: EDITS LOCKED" :
        tk_piano.full ? "STEP FULL: FOUR NOTES" : s->track_mute & (1u << t->selected) ? "K3 TRACK K4 BANK / MUTED" : "K3 TRACK K4 BANK";
    at = 0u; while (*text) c[at++] = *text++;
    if (!tk_piano_paint.valid) {
        memset(tk_piano_paint.drawn, 255, sizeof tk_piano_paint.drawn);
        tk_piano_paint.low = ~0u; tk_piano_paint.scan = 0u;
        draw_text_box(8, 66, 224, &FONT_S, "SELECT STEP / ALG PITCH", TK_DIM, 0);
        draw_text_box(8, 222, 224, &FONT_S, "SEL ADD REC DEL EDIT BACK", TK_DIM, 0);
    }
    for (i = 0u; i < 3u; i++) if (!tk_piano_paint.valid || memcmp(labels + i * 32u, tk_piano_paint.labels + i * 32u, 32u))
        draw_text_box(8, i == 0u ? 48u : i == 1u ? 184u : 203u, 224, &FONT_S, labels + i * 32u, i == 2u ? TK_DIM : TK_GREEN, 0);
    memcpy(tk_piano_paint.labels, labels, sizeof labels); tk_piano_paint.valid = 1u;
    tk_piano_grid(tk_piano_paint.wanted, &s->pattern[t->selected], s->length[t->selected], t->edit_step,
        tk_piano.pitch, t->mode == TK_MODE_SEQ && t->running ?
        tk_transport_local_step(t, t->selected, t->shown_step, s->length[t->selected]) : TK_PATTERN_STEPS);
}
static int tk_piano_upload(void)
{
    uint32_t i, low = tk_piano_low(tk_piano.pitch);
    if (!tk_piano_paint.valid) return 0;
    if (low != tk_piano_paint.low) { tk_piano_paint.low = low; tk_piano_paint.key_row = 0u; }
    if (tk_piano_paint.key_row < 12u) {
        uint32_t row = tk_piano_paint.key_row++, pc = (low + 11u - row) % 12u;
        cv_begin(24, 8, C_BLACK);
        cv_rect(0, 0, pc == 1u || pc == 3u || pc == 6u || pc == 8u || pc == 10u ? 14 : 22, 7, TK_DIM);
        cv_blit(8, 84u + row * 8u); return 1;
    }
    for (i = 0; i < TK_PIANO_ROWS * TK_PIANO_COLS; i++) {
        uint32_t index = (tk_piano_paint.scan + i) % (TK_PIANO_ROWS * TK_PIANO_COLS);
        uint32_t row = index / TK_PIANO_COLS, col = index % TK_PIANO_COLS, x;
        uint16_t bits = tk_piano_paint.wanted[row][col];
        if (bits == tk_piano_paint.drawn[row][col]) continue;
        cv_begin(12, 8, C_BLACK);
        cv_line(0, 7, 11, 7, C_BAR); cv_line(11, 0, 11, 7, col % 4u == 3u ? TK_DIM : C_BAR);
        for (x = 0; x < 12u; x++) if (bits & (1u << x)) cv_line(x, 2, x, 5, C_ACC);
        if (bits & TK_PIANO_ONSET) cv_line(0, 1, 0, 6, TK_GREEN);
        if (bits & TK_PIANO_HEAD) cv_line(1, 0, 10, 0, TK_GREEN);
        if (bits & TK_PIANO_CURSOR) { cv_line(1, 1, 10, 1, TK_GREEN); cv_line(1, 6, 10, 6, TK_GREEN); }
        cv_blit(40u + col * 12u, 84u + row * 8u);
        tk_piano_paint.drawn[row][col] = bits; tk_piano_paint.scan = (index + 1u) % (TK_PIANO_ROWS * TK_PIANO_COLS); return 1;
    }
    return 0;
}
#endif
