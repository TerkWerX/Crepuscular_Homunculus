/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TERKINSTEIN_ARRANGEMENT_DATA_H
#define TERKINSTEIN_ARRANGEMENT_DATA_H
#define TK_SCENES 8u
#define TK_SCENE_NONE TK_SCENES
enum { TK_COND_ALWAYS, TK_COND_FIRST, TK_COND_AFTER_FIRST, TK_COND_EVERY_2,
       TK_COND_EVERY_4, TK_COND_FILL, TK_COND_NO_FILL, TK_COND_LAST_4 };
typedef struct {
    uint8_t value[4], mask_condition, probability;
} tk_arr_step_t; /* low four bits: macro locks; bits 4..6: note condition */
typedef struct {
    uint8_t bank[TK_TRACKS], mute, tempo;
    uint16_t section_mutes;
    uint8_t enabled, reserved;
} tk_scene_t;
typedef struct {
    tk_arr_step_t step[TK_TRACKS][TK_BANK_COUNT][TK_PATTERN_STEPS];
    tk_scene_t scene[TK_SCENES];
} tk_arr_song_t;
#ifdef TERKINSTEIN_HOST
static tk_arr_song_t tk_arr_song;
#else
static tk_arr_song_t tk_arr_song __attribute__((section(".pool")));
#endif
static struct {
    uint32_t cycle[TK_TRACKS], rng;
    uint8_t lock_mask[TK_TRACKS], value[TK_TRACKS][4], seen[TK_TRACKS];
    uint8_t auto_armed, auto_touched[TK_TRACKS], auto_value[TK_TRACKS][4];
    uint8_t fill, scene, pending_scene, dirty;
    uint16_t revision;
} tk_arr;
static void tk_arr_defaults(void)
{
    uint32_t track, bank, step, scene;
    memset(&tk_arr_song, 0, sizeof tk_arr_song); memset(&tk_arr, 0, sizeof tk_arr);
    tk_arr.scene = tk_arr.pending_scene = TK_SCENE_NONE; tk_arr.rng = 0x13DFA965u;
    for (track = 0; track < TK_TRACKS; track++)
        for (bank = 0; bank < TK_BANK_COUNT; bank++)
            for (step = 0; step < TK_PATTERN_STEPS; step++) tk_arr_song.step[track][bank][step].probability = 100u;
    for (scene = 0; scene < TK_SCENES; scene++)
        for (track = 0; track < TK_TRACKS; track++) tk_arr_song.scene[scene].bank[track] = scene % TK_BANK_COUNT;
}
static int tk_arr_step_valid(const tk_arr_step_t *step)
{
    return !(step->mask_condition & 128u) && step->probability <= 100u &&
        step->value[0] <= 127u && step->value[1] <= 127u && step->value[2] <= 127u && step->value[3] <= 127u;
}
static int tk_scene_valid(const tk_scene_t *scene)
{
    uint32_t track;
    if (scene->mute >= 16u || (scene->tempo && (scene->tempo < 40u || scene->tempo > 180u))
        || scene->enabled > 1u || scene->reserved) return 0;
    for (track = 0; track < TK_TRACKS; track++) if (scene->bank[track] >= TK_BANK_COUNT) return 0;
    return 1;
}
static int tk_arr_song_valid(const tk_arr_song_t *song)
{
    uint32_t track, bank, step, scene;
    for (track = 0; track < TK_TRACKS; track++)
        for (bank = 0; bank < TK_BANK_COUNT; bank++)
            for (step = 0; step < TK_PATTERN_STEPS; step++) if (!tk_arr_step_valid(&song->step[track][bank][step])) return 0;
    for (scene = 0; scene < TK_SCENES; scene++) if (!tk_scene_valid(&song->scene[scene])) return 0;
    return 1;
}
static void tk_arr_apply_params(uint32_t track, tk_params_t *p)
{
    uint32_t knob;
    if (track >= TK_TRACKS) return;
    for (knob = 0; knob < 4u; knob++) if (tk_arr.lock_mask[track] & (1u << knob)) ((uint8_t *)p)[knob] = tk_arr.value[track][knob];
}
static int tk_arr_condition(uint32_t condition, uint32_t cycle, uint32_t fill)
{
    switch (condition) {
    case TK_COND_FIRST: return !cycle;
    case TK_COND_AFTER_FIRST: return !!cycle;
    case TK_COND_EVERY_2: return !(cycle & 1u);
    case TK_COND_EVERY_4: return !(cycle & 3u);
    case TK_COND_FILL: return !!fill;
    case TK_COND_NO_FILL: return !fill;
    case TK_COND_LAST_4: return (cycle & 3u) == 3u;
    default: return 1;
    }
}
#endif
