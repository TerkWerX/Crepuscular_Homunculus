/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TERKINSTEIN_ENGINE_H
#define TERKINSTEIN_ENGINE_H

#include <stdint.h>
#include "terkinstein_memory.h"

/* Seed engine, 44100 Hz. Suggested start: {88, 40, 32, 80}.
 * SHADE closes the lowpass; FRACTURE folds each voice; DRIFT spreads the
 * oscillators; BLOOM lengthens attack/release. Every macro is 0..127.
 * Each stereo voice is bounded by 4064; six voices <= 24384 (~2.6 dB headroom).
 * No limiter is needed at this voice count.
 * FRACTURE=0 bypasses both drive/folding and saturation after macro smoothing.
 * KIT uses the same four bytes as BODY/WIRE/DECAY/BITE; all 64 is its exact
 * original sound. Its coefficients slew at the same 32-frame control rate.
 * Folded high notes can alias: oversampling is deliberately a later DSP task.
 * Include once, call tk_init before publishing the app's audio-ready flag.
 */
#define TK_VOICES 6u
#define TK_SOURCE_SEQUENCE 33u
#define TK_SOURCE_DRUM 34u
#define TK_SOURCE_ATMOS 35u
#define TK_SOURCE_ARP 36u
#define TK_SOURCE_BASS 37u
#define TK_SOURCE_LIVE_DRUM 38u /* negotiated USB pads; captured DRUM sound, live ownership */
#define TK_SOURCES 40u
#define TK_INSTRUMENT_PAD 0u
#define TK_INSTRUMENT_METAL 1u
#define TK_INSTRUMENT_DRUM_KIT 2u
#define TK_INSTRUMENT_FM 3u
#define TK_FM_VOICE_LIMIT 4u /* provisional CPU qualification cap inside six total voices */
typedef struct tk_params_t {
    uint8_t shade, fracture, drift, bloom;
} tk_params_t;
#include "terkinstein_fm.h"

/* Main loop alone publishes commands and owns age/key_down. Audio alone owns
 * DSP state except tail, which audio publishes for quiet/voice allocation.
 * A whole note event
 * fits one aligned 32-bit store: note[6:0], velocity[13:7], source[19:14],
 * gate[20], panic[21], revision[31:22]. Source 0 is panel, 1..16 USB MIDI,
 * 17..32 TRS MIDI, 33 melody, 34 drums, 35 atmosphere, 36 ARP and 37 bass.
 * Source 38 is an explicitly enabled external pad owner, using the captured
 * DRUM sound while remaining independent of sequencer source 34. It stays
 * polyphonic even in live MONO mode. Track notes, bend and sustain stay separate
 * from panel/USB ownership.
 * Revisions distinguish same-note retriggers. At most 1023 events per voice
 * between audio blocks: exceeding that can collapse a retrigger to old state.
 */
#define TK_GATE (1u << 20)
#define TK_PANIC (1u << 21)
#define TK_PAYLOAD ((1u << 22) - 1u)
#define TK_ENV_FULL 8388608
#define TK_BEND_CENTER 8192u
#define TK_BEND_MASK 16383u
#define TK_SUSTAIN (1u << 14)
#define TK_SOSTENUTO (1u << 15)
#define TK_CONTROLLER_REV (1u << 16)
#define TK_CONTROLLER_MASK (TK_BEND_MASK | TK_SUSTAIN | TK_SOSTENUTO)
#define TK_PITCH_ONE 16777216u             /* Q24 pitch factor */
#define TK_PERFORMANCE_DEFAULT (127u | (127u << 7))

/* Captured sound and generated note ownership are independent properties.
 * External pads use captured controls without surrendering a held live voice
 * to generated sequencing. Sources 0..32 retain their original behavior. */
static uint32_t tk_source_is_live(uint32_t source)
{
    return source < TK_SOURCE_SEQUENCE || source == TK_SOURCE_LIVE_DRUM;
}

typedef struct {
    volatile uint32_t command;
    volatile uint32_t tail;
    volatile uint32_t fm_tail;           /* audio publishes actual FM residency */
    uint32_t age, key_down;               /* main-owned allocation and physical key */
    uint32_t allocated_instrument;       /* main-owned instrument of last command */
    uint32_t seen, pending;
    uint32_t phase_a, phase_b, base_inc, inc_a, inc_b, source;
    uint32_t audio_note, remembered, memory_colored;
    int32_t memory_shade, memory_fracture, color_shade, color_fracture;
    int32_t color_alpha, color_drive;
    int32_t env, filter_l, filter_r;
    uint32_t level, gate, fading;
    uint32_t instrument, drum_lane, drum_decay, drum_elapsed, drum_noise;
    uint32_t forge_decay_q8, forge_hat_alpha, forge_clap_width;
    int32_t forge_wire_q8, forge_bite_q8, forge_tone_gain, forge_noise_gain;
    int32_t forge_filter_l, forge_filter_r;
    uint32_t forge_latched;
    int32_t forge_bite_target_q8;
    uint32_t frozen_patch, patch_drift, patch_bloom, patch_attack, patch_release;
    int32_t patch_drift_q8, patch_bloom_q8;
    uint32_t patch_metal_attack;
    int32_t ghost_gain_q15, ghost_target_q15;
    uint32_t sostenuto_latched;           /* main-owned physical capture */
    volatile uint32_t poly_pressure;     /* main publishes one atomic scalar */
    int32_t pressure_q8, performance_gain_q15;
    int32_t performance_alpha, performance_drive, performance_fracture;
    tk_fm_voice_t fm;
} tk_voice_t;

static tk_voice_t tk_voice[TK_VOICES];
static uint32_t tk_age, tk_control_phase, tk_lfo;
static int32_t tk_macro[4];                /* Q8, audio-owned */
static int32_t tk_alpha, tk_drive, tk_attack, tk_release, tk_drift;
static uint32_t tk_metal_attack;
/* Main publishes FX separately from mode, so a bypass also cleans old KIT
 * tails after a preset switch. The smooth Q15 gain belongs to audio only. */
static volatile uint32_t tk_fx_control;
static int32_t tk_forge_fx_q15;
/* Main publishes amount[7:0] and forget revision[31:8] in one atomic word.
 * The compact history and all voice color offsets belong to audio only. */
static volatile uint32_t tk_memory_control;
static uint32_t tk_memory_seen;
static int32_t tk_memory_amount_q8;
static tk_memory_state_t tk_history;
/* Main publishes one depth scalar. Audio owns short melody-event pulses and
 * ATMOS gain slews; no extra voice, buffer or flash work enters the ISR. */
static volatile uint32_t tk_ghost_control;
typedef struct { uint16_t ticks; uint8_t pitch; } tk_ghost_pulse_t;
static tk_ghost_pulse_t tk_ghost_pulse[TK_VOICES];
#define TK_GHOST_PULSE_TICKS 256u /* 256 * 32 / 44100 ~= 186 ms */

/* Main publishes both envelope timings in one aligned store. 64/64 keeps
 * the original pad envelope; audio reads this once per control tick. */
static volatile uint32_t tk_envelope_control;
/* Main publishes one live mode and four independent internal-source modes.
 * Each 32-bit source patch word is atomic; set the timing/valid word last,
 * before issuing notes on that track. Audio latches all fields at note-on.
 * Explicit track macro edits additionally publish a follow mailbox. */
static volatile uint32_t tk_instrument_control;
static volatile uint32_t tk_source_instrument_control[TK_SOURCES];
static volatile uint32_t tk_source_patch_word[TK_SOURCES];
static volatile uint32_t tk_source_patch_timing[TK_SOURCES];
/* Bit 31 enables explicit captured-track macro following; the four bytes
 * otherwise carry the same 0..127 macros as the ordinary packed patch. */
static volatile uint32_t tk_source_macro_control[TK_SOURCES];
/* Main's per-track LFO publishes only SHADE. Audio smooths that independent
 * target alongside the captured source's atomically packed macro word. */
static volatile uint32_t tk_source_shade_control[TK_SOURCES];

static void tk_set_instrument(uint32_t mode)
{
    tk_instrument_control = mode <= TK_INSTRUMENT_FM ? mode : TK_INSTRUMENT_PAD;
}
static void tk_set_fx(uint32_t enabled)
{
    tk_fx_control = !!enabled;
}

static void tk_set_source_instrument(uint32_t source, uint32_t mode)
{
    if (source < TK_SOURCE_SEQUENCE || source >= TK_SOURCES) return;
    tk_source_instrument_control[source] =
        mode <= TK_INSTRUMENT_FM ? mode : TK_INSTRUMENT_PAD;
}

static void tk_set_sequence_instrument(uint32_t mode)
{
    tk_set_source_instrument(TK_SOURCE_SEQUENCE, mode);
}

static void tk_set_source_shade(uint32_t source, uint32_t shade)
{
    if (source < TK_SOURCE_SEQUENCE || source >= TK_SOURCES) return;
    tk_source_shade_control[source] = shade > 127u ? 127u : shade;
}

static void tk_set_source_patch(uint32_t source, const tk_params_t *p,
                                uint32_t attack, uint32_t release)
{
    uint32_t shade, fracture, drift, bloom;
    if (source < TK_SOURCE_SEQUENCE || source >= TK_SOURCES || !p) return;
    if (attack > 127u) attack = 127u;
    if (release > 127u) release = 127u;
    shade = p->shade > 127u ? 127u : p->shade;
    fracture = p->fracture > 127u ? 127u : p->fracture;
    drift = p->drift > 127u ? 127u : p->drift;
    bloom = p->bloom > 127u ? 127u : p->bloom;
    tk_source_patch_word[source] = shade | (fracture << 8) |
                                   (drift << 16) | (bloom << 24);
    tk_source_macro_control[source] = 0u;
    tk_source_shade_control[source] = shade;
    tk_source_patch_timing[source] = 0x80000000u | attack | (release << 7);
}

static void tk_follow_source_macros(uint32_t source)
{
    if (source < TK_SOURCE_SEQUENCE || source >= TK_SOURCES) return;
    tk_source_macro_control[source] = 0x80000000u | tk_source_patch_word[source];
}

static void tk_clear_source_patch(uint32_t source)
{
    if (source >= TK_SOURCE_SEQUENCE && source < TK_SOURCES) {
        tk_source_patch_timing[source] = 0u;
        tk_source_macro_control[source] = 0u;
    }
}

