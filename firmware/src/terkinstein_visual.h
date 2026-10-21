/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TERKINSTEIN_VISUAL_H
#define TERKINSTEIN_VISUAL_H
#include <stdint.h>

/* A read-only audio tap. Audio publishes small atomic words; main accepts a
 * snapshot only if its sequence stays even and unchanged. No screen or DSP
 * mutation belongs here, and the reader never waits for the interrupt. */
#if defined(_MSC_VER)
#include <intrin.h>
#define TK_VIS_BARRIER() _ReadWriteBarrier()
#elif defined(__GNUC__)
#define TK_VIS_BARRIER() __asm__ volatile("" ::: "memory")
#else
#define TK_VIS_BARRIER() ((void)0)
#endif
#define TK_VIS_SAMPLES 64u
#define TK_VIS_VOICES 8u
typedef struct {
    int16_t mono[TK_VIS_SAMPLES], side[TK_VIS_SAMPLES];
    /* pitch[7:0], envelope[15:8], source[23:16], sounding gate[24]. */
    uint32_t voice[TK_VIS_VOICES];
    uint16_t peak, rough;
    uint32_t generation;
} tk_visual_snapshot_t;
static struct {
    volatile uint32_t sequence, mono[TK_VIS_SAMPLES / 2u], side[TK_VIS_SAMPLES / 2u];
    volatile uint32_t voice[TK_VIS_VOICES], energy;
} tk_visual_bus;
static volatile uint32_t tk_visual_enabled; /* main is the only writer */
static uint32_t tk_visual_divider;          /* audio is the only writer */

static void tk_visual_init(void)
{
    uint32_t i;
    tk_visual_enabled = tk_visual_divider = tk_visual_bus.sequence = 0;
    tk_visual_bus.energy = 0;
    for (i = 0; i < TK_VIS_SAMPLES / 2u; i++)
        tk_visual_bus.mono[i] = tk_visual_bus.side[i] = 0;
    for (i = 0; i < TK_VIS_VOICES; i++) tk_visual_bus.voice[i] = 0;
}
static void tk_visual_set_enabled(uint32_t enabled)
{
    tk_visual_enabled = !!enabled;
}
static int tk_visual_due(void)
{
    return tk_visual_enabled && !(++tk_visual_divider & 3u);
}
static int16_t tk_visual_sample(int32_t value)
{
    if (value > 32760) value = 32760;
    if (value < -32760) value = -32760;
    return (int16_t)value;
}
static void tk_visual_capture(const int32_t *out, uint32_t frames,
                               const uint32_t *voices, uint32_t count)
{
    uint32_t i, peak = 0, rough = 0, mono_pair = 0, side_pair = 0;
    int32_t previous = 0;
    if (!out || !frames) return;
    tk_visual_bus.sequence++;
    TK_VIS_BARRIER();
    for (i = 0; i < TK_VIS_SAMPLES; i++) {
        uint32_t at = i * frames / TK_VIS_SAMPLES;
        int32_t l = out[2u * at] / 2, r = out[2u * at + 1u] / 2;
        int16_t mono = tk_visual_sample(l + r), side = tk_visual_sample(l - r);
        uint32_t magnitude = (uint32_t)(mono < 0 ? -mono : mono);
        int32_t difference = (int32_t)mono - previous;
        if (magnitude > peak) peak = magnitude;
        if (i) rough += (uint32_t)(difference < 0 ? -difference : difference);
        previous = mono;
        if (!(i & 1u)) {
            mono_pair = (uint16_t)mono; side_pair = (uint16_t)side;
        } else {
            tk_visual_bus.mono[i / 2u] = mono_pair | ((uint32_t)(uint16_t)mono << 16);
            tk_visual_bus.side[i / 2u] = side_pair | ((uint32_t)(uint16_t)side << 16);
        }
    }
    if (count > TK_VIS_VOICES) count = TK_VIS_VOICES;
    for (i = 0; i < TK_VIS_VOICES; i++)
        tk_visual_bus.voice[i] = voices && i < count ? voices[i] : 0;
    rough /= TK_VIS_SAMPLES - 1u;
    if (rough > 32760u) rough = 32760u;
    tk_visual_bus.energy = peak | (rough << 16);
    TK_VIS_BARRIER();
    tk_visual_bus.sequence++;
}
static int tk_visual_read(tk_visual_snapshot_t *snapshot)
{
    tk_visual_snapshot_t next;
    uint32_t i, energy, before = tk_visual_bus.sequence;
    if (!snapshot || !before || (before & 1u)) return 0;
    TK_VIS_BARRIER();
    for (i = 0; i < TK_VIS_SAMPLES / 2u; i++) {
        uint32_t mono = tk_visual_bus.mono[i], side = tk_visual_bus.side[i];
        next.mono[2u * i] = (int16_t)(uint16_t)mono;
        next.mono[2u * i + 1u] = (int16_t)(uint16_t)(mono >> 16);
        next.side[2u * i] = (int16_t)(uint16_t)side;
        next.side[2u * i + 1u] = (int16_t)(uint16_t)(side >> 16);
    }
    for (i = 0; i < TK_VIS_VOICES; i++) next.voice[i] = tk_visual_bus.voice[i];
    energy = tk_visual_bus.energy;
    next.peak = (uint16_t)energy; next.rough = (uint16_t)(energy >> 16);
    next.generation = before;
    TK_VIS_BARRIER();
    if (before != tk_visual_bus.sequence) return 0;
    *snapshot = next;
    return 1;
}
#endif