static void tk_set_envelope(uint32_t attack, uint32_t release)
{
    if (attack > 127u) attack = 127u;
    if (release > 127u) release = 127u;
    tk_envelope_control = attack | (release << 7);
}

static void tk_set_memory(uint32_t amount)
{
    if (amount > 127u) amount = 127u;
    tk_memory_control = (tk_memory_control & ~255u) | amount;
}
static void tk_set_ghost_mask(uint32_t amount)
{
    tk_ghost_control = amount > 127u ? 127u : amount;
}
static void tk_forget_memory(void)
{
    tk_memory_control += 256u;
}

/* One main writer, one atomic word per source: bend[13:0], sustain[14],
 * sostenuto[15], controller-reset revision[31:16]. Audio snapshots at control ticks.
 * The revision immediately centers a reset controller, even during a bend
 * slew. Ordinary bend changes slew only the audio-owned Q24 factors.
 */
static volatile uint32_t tk_source_control[TK_SOURCES];
static uint32_t tk_source_seen[TK_SOURCES]; /* audio-owned */
static uint32_t tk_bend_target[TK_SOURCES], tk_bend_current[TK_SOURCES];
/* volume[6:0], expression[13:7], modulation[20:14], channel pressure[27:21],
 * soft pedal[28]. Defaults are exact unity/neutral. Per-note pressure resides
 * in that note's allocated voice rather than a large 40 x 128 mailbox.
 * Main owns publication; audio alone owns the smooth values and vibrato clock. */
static volatile uint32_t tk_source_performance[TK_SOURCES];
static volatile uint32_t tk_source_bend_range[TK_SOURCES];
static volatile uint32_t tk_source_timbre[TK_SOURCES]; /* brightness[6:0], texture[13:7]; 64/64 neutral */
static uint32_t tk_bend_range_seen[TK_SOURCES];
static int32_t tk_modulation_q8[TK_SOURCES], tk_pressure_q8[TK_SOURCES];
static int32_t tk_soft_q8[TK_SOURCES], tk_gain_target_q15[TK_SOURCES];
static int32_t tk_brightness_q8[TK_SOURCES], tk_texture_q8[TK_SOURCES];
static uint32_t tk_performance_seen[TK_SOURCES], tk_timbre_seen[TK_SOURCES];
static uint32_t tk_performance_slewing[TK_SOURCES];
static uint32_t tk_performance_phase;
#include "terkinstein_mono.h"

/* Quarter-semitone factors from -2 to +2 semitones, rounded in Q24.
 * Interpolation needs only a bounded 32-bit product. The final phase
 * scaling uses a 64-bit multiply and shift, never a 64-bit division.
 */
static const uint32_t tk_bend_factor[17] = {
    14946800u, 15164206u, 15384775u, 15608552u,
    15835583u, 16065917u, 16299601u, 16536684u,
    16777216u, 17021246u, 17268826u, 17520007u,
    17774841u, 18033382u, 18295684u, 18561801u,
    18831788u
};

static const uint32_t tk_inc_c9[12] = {
    815363807u, 863847862u, 915214929u, 969636441u,
    1027294024u, 1088380105u, 1153098554u, 1221665363u,
    1294309365u, 1371273005u, 1452813141u, 1539201906u
};

static const uint32_t tk_drum_pitch[12] = {
    6369720u, 3184860u, 14300000u, 10000000u,
    50000000u, 55000000u, 90000000u, 9550000u,
    15000000u, 32000000u, 66000000u, 22500000u
};

static int32_t tk_performance_kit_bite(tk_voice_t *s, int32_t bite)
{
    int32_t extra;
    if (bite <= 0) return bite;            /* neutral/clean kit never gains grit */
    extra = ((s->pressure_q8 * 24) >> 7) +
        (((tk_texture_q8[s->source] - 16384) * 24) >> 6);
    if (!extra) return bite;
    bite += extra;
    return bite > 16384 ? 16384 : bite < 0 ? 0 : bite;
}

/* Coefficients only: no division, new oscillator, voice or sample buffer.
 * BODY spans approximately half/double pitch; DECAY spans quarter/fourfold
 * tails. Interpolation uses smoothed Q8 macros, so a held strike can evolve.
 * Frozen sources retain their own captured settings even while live knobs move.
 * Unpatched sequencer sources keep the legacy neutral kit; unfrozen ARP follows
 * the live controls just like the panel and incoming MIDI.
 * FX bypass publishes BITE=64 and a separate live FX scalar, leaving the other
 * three controls live and cleaning even a KIT tail after a preset switch. */
static void tk_forge_control(tk_voice_t *s)
{
    static const uint16_t body_factor[17] = {
        2048u, 2233u, 2435u, 2656u, 2896u, 3158u, 3444u, 3756u,
        4096u, 4467u, 4871u, 5312u, 5793u, 6317u, 6889u, 7512u, 8192u
    };
    static const uint16_t decay_factor[17] = {
        1024u, 861u, 724u, 609u, 512u, 431u, 362u, 304u,
        256u, 215u, 181u, 152u, 128u, 108u, 91u, 76u, 64u
    };
    uint32_t neutral = !s->frozen_patch && s->source >= TK_SOURCE_SEQUENCE && s->source != TK_SOURCE_ARP;
    uint32_t body = neutral ? 16384u : (uint32_t)(s->frozen_patch ? s->color_shade : tk_macro[0]);
    uint32_t decay = neutral ? 16384u : s->frozen_patch ? s->patch_drift << 8 : (uint32_t)tk_macro[2];
    uint32_t index = body >> 11, fraction = body & 2047u, factor, base;
    int32_t difference;
    if (!s->frozen_patch && !neutral && s->drum_elapsed &&
        tk_instrument_control != TK_INSTRUMENT_DRUM_KIT) s->forge_latched = 1u;
    if (s->forge_latched) {
        s->forge_bite_q8 = (s->forge_bite_target_q8 * tk_forge_fx_q15) >> 15;
        s->forge_bite_q8 = tk_performance_kit_bite(s, s->forge_bite_q8);
        return;
    }
    factor = body_factor[index] +
        (((body_factor[index + 1u] - body_factor[index]) * fraction) >> 11);
    base = (uint32_t)(((uint64_t)tk_drum_pitch[s->drum_lane] * factor) >> 12);
    difference = (int32_t)base - (int32_t)s->base_inc;
    if (s->drum_lane <= 1u || s->drum_lane == 7u || s->drum_lane == 8u)
        s->inc_a = (uint32_t)((int32_t)s->inc_a + difference);
    else s->inc_a = base * 3u;
    s->inc_b = base + (base >> 1);
    s->base_inc = base;
    s->forge_hat_alpha = factor >> 1;
    s->forge_clap_width = (uint32_t)(150 + ((16384 - (int32_t)body) >> 7));
    index = decay >> 11; fraction = decay & 2047u;
    s->forge_decay_q8 = decay_factor[index] -
        (((decay_factor[index] - decay_factor[index + 1u]) * fraction) >> 11);
    s->forge_wire_q8 = neutral ? 0 : (s->frozen_patch ? s->color_fracture : tk_macro[1]) - 16384;
    s->forge_bite_target_q8 = neutral ? 0 :
        (s->frozen_patch ? (int32_t)s->patch_bloom << 8 : tk_macro[3]) - 16384;
    s->forge_bite_q8 = s->frozen_patch ? s->forge_bite_target_q8 :
        (s->forge_bite_target_q8 * tk_forge_fx_q15) >> 15;
    s->forge_bite_q8 = tk_performance_kit_bite(s, s->forge_bite_q8);
    s->forge_tone_gain = 4096 - ((s->forge_wire_q8 * 24) >> 8);
    s->forge_noise_gain = 4096 + ((s->forge_wire_q8 * 48) >> 8);
}

static int32_t tk_sine(uint32_t phase)
{
    int32_t x = (int32_t)phase >> 16;
    int32_t ax = x < 0 ? -x : x;
    int32_t y = (x * (32768 - ax)) >> 13;
    int32_t ay = y < 0 ? -y : y;
    y += ((((y * ay) >> 15) - y) * 7373) >> 15;
    return y > 32760 ? 32760 : y < -32760 ? -32760 : y;
}

static int32_t tk_fold_color(int32_t x, int32_t drive, int32_t fracture)
{
    uint32_t pass;
    int32_t soft;
    x = (x * drive) >> 12;                /* |x| <= 162780 */
    for (pass = 0; pass < 2u; ++pass) {    /* two fixed reflections cover drive <= 5 */
        if (x > 32760) x = 65520 - x;
        if (x < -32760) x = -65520 - x;
    }
    /* Gentle, bounded saturation after folding. All products fit int32_t. */
    soft = (x * (x < 0 ? -x : x)) >> 17;
    x -= (soft * fracture) >> 15;         /* smooth corrosion depth; zero is clean */
    return x;
}
static int32_t tk_fold(int32_t x)
{
    return tk_fold_color(x, tk_drive, tk_macro[1]);
}

static uint32_t tk_revision(uint32_t old)
{
    /* Do not materialize ~TK_PAYLOAD (0xffc00000): the target ROM-address
     * guard rejects that bit pattern even when used only as an integer mask.
     * The volatile temporary keeps clang from folding this bounded index
     * extraction/repacking back into an add-and-high-mask instruction. Main
     * alone calls this event helper; no per-frame audio cost is introduced. */
    volatile uint32_t revision = ((old >> 22) + 1u) & 1023u;
    return revision << 22;
}

static uint32_t tk_bend_scale(uint32_t value)
{
    uint32_t position, index, fraction;
    if (value == TK_BEND_CENTER) return TK_PITCH_ONE;
    /* MIDI's positive half has 8191 steps; the negative half has 8192. */
    position = value <= TK_BEND_CENTER ? value :
        TK_BEND_CENTER + ((value - TK_BEND_CENTER) * 8192u + 4095u) / 8191u;
    index = position >> 10;
    if (index >= 16u) return tk_bend_factor[16];
    fraction = position & 1023u;
    return tk_bend_factor[index] +
        (((tk_bend_factor[index + 1u] - tk_bend_factor[index]) * fraction + 512u) >> 10);
}

/* Quarter-semitone grid from -24 to +24. At the default range the legacy
 * table/interpolation path is retained exactly; all arithmetic is bounded
 * 32-bit except the phase-factor multiply. MIDI RPN is parsed by main. */
static uint32_t tk_bend_scale_range(uint32_t value, uint32_t range)
{
    static const uint32_t factors[193] = {
    4194304u, 4255312u, 4317206u, 4380002u, 4443710u, 4508346u, 4573921u, 4640450u,
    4707947u, 4776426u, 4845900u, 4916386u, 4987896u, 5060447u, 5134053u, 5208729u,
    5284492u, 5361357u, 5439339u, 5518456u, 5598724u, 5680159u, 5762779u, 5846601u,
    5931642u, 6017919u, 6105452u, 6194258u, 6284355u, 6375763u, 6468501u, 6562588u,
    6658043u, 6754886u, 6853138u, 6952819u, 7053950u, 7156552u, 7260647u, 7366255u,
    7473400u, 7582103u, 7692387u, 7804276u, 7917792u, 8032959u, 8149801u, 8268342u,
    8388608u, 8510623u, 8634413u, 8760003u, 8887421u, 9016691u, 9147842u, 9280900u,
    9415894u, 9552851u, 9691801u, 9832771u, 9975792u, 10120894u, 10268105u, 10417458u,
    10568984u, 10722713u, 10878679u, 11036913u, 11197448u, 11360319u, 11525559u, 11693202u,
    11863283u, 12035839u, 12210904u, 12388516u, 12568711u, 12751527u, 12937002u, 13125175u,
    13316085u, 13509772u, 13706276u, 13905639u, 14107901u, 14313105u, 14521294u, 14732511u,
    14946800u, 15164206u, 15384775u, 15608552u, 15835583u, 16065917u, 16299601u, 16536684u,
    16777216u, 17021246u, 17268826u, 17520007u, 17774841u, 18033382u, 18295684u, 18561801u,
    18831788u, 19105703u, 19383602u, 19665543u, 19951585u, 20241787u, 20536211u, 20834917u,
    21137968u, 21445426u, 21757357u, 22073825u, 22394897u, 22720638u, 23051117u, 23386403u,
    23726566u, 24071677u, 24421808u, 24777031u, 25137421u, 25503054u, 25874004u, 26250350u,
    26632170u, 27019544u, 27412552u, 27811277u, 28215802u, 28626210u, 29042588u, 29465022u,
    29893600u, 30328413u, 30769550u, 31217103u, 31671166u, 32131834u, 32599202u, 33073369u,
    33554432u, 34042492u, 34537652u, 35040014u, 35549682u, 36066764u, 36591368u, 37123601u,
    37663576u, 38211406u, 38767204u, 39331085u, 39903169u, 40483574u, 41072421u, 41669834u,
    42275935u, 42890853u, 43514715u, 44147651u, 44789793u, 45441276u, 46102234u, 46772807u,
    47453133u, 48143355u, 48843616u, 49554062u, 50274843u, 51006107u, 51748008u, 52500700u,
    53264341u, 54039088u, 54825105u, 55622554u, 56431603u, 57252420u, 58085175u, 58930044u,
    59787201u, 60656826u, 61539100u, 62434206u, 63342333u, 64263668u, 65198405u, 66146738u,
    67108864u,
    };
    uint32_t position, index, fraction;
    int32_t offset;
    if (range == 2u) return tk_bend_scale(value);
    if (!range || value == TK_BEND_CENTER) return TK_PITCH_ONE;
    if (range > 24u) range = 24u;
    position = value <= TK_BEND_CENTER ? value :
        TK_BEND_CENTER + ((value - TK_BEND_CENTER) * 8192u + 4095u) / 8191u;
    offset = ((int32_t)position - 8192) * (int32_t)range / 2;
    position = (uint32_t)(98304 + offset);
    index = position >> 10;
    if (index >= 192u) return factors[192];
    fraction = position & 1023u;
    return factors[index] + (((factors[index + 1u] - factors[index]) * fraction + 512u) >> 10);
}

static uint32_t tk_envelope_rate(uint32_t value);

/* Q23 envelope units per sample. Interpolated strikes range from roughly
 * 1.5 ms at ATTACK=0 to a bowed ~0.5 s at 127, without a runtime divide. */
static uint32_t tk_metal_attack_step(uint32_t attack)
{
    static const uint32_t step[17] = {
        131072u, 110000u, 95000u, 80000u, 68000u, 56000u,
        46000u, 38000u, 32768u, 24000u, 16000u, 10000u,
        6000u, 3000u, 1400u, 700u, 350u
    };
    uint32_t index, fraction;
    if (attack > 127u) attack = 127u;
    index = attack >> 3;
    fraction = attack & 7u;
    return step[index] - (((step[index] - step[index + 1u]) * fraction) >> 3);
}

static void tk_pitch(tk_voice_t *v, uint32_t slot)
{
    int32_t tri = (int32_t)(tk_lfo + slot * 1073741824u) >> 16;
    int32_t spread, drift;
    uint32_t delta, base = v->base_inc;
    uint32_t factor = tk_bend_current[v->source];
    if (v->instrument == TK_INSTRUMENT_DRUM_KIT) return;
    if (tk_modulation_q8[v->source]) {
        int32_t triangle = (int32_t)tk_performance_phase >> 16;
        int32_t deviation;
        if (triangle < 0) triangle = -triangle;
        triangle = triangle * 2 - 32768;
        deviation = (triangle * tk_modulation_q8[v->source]) >> 13;
        factor = (uint32_t)(((uint64_t)factor * (uint32_t)((int32_t)TK_PITCH_ONE + deviation)) >> 24);
    }
    if (factor != TK_PITCH_ONE) {
        uint64_t scaled = ((uint64_t)base * factor) >> 24;
        /* High MIDI notes with wide positive bends exceed Nyquist. Saturate
         * the phase rate instead of wrapping a 32-bit multiply to a low note. */
        base = scaled > 2147483647u ? 2147483647u : (uint32_t)scaled;
    }
    if (v->instrument == TK_INSTRUMENT_FM) {
        v->inc_a=base; v->inc_b=base; return;
    }
    if (v->instrument == TK_INSTRUMENT_METAL) {
        /* DRIFT sweeps an inharmonic ratio from 5:4 to nearly 3:2. */
        drift = v->frozen_patch ? (int32_t)v->patch_drift : tk_drift;
        v->inc_a = base;
        v->inc_b = base + (base >> 2) + ((base >> 9) * (uint32_t)drift);
        return;
    }
    drift = v->frozen_patch ? (int32_t)v->patch_drift : tk_drift;
    if (tri < 0) tri = -tri;
    tri = tri * 2 - 32768;
    spread = 16 + drift * 3 + ((tri * drift) >> 15);
    delta = (base >> 16) * (uint32_t)spread;
    v->inc_a = base - delta;
    v->inc_b = base + delta;
}

static void tk_start(tk_voice_t *v, uint32_t cmd, uint32_t slot)
{
    static const uint8_t drum_decay[12] = {
        10u, 10u, 9u, 9u, 8u, 10u, 8u, 10u, 9u, 7u, 10u, 8u
    };
    uint32_t note = cmd & 127u, patch, timing;
    int32_t shade = tk_macro[0], fracture = tk_macro[1];
    v->base_inc = tk_inc_c9[note % 12u] >> (10u - note / 12u);
    v->level = ((cmd >> 7) & 127u) * 32u;
    v->source = (cmd >> 14) & 63u;
    v->audio_note = note;
    v->instrument = v->source >= TK_SOURCE_SEQUENCE ?
        tk_source_instrument_control[v->source] : tk_instrument_control;
    timing = v->source >= TK_SOURCE_SEQUENCE ? tk_source_patch_timing[v->source] : 0u;
    v->frozen_patch = !!(timing & 0x80000000u);
    if (v->frozen_patch) {
        uint32_t attack, release, bloom;
        patch = tk_source_patch_word[v->source];
        bloom = patch >> 24;
        attack = timing & 127u;
        release = (timing >> 7) & 127u;
        v->patch_drift = (patch >> 16) & 127u;
        v->patch_bloom = bloom;
        v->patch_drift_q8 = (int32_t)v->patch_drift << 8;
        v->patch_bloom_q8 = (int32_t)bloom << 8;
        v->patch_attack = ((72u + (128u - bloom) * 10u) * tk_envelope_rate(attack)) >> 8;
        v->patch_release = ((20u + (127u - bloom) * 3u) * tk_envelope_rate(release)) >> 8;
        v->patch_metal_attack = tk_metal_attack_step(attack);
    } else {
        v->patch_drift = v->patch_bloom = v->patch_attack = v->patch_release = 0u;
        v->patch_drift_q8 = v->patch_bloom_q8 = 0;
        v->patch_metal_attack = 0u;
        patch = 0u;
    }
    v->remembered = 0;
    v->memory_colored = 0;
    v->memory_shade = v->memory_fracture = 0;
    if (!v->frozen_patch && v->instrument == TK_INSTRUMENT_PAD &&
        (tk_memory_control & 255u) &&
        tk_memory_pick(&tk_history, note, v->source, &shade, &fracture)) {
        v->memory_shade = shade - tk_macro[0];
        v->memory_fracture = fracture - tk_macro[1];
        v->memory_colored = !!(v->memory_shade || v->memory_fracture);
    }
    v->color_shade = tk_memory_mix_q8(tk_macro[0], tk_macro[0] + v->memory_shade, (uint32_t)tk_memory_amount_q8 >> 8);
    v->color_fracture = tk_macro[1] ? tk_memory_mix_q8(tk_macro[1], tk_macro[1] + v->memory_fracture, (uint32_t)tk_memory_amount_q8 >> 8) : 0;
    if (v->frozen_patch) {
        v->color_shade = (int32_t)(v->instrument == TK_INSTRUMENT_PAD ?
            tk_source_shade_control[v->source] : (patch & 255u)) << 8;
        v->color_fracture = (int32_t)((patch >> 8) & 255u) << 8;
    }
    shade = 127 - (v->color_shade >> 8);
    v->color_alpha = 512 + ((shade * shade) >> 1);
    v->color_drive = 4096 + (v->color_fracture >> 1);
    v->phase_a = 0;
    /* Internal tracks retain their legacy random/phase seeds after the
     * external TRS channels take source slots 17..32. */
    v->phase_b = note * 521288629u +
        (v->source >= TK_SOURCE_SEQUENCE ? v->source - 16u : v->source) * 362436069u;
    v->filter_l = v->filter_r = 0;
    v->env = 0;
    v->gate = 1u;
    v->fading = v->pending = 0;
    v->ghost_gain_q15 = v->ghost_target_q15 = 32768;
    v->pressure_q8 = 0;
    v->performance_gain_q15 = tk_gain_target_q15[v->source];
    v->performance_alpha = v->color_alpha;
    v->performance_drive = v->color_drive;
    v->performance_fracture = v->color_fracture;
    v->drum_lane = note % 12u;
    v->drum_elapsed = 0u;
    v->drum_noise = (note + 1u) * 747796405u ^
                    ((v->source >= TK_SOURCE_SEQUENCE ? v->source - 16u : v->source) + 1u) * 2891336453u ^
                    ((cmd >> 22) << 21);
    if (!v->drum_noise) v->drum_noise = 1u;
    v->drum_decay = drum_decay[v->drum_lane];
    v->forge_filter_l = v->forge_filter_r = 0;
    v->forge_latched = 0u;
    if (v->instrument == TK_INSTRUMENT_DRUM_KIT) {
        v->base_inc = tk_drum_pitch[v->drum_lane];
        v->inc_a = v->base_inc * 3u;  /* opening pitch bends down on tonal hits */
        v->inc_b = v->base_inc + (v->base_inc >> 1);
        v->phase_b = 0;
        v->env = TK_ENV_FULL;        /* a strike starts immediately */
        tk_forge_control(v);
        v->inc_a = v->base_inc * 3u;
    }
    tk_pitch(v, slot);
    if (v->instrument == TK_INSTRUMENT_FM) {
        tk_fm_start(&v->fm,v->source);
        /* FM's six real envelopes control amplitude; this outer marker is
         * only the existing smooth steal/panic fade and activity lifetime. */
        v->env=TK_ENV_FULL;
    }
}

static int32_t tk_smooth(int32_t current, uint32_t target)
{
    int32_t next = (int32_t)(target > 127u ? 127u : target) * 256;
    int32_t d = next - current;
    return d < 8 && d > -8 ? next : current + d / 8;
}
static int32_t tk_color_smooth(int32_t current, int32_t target)
{
    int32_t difference = target - current;
    return difference < 8 && difference > -8 ? target : current + difference / 8;
}

/* One atomic source-word read supplies all four captured macro targets.
 * The existing SHADE LFO remains independent. No oscillator/envelope phase,
 * gate, source ownership or saved live patch is reset by a controller turn. */
static void tk_track_macro_control(tk_voice_t *s)
{
    uint32_t patch, bloom, timing;
    int32_t target_shade;
    if (!s->frozen_patch) return;
    patch = tk_source_macro_control[s->source];
    if (!(patch & 0x80000000u)) {
        /* Ordinary captured patch publication retains its note-on latch.
         * Its independent SHADE LFO remains live as before. */
        if (s->instrument == TK_INSTRUMENT_PAD || s->instrument == TK_INSTRUMENT_FM) {
            target_shade = (int32_t)tk_source_shade_control[s->source] << 8;
            s->color_shade = tk_color_smooth(s->color_shade, target_shade);
            target_shade = 127 - (s->color_shade >> 8);
            s->color_alpha = 512 + ((target_shade * target_shade) >> 1);
        }
        return;
    }
    patch &= 0x7FFFFFFFu;
    target_shade = (int32_t)(s->instrument == TK_INSTRUMENT_PAD || s->instrument == TK_INSTRUMENT_FM ?
        tk_source_shade_control[s->source] : patch & 127u) << 8;
    s->color_shade = tk_color_smooth(s->color_shade, target_shade);
    s->color_fracture = tk_color_smooth(s->color_fracture, (int32_t)((patch >> 8) & 127u) << 8);
    s->patch_drift_q8 = tk_color_smooth(s->patch_drift_q8, (int32_t)((patch >> 16) & 127u) << 8);
    s->patch_bloom_q8 = tk_color_smooth(s->patch_bloom_q8, (int32_t)(patch >> 24) << 8);
    s->patch_drift = (uint32_t)s->patch_drift_q8 >> 8;
    bloom = (uint32_t)s->patch_bloom_q8 >> 8;
    if (bloom != s->patch_bloom) {
        timing = tk_source_patch_timing[s->source];
        s->patch_bloom = bloom;
        s->patch_attack = ((72u + (128u - bloom) * 10u) * tk_envelope_rate(timing & 127u)) >> 8;
        s->patch_release = ((20u + (127u - bloom) * 3u) * tk_envelope_rate((timing >> 7) & 127u)) >> 8;
    }
    target_shade = 127 - (s->color_shade >> 8);
    s->color_alpha = 512 + ((target_shade * target_shade) >> 1);
    s->color_drive = 4096 + (s->color_fracture >> 1);
}

/* Performance colour never overwrites the saved knobs or chord history.
 * Channel and poly pressure combine by maximum, so a single pressure gesture
 * cannot double the intended depth. Default values copy existing coefficients
 * verbatim, preserving neutral pad/metal PCM including fractional smoothing. */
static void tk_voice_performance(tk_voice_t *s)
{
    uint32_t source = s->source;
    int32_t pressure = (int32_t)s->poly_pressure << 8;
    int32_t shade, fracture;
    if (tk_pressure_q8[source] > pressure) pressure = tk_pressure_q8[source];
    s->pressure_q8 = tk_color_smooth(s->pressure_q8, pressure);
    s->performance_alpha = s->color_alpha;
    s->performance_drive = s->color_drive;
    s->performance_fracture = s->color_fracture;
    if (!s->pressure_q8 && tk_brightness_q8[source] == 16384 &&
        tk_texture_q8[source] == 16384 && !tk_soft_q8[source]) return;
    shade = s->color_shade - ((s->pressure_q8 * 24) >> 7) -
        (((tk_brightness_q8[source] - 16384) * 24) >> 6) +
        ((tk_soft_q8[source] * 24) >> 7);
    shade = tk_memory_clamp_q8(shade);
    shade = 127 - (shade >> 8);
    s->performance_alpha = 512 + ((shade * shade) >> 1);
    /* A clean patch or settled FX bypass remains clean even under pressure. */
    fracture = s->color_fracture ? tk_memory_clamp_q8(s->color_fracture +
        ((s->pressure_q8 * 24) >> 7) + (((tk_texture_q8[source] - 16384) * 24) >> 6)) : 0;
    s->performance_fracture = fracture;
    s->performance_drive = 4096 + (fracture >> 1);
}
static void tk_fm_control_voice(tk_voice_t *s)
{
    uint32_t timing, refresh, bloom_scale, drift;
    if (s->instrument!=TK_INSTRUMENT_FM || s->fading || (!s->env && !s->gate)) return;
    timing=s->frozen_patch ? tk_source_patch_timing[s->source] : tk_envelope_control;
    bloom_scale=tk_envelope_rate(s->frozen_patch?s->patch_bloom:(uint32_t)(tk_macro[3]>>8));
    drift=s->frozen_patch?s->patch_drift:(uint32_t)(tk_macro[2]>>8);
    refresh=s->source>=TK_SOURCE_SEQUENCE || tk_instrument_control==TK_INSTRUMENT_FM;
    if (!tk_fm_tick(&s->fm,s->gate,s->inc_a,refresh,
        (tk_envelope_rate(timing&127u)*bloom_scale)>>8,bloom_scale,
        (tk_envelope_rate((timing>>7)&127u)*bloom_scale)>>8,drift)) s->env=0;
}

/* Q8 increment factor: timing spans about 0.7x..2x. No division or added
 * work in the per-sample envelope path; value 64 is exactly unity. */
static uint32_t tk_envelope_rate(uint32_t value)
{
    return value < 64u ? 256u + (((64u - value) * 7u) >> 2) :
           256u - ((value - 64u) << 1);
}
static void tk_remember_voice(tk_voice_t *v)
{
    int32_t shade, fracture;
    uint32_t amount = (uint32_t)tk_memory_amount_q8 >> 8;
    if (v->instrument != TK_INSTRUMENT_PAD || v->frozen_patch || v->remembered || !v->env) return;
    /* Derive from current macros/offsets, not a stale color coefficient after
     * FORGET. Mature envelopes leave a small darkness/corrosion imprint. */
    shade = tk_memory_mix_q8(tk_macro[0], tk_macro[0] + v->memory_shade, amount);
    fracture = tk_macro[1] ? tk_memory_mix_q8(tk_macro[1], tk_macro[1] + v->memory_fracture, amount) : 0;
    tk_memory_imprint_q8(shade, fracture, (uint32_t)v->env, &shade, &fracture);
    tk_memory_remember(&tk_history, v->audio_note, v->source, shade, fracture);
    v->remembered = 1;
}

/* A melodic onset leaves a short pitch marker. Repeated pitches refresh it;
 * when all slots are busy, evict the pulse nearest expiry. The pulse is an
 * audio-owned control event, not another sounding voice. */
static void tk_ghost_trigger(uint32_t pitch)
{
    uint32_t i, pick = 0u;
    if (!tk_ghost_control) return;
    for (i = 0; i < TK_VOICES; ++i) {
        if (tk_ghost_pulse[i].ticks && tk_ghost_pulse[i].pitch == pitch) {
            tk_ghost_pulse[i].ticks = TK_GHOST_PULSE_TICKS;
            return;
        }
    }
    for (i = 0; i < TK_VOICES; ++i) {
        if (!tk_ghost_pulse[i].ticks) { pick = i; break; }
        if (tk_ghost_pulse[i].ticks < tk_ghost_pulse[pick].ticks) pick = i;
    }
    tk_ghost_pulse[pick].pitch = (uint8_t)pitch;
    tk_ghost_pulse[pick].ticks = TK_GHOST_PULSE_TICKS;
}

/* Once each 32-frame control tick, choose the strongest nearby melody pulse.
 * Unison, one semitone and two semitones use 100%, 75% and 50% of depth.
 * The actual gain slews per sample so a new pulse never makes a hard step. */
static void tk_ghost_tick(void)
{
    uint32_t v, i, amount = tk_ghost_control;
    if (amount > 127u) amount = 127u;
    for (i = 0; i < TK_VOICES; ++i) {
        if (!amount) tk_ghost_pulse[i].ticks = 0u;
        else if (tk_ghost_pulse[i].ticks) tk_ghost_pulse[i].ticks--;
    }
    for (v = 0; v < TK_VOICES; ++v) {
        tk_voice_t *s = &tk_voice[v];
        int32_t target = 32768;
        if (amount && s->source == TK_SOURCE_ATMOS &&
            s->instrument != TK_INSTRUMENT_DRUM_KIT && (s->gate || s->env)) {
            for (i = 0; i < TK_VOICES; ++i) if (tk_ghost_pulse[i].ticks) {
                int32_t distance = (int32_t)s->audio_note - (int32_t)tk_ghost_pulse[i].pitch;
                int32_t weight, candidate;
                if (distance < 0) distance = -distance;
                if (distance > 2) continue;
                weight = 4 - distance; /* 4, 3 or 2 */
                candidate = 32768 - ((int32_t)amount * 160 * weight >> 2);
                if (candidate < target) target = candidate;
            }
        }
        s->ghost_target_q15 = target;
    }
}

static void tk_control(const tk_params_t *p)
{
    uint32_t v, envelope = tk_envelope_control;
    int32_t shade, bloom;
    for (v=0; v<TK_FM_BANKS; ++v) tk_fm_read(v);
    for (v = 0; v < TK_SOURCES; ++v) {
        uint32_t control = tk_source_control[v];
        uint32_t performance = tk_source_performance[v], timbre = tk_source_timbre[v];
        uint32_t range = tk_source_bend_range[v];
        uint32_t volume = performance & 127u, expression = (performance >> 7) & 127u;
        int32_t distance;
        if ((control & ~TK_CONTROLLER_MASK) != (tk_source_seen[v] & ~TK_CONTROLLER_MASK)) {
            tk_bend_current[v] = TK_PITCH_ONE;
            tk_bend_target[v] = tk_bend_scale_range(control & TK_BEND_MASK, range);
        } else if ((control & TK_BEND_MASK) != (tk_source_seen[v] & TK_BEND_MASK) ||
                   range != tk_bend_range_seen[v]) {
            tk_bend_target[v] = tk_bend_scale_range(control & TK_BEND_MASK, range);
        }
        tk_bend_range_seen[v] = range;
        tk_source_seen[v] = control;
        distance = (int32_t)tk_bend_target[v] - (int32_t)tk_bend_current[v];
        if (distance)
            tk_bend_current[v] = distance > -8 && distance < 8 ?
                tk_bend_target[v] : (uint32_t)((int32_t)tk_bend_current[v] + distance / 8);
        /* The many independent external channels cost only a mailbox check
         * once settled; no gain division or six colour slews on idle channels. */
        if (performance == tk_performance_seen[v] && timbre == tk_timbre_seen[v] &&
            !tk_performance_slewing[v]) continue;
        tk_performance_seen[v] = performance;
        tk_timbre_seen[v] = timbre;
        tk_modulation_q8[v] = tk_smooth(tk_modulation_q8[v], (performance >> 14) & 127u);
        tk_pressure_q8[v] = tk_smooth(tk_pressure_q8[v], (performance >> 21) & 127u);
        tk_soft_q8[v] = tk_smooth(tk_soft_q8[v], (performance & (1u << 28)) ? 127u : 0u);
        tk_brightness_q8[v] = tk_smooth(tk_brightness_q8[v], timbre & 127u);
        tk_texture_q8[v] = tk_smooth(tk_texture_q8[v], (timbre >> 7) & 127u);
        /* Two seven-bit controls combine without amplification; soft pedal
         * adds up to 25% attenuation. Division is bounded 32-bit, per control. */
        tk_gain_target_q15[v] = (int32_t)((volume * expression * 32768u + 8064u) / 16129u);
        if (tk_soft_q8[v]) tk_gain_target_q15[v] =
            (tk_gain_target_q15[v] * (32768 - (tk_soft_q8[v] >> 2))) >> 15;
        tk_performance_slewing[v] =
            tk_modulation_q8[v] != (int32_t)(((performance >> 14) & 127u) << 8) ||
            tk_pressure_q8[v] != (int32_t)(((performance >> 21) & 127u) << 8) ||
            tk_soft_q8[v] != ((performance & (1u << 28)) ? 32512 : 0) ||
            tk_brightness_q8[v] != (int32_t)((timbre & 127u) << 8) ||
            tk_texture_q8[v] != (int32_t)(((timbre >> 7) & 127u) << 8);
    }
    tk_performance_phase += 15582648u;     /* ~5 Hz; bounded vibrato, no extra voice */
    tk_macro[0] = tk_smooth(tk_macro[0], p->shade);
    tk_macro[1] = tk_smooth(tk_macro[1], p->fracture);
    tk_macro[2] = tk_smooth(tk_macro[2], p->drift);
    tk_macro[3] = tk_smooth(tk_macro[3], p->bloom);
    tk_forge_fx_q15 = tk_color_smooth(tk_forge_fx_q15, tk_fx_control ? 32768 : 0);
    tk_memory_amount_q8 = tk_smooth(tk_memory_amount_q8, tk_memory_control & 255u);
    shade = 127 - (tk_macro[0] >> 8);
    bloom = tk_macro[3] >> 8;
    tk_alpha = 512 + ((shade * shade) >> 1); /* ~110..2100 Hz at 44100 */
    tk_drive = 4096 + (tk_macro[1] >> 1);   /* Q8 macro -> continuous Q12 gain */
    tk_drift = tk_macro[2] >> 8;
    tk_attack = ((72 + (128 - bloom) * 10) *
                 (int32_t)tk_envelope_rate(envelope & 127u)) >> 8;
    tk_metal_attack = tk_metal_attack_step(envelope & 127u);
    tk_release = ((20 + (127 - bloom) * 3) *
                  (int32_t)tk_envelope_rate((envelope >> 7) & 127u)) >> 8;
    tk_lfo += 233728u;                     /* ~0.075 Hz, updated every 32 frames */
    tk_ghost_tick();
    for (v = 0; v < TK_VOICES; ++v) {
        tk_voice_t *s = &tk_voice[v];
        uint32_t amount = (uint32_t)tk_memory_amount_q8 >> 8;
        int32_t target_shade, target_fracture;
        tk_track_macro_control(s);
        if (s->instrument == TK_INSTRUMENT_DRUM_KIT) {
            tk_voice_performance(s);
            if (s->env || s->gate) tk_forge_control(s);
            continue;
        }
        tk_pitch(s, v);
        if (s->frozen_patch) {
            tk_voice_performance(s);
            tk_fm_control_voice(s);
            continue;
        }
        target_shade = tk_memory_mix_q8(tk_macro[0], tk_macro[0] + s->memory_shade, amount);
        target_fracture = tk_macro[1] ? tk_memory_mix_q8(tk_macro[1], tk_macro[1] + s->memory_fracture, amount) : 0;
        if (amount && s->memory_colored) {
            s->color_shade = tk_color_smooth(s->color_shade, target_shade);
            s->color_fracture = tk_color_smooth(s->color_fracture, target_fracture);
        } else {
            s->color_shade = tk_macro[0]; s->color_fracture = tk_macro[1];
        }
        /* A settled live FX bypass wins over any remembered corrosion. */
        if (!tk_macro[1]) s->color_fracture = 0;
        shade = 127 - (s->color_shade >> 8);
        s->color_alpha = 512 + ((shade * shade) >> 1);
        s->color_drive = 4096 + (s->color_fracture >> 1);
        s->memory_shade = tk_memory_decay_q8(s->memory_shade);
        s->memory_fracture = tk_memory_decay_q8(s->memory_fracture);
        if (!s->memory_shade && !s->memory_fracture &&
            s->color_shade == tk_macro[0] && s->color_fracture == tk_macro[1])
            s->memory_colored = 0;
        tk_voice_performance(s);
        tk_fm_control_voice(s);
    }
}

static void tk_init(void)
{
    uint32_t v;
    tk_fm_init();
    tk_mono_init();
    for (v = 0; v < TK_VOICES; ++v) {
        tk_voice_t *s = &tk_voice[v];
        s->command = s->tail = s->age = s->key_down = s->seen = s->pending = 0;
        s->fm_tail=0u; s->allocated_instrument=TK_INSTRUMENT_PAD;
        s->phase_a = s->phase_b = s->base_inc = s->inc_a = s->inc_b = 0;
        s->env = s->filter_l = s->filter_r = 0;
        s->level = s->gate = s->fading = 0;
        s->source = 0;
        s->audio_note = s->remembered = s->memory_colored = 0;
        s->memory_shade = s->memory_fracture = s->color_shade = s->color_fracture = 0;
        s->color_alpha = s->color_drive = 0;
        s->instrument = s->drum_lane = s->drum_decay = 0;
        s->drum_elapsed = s->drum_noise = 0;
        s->forge_decay_q8 = 256u;
        s->forge_hat_alpha = 2048u;
        s->forge_clap_width = 150u;
        s->forge_wire_q8 = s->forge_bite_q8 = 0;
        s->forge_tone_gain = s->forge_noise_gain = 4096;
        s->forge_filter_l = s->forge_filter_r = 0;
        s->forge_latched = 0u;
        s->forge_bite_target_q8 = 0;
        s->frozen_patch = s->patch_drift = s->patch_bloom = 0;
        s->patch_drift_q8 = s->patch_bloom_q8 = 0;
        s->patch_attack = s->patch_release = 0;
        s->patch_metal_attack = 0u;
        s->ghost_gain_q15 = s->ghost_target_q15 = 32768;
        s->sostenuto_latched = s->poly_pressure = 0u;
        s->pressure_q8 = 0;
        s->performance_gain_q15 = 32768;
        s->performance_alpha = s->performance_drive = s->performance_fracture = 0;
    }
    for (v = 0; v < TK_VOICES; ++v)
        tk_ghost_pulse[v].ticks = tk_ghost_pulse[v].pitch = 0u;
    for (v = 0; v < TK_SOURCES; ++v) {
        tk_source_control[v] = tk_source_seen[v] = TK_BEND_CENTER;
        tk_bend_target[v] = tk_bend_current[v] = TK_PITCH_ONE;
        tk_source_performance[v] = TK_PERFORMANCE_DEFAULT;
        tk_source_bend_range[v] = tk_bend_range_seen[v] = 2u;
        tk_source_timbre[v] = 64u | (64u << 7);
        tk_modulation_q8[v] = tk_pressure_q8[v] = tk_soft_q8[v] = 0;
        tk_gain_target_q15[v] = 32768;
        tk_brightness_q8[v] = tk_texture_q8[v] = 16384;
        tk_performance_seen[v] = TK_PERFORMANCE_DEFAULT;
        tk_timbre_seen[v] = 64u | (64u << 7);
        tk_performance_slewing[v] = 0u;
        tk_source_instrument_control[v] = TK_INSTRUMENT_PAD;
        tk_source_patch_word[v] = tk_source_patch_timing[v] = 0u;
        tk_source_macro_control[v] = 0u;
        tk_source_shade_control[v] = 0u;
    }
    tk_age = tk_control_phase = tk_lfo = 0;
    tk_performance_phase = 0u;
    tk_memory_control = tk_memory_seen = 0;
    tk_ghost_control = 0;
    tk_instrument_control = TK_INSTRUMENT_PAD;
    tk_fx_control = 1u;
    tk_forge_fx_q15 = 32768;
    tk_memory_amount_q8 = 0;
    tk_memory_init(&tk_history);
    tk_set_envelope(64u, 64u);
    tk_macro[0] = 88 * 256;
    tk_macro[1] = 40 * 256;
    tk_macro[2] = 32 * 256;
    tk_macro[3] = 80 * 256;
    tk_alpha = 1272;
    tk_drive = 9216;
    tk_attack = 552;
    tk_metal_attack = tk_metal_attack_step(64u);
    tk_release = 161;
    tk_drift = 32;
}

static void tk_note_off_raw(uint32_t note, uint32_t source)
{
    uint32_t v;
    if (note > 127u || source >= TK_SOURCES) return;
    for (v = 0; v < TK_VOICES; ++v) {
        uint32_t cmd = tk_voice[v].command;
        if ((cmd & 127u) == note && ((cmd >> 14) & 63u) == source) {
            tk_voice[v].key_down = 0;
            if ((cmd & TK_GATE) && !(tk_source_control[source] & TK_SUSTAIN) &&
                !((tk_source_control[source] & TK_SOSTENUTO) && tk_voice[v].sostenuto_latched))
                tk_voice[v].command = tk_revision(cmd) | (cmd & (TK_GATE - 1u));
        }
    }
}

/* A generated note may borrow a released live tail, but never replaces an
 * active live key/pedal note. Live notes instead prefer stealing a generated
 * voice over an active live voice. Return zero if no safe slot is available. */
static uint32_t tk_fm_voice_count(void)
{
    uint32_t v,count=0u;
    for (v=0; v<TK_VOICES; ++v) if (tk_voice[v].fm_tail ||
        ((tk_voice[v].command&TK_GATE) && tk_voice[v].allocated_instrument==TK_INSTRUMENT_FM)) ++count;
    return count;
}
static int tk_note_on_raw(uint32_t note, uint32_t vel, uint32_t source)
{
    uint32_t v, pick = TK_VOICES, best_kind = 5u, oldest = 0;
    uint32_t cmd;
    uint32_t instrument,limit;
    if (note > 127u || source >= TK_SOURCES) return 0;
    if (!vel) { tk_note_off_raw(note, source); return 0; }
    if (vel > 127u) vel = 127u;
    instrument=source>=TK_SOURCE_SEQUENCE?tk_source_instrument_control[source]:tk_instrument_control;
    limit=instrument==TK_INSTRUMENT_FM && tk_fm_voice_count()>=TK_FM_VOICE_LIMIT;
    for (v = 0; v < TK_VOICES; ++v) {
        uint32_t c = tk_voice[v].command;
        uint32_t held_source = (c >> 14) & 63u;
        uint32_t kind = (c & TK_GATE) ? tk_voice[v].key_down ? 3u : 2u :
            tk_voice[v].tail ? 1u : 0u;
        uint32_t age = tk_age - tk_voice[v].age;
        if (limit && !(tk_voice[v].fm_tail ||
            ((c&TK_GATE) && tk_voice[v].allocated_instrument==TK_INSTRUMENT_FM))) continue;
        if (limit && (c&TK_GATE) && tk_voice[v].allocated_instrument!=TK_INSTRUMENT_FM) continue;
        if (tk_mono && source<TK_SOURCE_SEQUENCE && (c&TK_GATE) && held_source==source) {
            pick=v; break;
        }
        if ((c & TK_GATE) && (c & 127u) == note && ((c >> 14) & 63u) == source) {
            pick = v;
            break;
        }
        if (kind >= 2u) {
            if (!tk_source_is_live(source) && tk_source_is_live(held_source))
                continue;               /* generated events cannot evict live notes */
            if (tk_source_is_live(source))
                kind = !tk_source_is_live(held_source) ? 2u : kind + 1u;
        }
        if (kind < best_kind || (kind == best_kind && age > oldest)) {
            pick = v;
            best_kind = kind;
            oldest = age;
        }
    }
    if (pick == TK_VOICES) return 0;
    tk_voice[pick].age = ++tk_age;
    tk_voice[pick].key_down = 1u;
    tk_voice[pick].sostenuto_latched = 0u;
    tk_voice[pick].poly_pressure = 0u;
    tk_voice[pick].allocated_instrument=instrument;
    if (tk_mono && source<TK_SOURCE_SEQUENCE) for (v=0; v<TK_VOICES; ++v) {
        uint32_t previous=tk_voice[v].command;
        if (v!=pick && (previous&TK_GATE) && ((previous>>14)&63u)==source) {
            tk_voice[v].key_down=tk_voice[v].sostenuto_latched=0u;
            tk_voice[v].command=tk_revision(previous)|(previous&(TK_GATE-1u))|TK_PANIC;
        }
    }
    cmd = tk_revision(tk_voice[pick].command) | note | (vel << 7) | (source << 14) | TK_GATE;
    tk_voice[pick].command = cmd;
    return 1;
}
static void tk_note_off(uint32_t note,uint32_t source)
{
    if (note>127u || source>=TK_SOURCES) return;
    if (source<TK_SOURCE_SEQUENCE) {
        tk_mono_key_up(note,source);
        if (tk_mono) { tk_mono_refresh(source); return; }
    }
    tk_note_off_raw(note,source);
}
static int tk_note_on(uint32_t note,uint32_t vel,uint32_t source)
{
    uint32_t v,repeat=0u;
    if (note>127u || source>=TK_SOURCES) return 0;
    if (!vel) { tk_note_off(note,source); return 0; }
    if (vel>127u) vel=127u;
    if (source<TK_SOURCE_SEQUENCE) {
        if (tk_mono) for (v=0; v<TK_VOICES; ++v) {
            uint32_t command=tk_voice[v].command;
            if ((command&TK_GATE) && (command&127u)==note && ((command>>14)&63u)==source) repeat=1u;
        }
        tk_mono_push(note,vel,source);
        if (tk_mono) {
            int accepted=tk_mono_refresh(source);
            /* Repeated winner notes remain real retriggers. */
            return repeat?tk_note_on_raw(note,vel,source):accepted;
        }
    }
    return tk_note_on_raw(note,vel,source);
}

/* Main-owned command snapshots; counts pedal-held live voices too, because
 * those slots are still protected from generated note allocation. */
static uint32_t tk_live_voice_count(void)
{
    uint32_t v, count = 0;
    for (v = 0; v < TK_VOICES; ++v) {
        uint32_t cmd = tk_voice[v].command;
        if ((cmd & TK_GATE) && tk_source_is_live((cmd >> 14) & 63u)) count++;
    }
    return count;
}
static int tk_note_is_held(uint32_t note, uint32_t source)
{
    uint32_t v;
    for (v = 0; v < TK_VOICES; v++) {
        uint32_t cmd = tk_voice[v].command;
        if ((cmd & TK_GATE) && (cmd & 127u) == note &&
            ((cmd >> 14) & 63u) == source) return 1;
    }
    return 0;
}

/* Main-side release uses physical ownership, never audio state. A stolen slot
 * loses its old key_down state, so pedal lift cannot revive the previous note.
 */
static void tk_release_sustained(uint32_t source)
{
    uint32_t v;
    for (v = 0; v < TK_VOICES; ++v) {
        uint32_t cmd = tk_voice[v].command;
        if ((cmd & TK_GATE) && !tk_voice[v].key_down && ((cmd >> 14) & 63u) == source &&
            !(tk_source_control[source] & TK_SUSTAIN) &&
            !((tk_source_control[source] & TK_SOSTENUTO) && tk_voice[v].sostenuto_latched))
            tk_voice[v].command = tk_revision(cmd) | (cmd & (TK_GATE - 1u));
    }
}

static void tk_sustain(uint32_t source, uint32_t value)
{
    uint32_t control;
    if (source >= TK_SOURCES) return;
    control = tk_source_control[source];
    tk_source_control[source] = value >= 64u ? control | TK_SUSTAIN : control & ~TK_SUSTAIN;
    tk_mono_pedal(source,TK_MONO_SUSTAIN,value>=64u);
    if (value < 64u) tk_release_sustained(source);
    tk_mono_refresh(source);
}

/* Only voices physically down at the rising edge are captured. A later key,
 * retrigger or stolen slot cannot inherit the old latch. Pedals cooperate:
 * lifting either releases a key only when neither pedal still owns it. */
static void tk_sostenuto(uint32_t source, uint32_t value)
{
    uint32_t v, control;
    if (source >= TK_SOURCES) return;
    control = tk_source_control[source];
    if (!!(control&TK_SOSTENUTO)!=(value>=64u)) tk_mono_pedal(source,TK_MONO_SOSTENUTO,value>=64u);
    if (value >= 64u) {
        if (!(control & TK_SOSTENUTO)) for (v = 0; v < TK_VOICES; ++v) {
            uint32_t cmd = tk_voice[v].command;
            if (((cmd >> 14) & 63u) == source)
                tk_voice[v].sostenuto_latched = !!((cmd & TK_GATE) && tk_voice[v].key_down);
        }
        tk_source_control[source] = control | TK_SOSTENUTO;
    } else {
        tk_source_control[source] = control & ~TK_SOSTENUTO;
        for (v = 0; v < TK_VOICES; ++v)
            if (((tk_voice[v].command >> 14) & 63u) == source) tk_voice[v].sostenuto_latched = 0u;
        tk_release_sustained(source);
    }
    tk_mono_refresh(source);
}

static void tk_performance_value(uint32_t source, uint32_t value, uint32_t shift)
{
    uint32_t word;
    if (source >= TK_SOURCES) return;
    if (value > 127u) value = 127u;
    word = tk_source_performance[source];
    tk_source_performance[source] = (word & ~(127u << shift)) | (value << shift);
}
static void tk_volume(uint32_t source, uint32_t value)
{
    tk_performance_value(source, value, 0u);
}
static void tk_expression(uint32_t source, uint32_t value)
{
    tk_performance_value(source, value, 7u);
}
static void tk_modulation(uint32_t source, uint32_t value)
{
    tk_performance_value(source, value, 14u);
}
static void tk_channel_pressure(uint32_t source, uint32_t value)
{
    tk_performance_value(source, value, 21u);
}
static void tk_soft_pedal(uint32_t source, uint32_t value)
{
    uint32_t word;
    if (source >= TK_SOURCES) return;
    word = tk_source_performance[source];
    tk_source_performance[source] = value >= 64u ? word | (1u << 28) : word & ~(1u << 28);
}
static void tk_poly_pressure(uint32_t note, uint32_t source, uint32_t value)
{
    uint32_t v;
    if (note > 127u || source >= TK_SOURCES) return;
    if (value > 127u) value = 127u;
    for (v = 0; v < TK_VOICES; ++v) {
        uint32_t cmd = tk_voice[v].command;
        if ((cmd & TK_GATE) && (cmd & 127u) == note && ((cmd >> 14) & 63u) == source)
            tk_voice[v].poly_pressure = value;
    }
}
static void tk_brightness(uint32_t source, uint32_t value)
{
    uint32_t word;
    if (source >= TK_SOURCES) return;
    if (value > 127u) value = 127u;
    word = tk_source_timbre[source];
    tk_source_timbre[source] = (word & ~127u) | value;
}
static void tk_texture(uint32_t source, uint32_t value)
{
    uint32_t word;
    if (source >= TK_SOURCES) return;
    if (value > 127u) value = 127u;
    word = tk_source_timbre[source];
    tk_source_timbre[source] = (word & ~(127u << 7)) | (value << 7);
}
static void tk_set_bend_range(uint32_t source, uint32_t semitones)
{
    if (source < TK_SOURCES) tk_source_bend_range[source] = semitones > 24u ? 24u : semitones;
}

static void tk_pitch_bend(uint32_t source, uint32_t value14)
{
    uint32_t control;
    if (source >= TK_SOURCES) return;
    if (value14 > TK_BEND_MASK) value14 = TK_BEND_MASK;
    control = tk_source_control[source];
    tk_source_control[source] = (control & ~TK_BEND_MASK) | value14;
}

static void tk_all_notes_off(uint32_t source)
{
    uint32_t v;
    if (source >= TK_SOURCES) return;
    if (source<TK_SOURCE_SEQUENCE) {
        for (v=0; v<128u; ++v) tk_mono_key_up(v,source);
    }
    for (v = 0; v < TK_VOICES; ++v) {
        uint32_t cmd = tk_voice[v].command;
        if (((cmd >> 14) & 63u) == source) tk_voice[v].key_down = 0;
    }
    if (!(tk_source_control[source] & TK_SUSTAIN)) tk_release_sustained(source);
    tk_mono_refresh(source);
}

static void tk_all_sound_off(uint32_t source)
{
    uint32_t v;
    if (source >= TK_SOURCES) return;
    tk_mono_clear(source);
    for (v = 0; v < TK_VOICES; ++v) {
        uint32_t cmd = tk_voice[v].command;
        if (((cmd >> 14) & 63u) != source) continue;
        tk_voice[v].key_down = 0;
        tk_voice[v].sostenuto_latched = tk_voice[v].poly_pressure = 0u;
        tk_voice[v].command = tk_revision(cmd) | (cmd & (TK_GATE - 1u)) | TK_PANIC;
    }
}

static void tk_reset_controllers(uint32_t source)
{
    uint32_t control, v;
    if (source >= TK_SOURCES) return;
    control = tk_source_control[source];
    /* MIDI RP-015: preserve Volume7, Sound Controllers71/74 and RPN values;
     * reset expression, modulation, pressure, pedals and pitch position. */
    tk_source_performance[source] = (tk_source_performance[source] & 127u) | (127u << 7);
    tk_source_control[source] = ((control + TK_CONTROLLER_REV) & ~TK_CONTROLLER_MASK) | TK_BEND_CENTER;
    tk_mono_pedal(source,TK_MONO_SUSTAIN,0u);
    tk_mono_pedal(source,TK_MONO_SOSTENUTO,0u);
    for (v = 0; v < TK_VOICES; ++v) if (((tk_voice[v].command >> 14) & 63u) == source)
        tk_voice[v].sostenuto_latched = tk_voice[v].poly_pressure = 0u;
    tk_release_sustained(source);
    tk_mono_refresh(source);
}

static void tk_panic(void)
{
    uint32_t v;
    tk_forget_memory();
    for (v = 0; v < TK_SOURCES; ++v) {
        tk_mono_clear(v);
        uint32_t control = tk_source_control[v];
        tk_source_performance[v] = TK_PERFORMANCE_DEFAULT;
        tk_source_timbre[v] = 64u | (64u << 7);
        tk_source_bend_range[v] = 2u;
        tk_source_control[v] = ((control + TK_CONTROLLER_REV) & ~TK_CONTROLLER_MASK) | TK_BEND_CENTER;
    }
    for (v = 0; v < TK_VOICES; ++v) {
        uint32_t cmd = tk_voice[v].command;
        tk_voice[v].key_down = 0;
        tk_voice[v].sostenuto_latched = tk_voice[v].poly_pressure = 0u;
        tk_voice[v].command = tk_revision(cmd) | (cmd & (TK_GATE - 1u)) | TK_PANIC;
    }
}

static uint32_t tk_active_notes(uint32_t base)
{
    uint32_t v, mask = 0;
    if (base > 127u) return 0;
    for (v = 0; v < TK_VOICES; ++v) {
        uint32_t cmd = tk_voice[v].command, note = cmd & 127u;
        if ((cmd & TK_GATE) && note >= base && note - base < 27u)
            mask |= 1u << (note - base);
    }
    return mask;
}

static int tk_quiet(void)
{
    uint32_t v;
    for (v = 0; v < TK_VOICES; ++v)
        if ((tk_voice[v].command & TK_GATE) || tk_voice[v].tail) return 0;
    return 1;
}

/* Twelve pitch-class drum lanes. Every octave repeats the same kit: C kick,
 * C# sub, D snare, D# clap, E/F short/open hats, F# alloy hat, G/G# toms,
 * A rim, A# bell, B broken-clock click. All sample arithmetic stays 32-bit.
 * Separate shell/alloy and wire/noise stems make WIRE musically role-aware. */
static void tk_drum_wave(tk_voice_t *s, int32_t *left, int32_t *right)
{
    uint32_t x = s->drum_noise, lane = s->drum_lane;
    int32_t n, n2, a = 0, b = 0, l = 0, r = 0;
    int32_t tone_l = 0, tone_r = 0, noise_l = 0, noise_r = 0;
    int32_t wire = s->forge_wire_q8, bite = s->forge_bite_q8;
    if (lane != 11u || !(s->drum_elapsed & 15u)) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        s->drum_noise = x;
    }
    n = (int32_t)(x >> 16) - 32768;
    n2 = (int32_t)(x & 65535u) - 32768;
    s->phase_a += s->inc_a;
    s->phase_b += s->inc_b;
    if (lane == 0u || lane == 1u || lane == 7u || lane == 8u) {
        uint32_t gap = s->inc_a - s->base_inc;
        s->inc_a -= (gap >> 7) + !!gap; /* strike bends toward a fixed body pitch */
    }
    if (lane <= 3u || (lane >= 6u && lane <= 10u) ||
        (wire < 0 && lane >= 4u && lane <= 5u)) {
        a = tk_sine(s->phase_a);
        b = tk_sine(s->phase_b);
    }
    switch (lane) {
    case 0u: /* kick: swept membrane plus a short click */
        tone_l = (3 * a + b) >> 2;
        tone_r = (a + 3 * b) >> 2;
        if (s->drum_elapsed < 96u) { noise_l = n >> 3; noise_r = n2 >> 3; }
        break;
    case 1u: /* slower, lower sub impact */
        tone_l = (3 * a) >> 2; tone_r = (3 * b) >> 2;
        break;
    case 2u: /* snare wire over a pitched shell */
        tone_l = a >> 2; tone_r = b >> 2;
        noise_l = n >> 1; noise_r = n2 >> 1;
        break;
    case 3u: /* three rough little clap bursts */
        if (s->drum_elapsed < 1600u && (s->drum_elapsed & 511u) < s->forge_clap_width) {
            noise_l = n >> 1; noise_r = n2 >> 1;
        } else { noise_l = n >> 3; noise_r = n2 >> 3; }
        break;
    case 4u: case 5u: /* high-passed closed/open hats */
        s->filter_l += ((n - s->filter_l) * (int32_t)s->forge_hat_alpha) >> 15;
        s->filter_r += ((n2 - s->filter_r) * (int32_t)s->forge_hat_alpha) >> 15;
        noise_l = (n - s->filter_l) >> 1;
        noise_r = (n2 - s->filter_r) >> 1;
        break;
    case 6u: /* inharmonic alloy tick */
        tone_l = tone_r = (a * b) >> 16;
        noise_l = n >> 2; noise_r = n2 >> 2;
        break;
    case 7u: case 8u: /* low and high pitch-bent toms */
        tone_l = (3 * a + b) >> 2; tone_r = (a + 3 * b) >> 2;
        break;
    case 9u: /* rim */
        tone_l = a >> 1; tone_r = b >> 1;
        noise_l = n >> 2; noise_r = n2 >> 2;
        break;
    case 10u: /* struck inharmonic bell */
        tone_l = ((a * b) >> 16) + (a >> 2);
        tone_r = ((a * b) >> 16) + (b >> 2);
        break;
    default: /* sample-held digital grit */
        tone_l = (s->phase_a & 0x80000000u) ? 11000 : -11000;
        tone_r = (s->phase_b & 0x80000000u) ? 11000 : -11000;
        noise_l = n >> 2; noise_r = n2 >> 2;
        break;
    }
    l = tone_l + noise_l; r = tone_r + noise_r;
    if (wire) {
        l = ((tone_l * s->forge_tone_gain) >> 12) + ((noise_l * s->forge_noise_gain) >> 12);
        r = ((tone_r * s->forge_tone_gain) >> 12) + ((noise_r * s->forge_noise_gain) >> 12);
        /* Sub/toms/bell acquire wire above centre; noise-only hats and clap
         * reveal a tuned alloy core below centre. Neutral adds neither. */
        if (wire > 0 && (lane == 1u || lane == 7u || lane == 8u || lane == 10u)) {
            l += (n * wire) >> 16; r += (n2 * wire) >> 16;
        } else if (wire < 0 && lane >= 3u && lane <= 5u) {
            int32_t alloy = lane == 3u ? a >> 1 : (a * b) >> 16;
            l += (alloy * -wire) >> 15; r += (alloy * -wire) >> 15;
        }
    }
    if (l > 32760) l = 32760; else if (l < -32760) l = -32760;
    if (r > 32760) r = 32760; else if (r < -32760) r = -32760;
    if (bite < 0) {
        /* Sanding is a spectral/transient change, not a master-volume boost. */
        s->forge_filter_l += (l - s->forge_filter_l) >> 3;
        s->forge_filter_r += (r - s->forge_filter_r) >> 3;
        l += ((s->forge_filter_l - l) * -bite) >> 14;
        r += ((s->forge_filter_r - r) * -bite) >> 14;
    } else {
        s->forge_filter_l = l; s->forge_filter_r = r;
        if (bite) {
            int32_t drive = 4096 + (bite >> 2);
            l = tk_fold_color(l, drive, bite);
            r = tk_fold_color(r, drive, bite);
        }
    }
    *left = l; *right = r;
    ++s->drum_elapsed;
}

static void tk_render(int32_t *out, uint32_t frames, const tk_params_t *p)
{
    static const tk_params_t defaults = {88, 40, 32, 80};
    uint32_t v, i;
    if (!out || !frames) return;
    if (!p) p = &defaults;
    {
        uint32_t control = tk_memory_control;
        if ((control & ~255u) != (tk_memory_seen & ~255u)) {
            tk_memory_init(&tk_history);
            for (v = 0; v < TK_VOICES; ++v) {
                tk_voice[v].memory_shade = tk_voice[v].memory_fracture = 0;
                /* FORGET discards the current gesture as well as past cells.
                 * A held voice cannot immediately re-imprint on replacement;
                 * the next tk_start() makes a new gesture recordable again. */
                tk_voice[v].remembered = 1;
            }
        }
        tk_memory_seen = control;
    }
    /* Remember every outgoing chord member before starting any new member. */
    for (v = 0; v < TK_VOICES; ++v) {
        tk_voice_t *s = &tk_voice[v];
        uint32_t cmd = s->command;
        if (cmd != s->seen && !(cmd & TK_PANIC) && s->gate)
            tk_remember_voice(s);
    }
    for (v = 0; v < TK_VOICES; ++v) {
        tk_voice_t *s = &tk_voice[v];
        uint32_t cmd = s->command;         /* one atomic event snapshot per block */
        if (cmd == s->seen) continue;
        s->seen = cmd;
        if (cmd & TK_GATE) {
            uint32_t source = (cmd >> 14) & 63u;
            if (source <= TK_SOURCE_SEQUENCE &&
                (source == TK_SOURCE_SEQUENCE ? tk_source_instrument_control[source] :
                    tk_instrument_control) != TK_INSTRUMENT_DRUM_KIT)
                tk_ghost_trigger(cmd & 127u);
            if (s->env) {
                s->pending = cmd;
                s->fading = 1u;           /* old timbre fades before new pitch starts */
            } else tk_start(s, cmd, v);
        } else {
            s->pending = 0;
            s->gate = 0;
            s->fading = (cmd & TK_PANIC) ? 1u : 0u;
        }
    }
    for (i = 0; i < frames; ++i) {
        if (!(tk_control_phase++ & 31u)) tk_control(p);
        for (v = 0; v < TK_VOICES; ++v) {
            tk_voice_t *s = &tk_voice[v];
            int32_t a, b, l, r, env;
            if (!s->env && !s->gate && !s->pending) continue;
            if (s->performance_gain_q15 != tk_gain_target_q15[s->source]) {
                int32_t difference = tk_gain_target_q15[s->source] - s->performance_gain_q15;
                s->performance_gain_q15 += difference < 0 ?
                    -(((-difference) >> 8) + 1) : (difference >> 8) + 1;
            }
            if (s->source == TK_SOURCE_ATMOS && s->ghost_gain_q15 != s->ghost_target_q15) {
                int32_t difference = s->ghost_target_q15 - s->ghost_gain_q15;
                s->ghost_gain_q15 += difference < 0 ?
                    -(((-difference) >> 8) + 1) : (difference >> 12) + 1;
            }
            if (s->fading) {
                s->env -= 32768;          /* full-level steal/panic fade <= 256 frames */
                if (s->env <= 0) {
                    uint32_t next = s->pending;
                    s->env = 0;
                    s->gate = s->fading = s->pending = 0;
                    if (next & TK_GATE) tk_start(s, next, v);
                }
            } else if (s->instrument == TK_INSTRUMENT_FM) {
                /* Operator envelopes are advanced at the 32-frame control tick. */
            } else if (s->instrument == TK_INSTRUMENT_DRUM_KIT) {
                s->env -= (((s->env >> s->drum_decay) * (int32_t)s->forge_decay_q8) >> 8) + 1;
                if (s->env <= 256) { s->env = 0; s->gate = 0; }
            } else if (s->instrument == TK_INSTRUMENT_METAL) {
                if (s->gate) {
                    s->env += s->frozen_patch ?
                        (int32_t)s->patch_metal_attack : (int32_t)tk_metal_attack;
                    if (s->env > TK_ENV_FULL) s->env = TK_ENV_FULL;
                } else {
                    uint32_t bloom = s->frozen_patch ? s->patch_bloom : (uint32_t)(tk_macro[3] >> 8);
                    uint32_t decay = 8u + (bloom >> 5);
                    s->env -= (s->env >> decay) + 1;
                    if (s->env < 256) s->env = 0;
                }
            } else if (s->gate) {
                s->env += s->frozen_patch ? (int32_t)s->patch_attack : tk_attack;
                if (s->env > TK_ENV_FULL) s->env = TK_ENV_FULL;
            } else {
                s->env -= s->frozen_patch ? (int32_t)s->patch_release : tk_release;
                if (s->env < 0) s->env = 0;
            }
            if (s->instrument == TK_INSTRUMENT_FM) {
                a=tk_fm_wave(&s->fm);
                l=r=tk_fold_color(a,s->performance_drive,s->performance_fracture);
                s->filter_l+=((l-s->filter_l)*s->performance_alpha)>>15;
                s->filter_r+=((r-s->filter_r)*s->performance_alpha)>>15;
                l=s->filter_l; r=s->filter_r;
            } else if (s->instrument == TK_INSTRUMENT_DRUM_KIT) {
                tk_drum_wave(s, &l, &r);
            } else if (s->instrument == TK_INSTRUMENT_METAL) {
                int32_t alpha = s->performance_alpha * 3;
                s->phase_a += s->inc_a;
                s->phase_b += s->inc_b;
                a = tk_sine(s->phase_a);
                b = tk_sine(s->phase_b);
                l = tk_fold_color(((a * b) >> 16) + (a >> 2), s->performance_drive, s->performance_fracture);
                r = tk_fold_color(((a * b) >> 16) + (b >> 2), s->performance_drive, s->performance_fracture);
                if (l > 32760) l = 32760; else if (l < -32760) l = -32760;
                if (r > 32760) r = 32760; else if (r < -32760) r = -32760;
                s->filter_l += ((l - s->filter_l) * alpha) >> 15;
                s->filter_r += ((r - s->filter_r) * alpha) >> 15;
                l = s->filter_l; r = s->filter_r;
            } else {
                s->phase_a += s->inc_a;
                s->phase_b += s->inc_b;
                a = tk_sine(s->phase_a);
                b = tk_sine(s->phase_b);
                l = tk_fold_color((3 * a + b) >> 2, s->performance_drive, s->performance_fracture);
                r = tk_fold_color((a + 3 * b) >> 2, s->performance_drive, s->performance_fracture);
                s->filter_l += ((l - s->filter_l) * s->performance_alpha) >> 15;
                s->filter_r += ((r - s->filter_r) * s->performance_alpha) >> 15;
                l = s->filter_l; r = s->filter_r;
            }
            env = s->env >> 8;
            l = (l * env) >> 15;
            r = (r * env) >> 15;
            if (s->source == TK_SOURCE_ATMOS && s->ghost_gain_q15 < 32768) {
                l = (l * s->ghost_gain_q15) >> 15;
                r = (r * s->ghost_gain_q15) >> 15;
            }
            if (s->performance_gain_q15 != 32768) {
                l = (l * s->performance_gain_q15) >> 15;
                r = (r * s->performance_gain_q15) >> 15;
            }
            out[2u * i] += (l * (int32_t)s->level) >> 15;
            out[2u * i + 1u] += (r * (int32_t)s->level) >> 15;
        }
    }
    for (v = 0; v < TK_VOICES; ++v) {
        tk_voice[v].tail = tk_voice[v].env || tk_voice[v].gate || tk_voice[v].pending;
        tk_voice[v].fm_tail=tk_voice[v].instrument==TK_INSTRUMENT_FM && tk_voice[v].tail;
    }
}

#endif
