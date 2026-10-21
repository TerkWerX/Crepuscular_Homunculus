/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual application settings, MIDI commands, transport and pixel parity.
 * Desktop checks do not qualify physical listening or target CPU deadlines. */
#define TERKINSTEIN_HOST 1
#define main tk_metronome_host_main
#include "../firmware/src/terkinstein.c"
#undef main
#include <stddef.h>

static uint32_t checks, reply_length, panel_hold;
static uint8_t reply[TK_REMOTE_PAYLOAD_MAX], live_shade, track_shade[TK_TRACKS];
static tk_settings_t fixture, before, migrated;
static tk_settings_v10_t legacy, corrupt;
static tk_transport_t transport, transport_before;
static tk_lfo_t live_lfo, track_lfo[TK_TRACKS];
static tk_song_stage_t expected_song, staged_before;
static uint16_t painted[240u * 240u];
static int32_t audio[512];

static void check(int condition, const char *why)
{
    checks++;
    if (!condition) { fprintf(stderr, "FAIL metronome app %u: %s\n", checks, why); exit(1); }
}

static void reset_fixture(void)
{
    uint32_t track;
    settings_defaults(&fixture);
    memset(&transport, 0, sizeof transport);
    tk_init(); tk_visual_init(); tk_metronome_init(); tk_metronome_clock_init(&metro_clock);
    tk_banks_init(&fixture, &transport);
    remote_observed_valid = 0; remote_revision = remote_pattern_revision = 0;
    tk_ready = 1; fm1_ms = 1000u; panel_hold = 0;
    memset(panel_refs, 0, sizeof panel_refs); memset(panel_down, 0, sizeof panel_down);
    panel_arpeggiated = 0;
    live_shade = fixture.p.shade; tk_lfo_init(&live_lfo, fm1_ms);
    for (track = 0; track < TK_TRACKS; track++) {
        tk_pattern_clear(&fixture.pattern[track]);
        track_shade[track] = fixture.track[track].p.shade;
        tk_lfo_init(&track_lfo[track], fm1_ms);
        publish_track(&fixture, track);
    }
    publish_params(&fixture, live_shade);
}

static uint32_t command(uint32_t which, const uint8_t *payload, uint32_t count)
{
    tk_remote_message_t request;
    request.direction = TK_REMOTE_REQUEST; request.command = (uint8_t)which;
    request.id = 1u; request.length = (uint8_t)count; request.payload = payload;
    memset(reply, 0xA5, sizeof reply);
    return tk_remote_command(&request, &fixture, &transport, &live_shade,
        &live_lfo, track_lfo, track_shade, TK_PAGE_METRONOME, 0u, reply, &reply_length);
}

static void expect_metro(uint32_t enabled, uint32_t level, uint32_t beats, uint32_t unit)
{
    check(!command(TK_REMOTE_METRONOME, 0, 0), "metronome query reports no dirty/change flags");
    check(reply_length == 6u && reply[0] == TK_REMOTE_OK && reply[1] == 1u &&
        reply[2] == enabled && reply[3] == level && reply[4] == beats && reply[5] == unit,
        "GET 0D has the exact status plus five-byte versioned metronome body");
}

static void migration_checks(void)
{
    uint32_t track, step, byte;
    reset_fixture(); preset_apply(&fixture, 32u);
    fixture.graft_target = 35u; fixture.graft_amount = 77u; graft_apply(&fixture);
    fixture.octave = 2; fixture.tempo = 173u; fixture.track_mute = 10u;
    fixture.ghost_mask = 107u; fixture.mono = 1u; fixture.panel_response = TK_ENCODER_ADAPTIVE;
    fixture.midi_bend_range = 19u; fixture.midi_bind[6].cc = 42u;
    fixture.midi_bind[6].port = 1u; fixture.midi_bind[6].channel = 9u;
    for (track = 0; track < TK_TRACKS; track++) {
        track_factory_apply(&fixture, track, 32u + track);
        fixture.length[track] = track == 0u ? 16u : track == 1u ? 32u : 64u;
        for (step = 0; step < TK_PATTERN_STEPS; step++) {
            tk_pattern_record(&fixture.pattern[track], step, 24u + (step + track) % 90u,
                              1u + (step * 7u + track) % 127u);
            fixture.pattern[track].step[step].note[0].duration_q4 = (uint8_t)(step * 4u + track);
        }
        fixture.fm[track + 1u].feedback = (uint8_t)(71u + track);
    }
    check(settings_valid(&fixture), "dense migration fixture has valid FM, GRAFT and all four patterns");
    check(sizeof fixture == 3700u && sizeof legacy == 3696u && TK_SCHEMA == 12u,
          "schema 12 uses exactly 3700 bytes and retains schema-10's 3696-byte type");
    check(offsetof(tk_settings_t, metro_enabled) == offsetof(tk_settings_v10_t, mono) + 1u,
          "metronome follows the last real schema-10 field without moving any prior field");
    memcpy(&legacy, &fixture, sizeof legacy); legacy.schema = 10u;
    legacy.midi_pad = 0xA7u; /* Old reserved data cannot become a new preference. */
    for (byte = (uint32_t)offsetof(tk_settings_v10_t, mono) + 1u; byte < sizeof legacy; byte++)
        ((uint8_t *)&legacy)[byte] = 0xDAu;
    check(settings_upgrade_v10(&migrated, &legacy) && settings_valid(&migrated),
          "valid schema-10 FM save migrates despite nonzero tail padding");
    check(migrated.schema == 12u && !migrated.metro_enabled && migrated.metro_level == 32u &&
          migrated.metro_beats == 4u && migrated.metro_unit == 4u,
          "migration starts with silent metronome defaults rather than interpreting old padding");
    check(migrated.panel_response == TK_ENCODER_LINEAR,
          "old reserved MIDI byte migrates to LINEAR rather than an invented acceleration setting");
    check(!memcmp((uint8_t *)&migrated + 4u, (uint8_t *)&legacy + 4u,
          offsetof(tk_settings_t, panel_response) - 4u) &&
          !memcmp((uint8_t *)&migrated + offsetof(tk_settings_t, panel_response) + 1u,
          (uint8_t *)&legacy + offsetof(tk_settings_t, panel_response) + 1u,
          offsetof(tk_settings_t, metro_enabled) - offsetof(tk_settings_t, panel_response) - 1u),
          "every real preexisting field is retained, including all notes, captured FM and GRAFT");
    corrupt = legacy; corrupt.schema = 11u;
    check(!settings_upgrade_v10(&migrated, &corrupt), "wrong old schema is rejected");
    corrupt = legacy; corrupt.fm[3].op[2].ratio = 32u;
    check(!settings_upgrade_v10(&migrated, &corrupt), "invalid captured FM ratio is rejected during migration");
    corrupt = legacy; corrupt.pattern[3].step[63].note[0].velocity = 0u;
    check(!settings_upgrade_v10(&migrated, &corrupt), "malformed old hidden note is rejected");
    corrupt = legacy; corrupt.mono = 2u;
    check(!settings_upgrade_v10(&migrated, &corrupt), "invalid old live voice mode is rejected");
    corrupt = legacy; corrupt.graft_target = TK_KIT_PRESET_INDEX;
    check(!settings_upgrade_v10(&migrated, &corrupt), "incompatible old FM GRAFT target is rejected");
    corrupt = legacy; corrupt.midi_bind[6].cc = 64u;
    check(!settings_upgrade_v10(&migrated, &corrupt), "reserved old learned controller binding is rejected");
    check(settings_upgrade_v10(&migrated, &legacy), "valid old save still migrates after rejection checks");
    before = migrated; migrated.metro_enabled = 2u; check(!settings_valid(&migrated), "schema-12 enable flag is bounded");
    migrated = before; migrated.metro_level = 128u; check(!settings_valid(&migrated), "schema-12 click level is bounded");
    migrated = before; migrated.metro_beats = 0u; check(!settings_valid(&migrated), "schema-12 meter needs at least one beat");
    migrated = before; migrated.metro_beats = 13u; check(!settings_valid(&migrated), "schema-12 meter allows at most twelve beats");
    migrated = before; migrated.metro_unit = 16u; check(!settings_valid(&migrated), "schema-12 unit accepts only quarter/eighth");
    migrated = before; migrated.panel_response = 2u; check(!settings_valid(&migrated), "schema-12 panel response accepts only LINEAR/ACCEL");
}

static void remote_checks(void)
{
    static const uint8_t valid[] = {1u, 81u, 7u, 8u};
    static const uint8_t invalid[][4] = {
        {2u,32u,4u,4u}, {1u,128u,4u,4u}, {1u,32u,0u,4u},
        {1u,32u,13u,4u}, {1u,32u,4u,0u}, {1u,32u,4u,16u}
    };
    uint32_t i, word;
    tk_metronome_clock_t before_clock;
    reset_fixture(); before = fixture; transport_before = transport;
    before_clock = metro_clock; word = tk_metronome_command;
    expect_metro(0u, 32u, 4u, 4u);
    check(!memcmp(&fixture, &before, sizeof fixture) && !memcmp(&transport, &transport_before, sizeof transport)
        && !memcmp(&metro_clock, &before_clock, sizeof metro_clock) && tk_metronome_command == word,
        "query cannot change settings, transport, clock or audio publication");
    command(TK_REMOTE_HELLO, 0, 0);
    check(reply[0] == TK_REMOTE_OK && (reply[3] & 32u), "HELLO advertises the metronome capability");
    check(!command(TK_REMOTE_METRONOME, valid, 1u) && reply[0] == TK_REMOTE_BAD_ARGUMENT && reply_length == 1u,
        "GET 0D rejects any request body");
    for (i = 0; i < sizeof invalid / sizeof invalid[0]; i++) {
        check(!command(TK_REMOTE_SET_METRONOME, invalid[i], 4u) && reply[0] == TK_REMOTE_BAD_ARGUMENT,
            "SET 2B rejects every invalid enable/level/meter/unit bound");
        check(!memcmp(&fixture, &before, sizeof fixture) && tk_metronome_command == word,
            "invalid write has no partial settings or command effect");
    }
    check(!command(TK_REMOTE_SET_METRONOME, valid, 3u) && reply[0] == TK_REMOTE_BAD_ARGUMENT,
        "SET 2B requires all four fields");
    check(!command(TK_REMOTE_SET_METRONOME, valid, 5u) && reply[0] == TK_REMOTE_BAD_ARGUMENT,
        "SET 2B rejects an extra byte before reading payload");
    check(command(TK_REMOTE_SET_METRONOME, valid, 4u) == (TK_REMOTE_CHANGED | TK_REMOTE_DIRTY)
        && reply[0] == TK_REMOTE_OK && reply_length == 1u, "changed SET 2B marks settings dirty exactly once");
    before.metro_enabled = 1u; before.metro_level = 81u; before.metro_beats = 7u; before.metro_unit = 8u;
    check(!memcmp(&fixture, &before, sizeof fixture), "SET 2B changes only its four persistent fields");
    expect_metro(1u,81u,7u,8u); word = tk_metronome_command; before_clock = metro_clock;
    check(!command(TK_REMOTE_SET_METRONOME, valid, 4u) && reply[0] == TK_REMOTE_OK
        && tk_metronome_command == word && !memcmp(&metro_clock,&before_clock,sizeof metro_clock),
        "identical metronome set is a complete no-op without dirtying or restarting phase");
    check(command(TK_REMOTE_SONG_BEGIN, 0, 0) == TK_REMOTE_CHANGED && reply[0] == TK_REMOTE_OK,
        "song begin creates a transaction");
    before = fixture; before_clock = metro_clock; word = tk_metronome_command;
    check(!command(TK_REMOTE_SET_METRONOME, valid, 4u) && reply[0] == TK_REMOTE_BUSY
        && !memcmp(&fixture,&before,sizeof fixture) && tk_metronome_command == word
        && !memcmp(&metro_clock,&before_clock,sizeof metro_clock), "SET 2B is blocked during song import even for no-op writes");
    expect_metro(1u,81u,7u,8u);
    command(TK_REMOTE_SONG_ABORT, 0, 0);
}

static void fill_song(void)
{
    uint32_t track, bank;
    fixture.metro_enabled = 1u; fixture.metro_level = 93u; fixture.metro_beats = 9u; fixture.metro_unit = 8u;
    for (track = 0; track < TK_TRACKS; track++) for (bank = 0; bank < TK_BANK_COUNT; bank++) {
        tk_pattern_t *part = tk_bank_pattern(&fixture, track, bank);
        tk_pattern_clear(part);
        tk_pattern_record(part, 63u, 40u + track * 4u + bank, 85u + track + bank);
        part->step[63].note[0].duration_q4 = 255u;
    }
    tk_song_clone(&fixture, &transport);
}

static void song_checks(void)
{
    static const uint8_t globals[14] = {21u,1u,5u,2u,1u,2u,3u,0u,105u,0u,1u,72u,3u,4u};
    static const uint8_t note[] = {1u,2u,63u,1u,67u,123u,127u,1u};
    uint8_t malformed[14];
    uint32_t extended, i;
    for (extended = 0; extended < 2u; extended++) {
        reset_fixture(); fill_song(); expected_song = tk_bank_pool.stage;
        command(TK_REMOTE_SONG_BEGIN, 0, 0); before = fixture;
        check(!command(TK_REMOTE_SONG_GLOBAL, globals, extended ? 14u : 10u) && reply[0] == TK_REMOTE_OK,
            "SONG_GLOBAL accepts both legacy ten-byte and extended fourteen-byte forms");
        check(!memcmp(&fixture,&before,sizeof fixture), "staging metronome/global data cannot mutate live song settings");
        command(TK_REMOTE_SONG_STEP, note, sizeof note);
        check(reply[0] == TK_REMOTE_OK, "transaction can also stage a held note in an alternate bank");
        expected_song.tempo = 149u; expected_song.mute = 5u; expected_song.selected = 2u;
        for (i = 0; i < TK_TRACKS; i++) expected_song.active[i] = globals[4u+i];
        expected_song.ghost_mask = 105u; expected_song.fx = 0u;
        expected_song.metro_enabled = (uint8_t)extended; expected_song.metro_level = extended ? 72u : 32u;
        expected_song.metro_beats = extended ? 3u : 4u; expected_song.metro_unit = 4u;
        tk_pattern_clear_step(&expected_song.pattern[1][2],63u);
        tk_pattern_record(&expected_song.pattern[1][2],63u,67u,123u);
        expected_song.pattern[1][2].step[63].note[0].duration_q4 = 255u;
        check(command(TK_REMOTE_SONG_COMMIT,0,0) == (TK_REMOTE_CHANGED|TK_REMOTE_DIRTY) && reply[0] == TK_REMOTE_OK,
            "valid transaction atomically commits metronome plus all song content");
        check(!tk_banks.importing && !transport.running && !transport.recording && !metro_clock.step_in_bar
            && !(tk_metronome_command&127u), "song commit remains stopped, resets meter and cancels active click");
        tk_song_clone(&fixture,&transport);
        check(!memcmp(&tk_bank_pool.stage,&expected_song,sizeof expected_song),
            "complete readback matches all sixteen patterns, sounds, lengths and extended globals");
        expect_metro(extended,extended?72u:32u,extended?3u:4u,4u);
    }
    for (i = 0; i < 8u; i++) {
        reset_fixture(); fill_song(); command(TK_REMOTE_SONG_BEGIN,0,0);
        before = fixture; staged_before = tk_bank_pool.stage; memcpy(malformed,globals,sizeof malformed);
        switch(i) {
        case 0: malformed[10]=2u; break; case 1: malformed[11]=128u; break;
        case 2: malformed[12]=0u; break; case 3: malformed[12]=13u; break;
        case 4: malformed[13]=16u; break; case 5: malformed[6]=4u; break;
        case 6: malformed[0]=39u; malformed[1]=0u; break; default: break;
        }
        check(!command(TK_REMOTE_SONG_GLOBAL,malformed,i==7u?13u:14u) && reply[0]==TK_REMOTE_BAD_ARGUMENT,
            "invalid extended song global is rejected");
        check(tk_banks.import_failed && !memcmp(&tk_bank_pool.stage,&staged_before,sizeof staged_before)
            && !memcmp(&fixture,&before,sizeof fixture), "invalid staging poisons the transaction with no partial stage/live edit");
        check(!command(TK_REMOTE_SONG_GLOBAL,globals,14u) && reply[0]==TK_REMOTE_OK,
            "a later valid stage can be acknowledged");
        check(!command(TK_REMOTE_SONG_COMMIT,0,0) && reply[0]==TK_REMOTE_BAD_ARGUMENT
            && !memcmp(&fixture,&before,sizeof fixture) && tk_banks.importing,
            "later valid data cannot rescue a poisoned transaction or alter the live song");
        command(TK_REMOTE_SONG_ABORT,0,0);
        check(!tk_banks.importing, "abort releases a poisoned transaction");
    }
}

static void transport_checks(void)
{
    uint8_t op;
    uint32_t i, word, old_phase, old_next;
    reset_fixture(); fixture.metro_enabled=1u; fixture.metro_level=64u; fixture.metro_beats=3u;
    op=1u; command(TK_REMOTE_TRANSPORT,&op,1u);
    check(transport.running && transport.mode==TK_MODE_SEQ && !transport.next_step && !metro_clock.step_in_bar,
        "fresh MIDI START anchors sequencer and meter to step one");
    for(i=0;i<131u;i++) {
        word=tk_metronome_command; fm1_ms=transport.next_ms;
        check(transport_tick(&transport,&fixture,fm1_ms), "due sequencer step advances");
        check(transport.shown_step==i%64u && transport.next_step==(i+1u)%64u
            && metro_clock.step_in_bar==(i+1u)%12u, "three-four meter continues across independent sixty-four-step transport wrap");
        check((tk_metronome_command!=word)==(!(i%4u)), "sequencer ticks publish metronome only on quarter-note beats");
        if(!(i%4u)) check(!!(tk_metronome_command&128u)==(!(i%12u)),
            "metronome accents every three beats without false sixty-four-step downbeat");
    }
    old_phase=metro_clock.step_in_bar; old_next=transport.next_step;
    word=tk_metronome_command;
    check(!transport_tick(&transport,&fixture,fm1_ms+1u) && metro_clock.step_in_bar==old_phase
        && tk_metronome_command==word, "early main-loop polling cannot advance meter or duplicate click");
    op=0u; command(TK_REMOTE_TRANSPORT,&op,1u);
    check(!transport.running && !(tk_metronome_command&127u) && metro_clock.step_in_bar==old_phase,
        "STOP cancels click while preserving meter phase for Continue");
    memset(audio,0,sizeof audio); app_audio(audio,256u);
    for(i=0;i<512u;i++) check(!audio[i], "stopped empty song has no metronome sound");
    op=2u; fm1_ms+=500u; command(TK_REMOTE_TRANSPORT,&op,1u);
    check(transport.running && transport.next_step==old_next && metro_clock.step_in_bar==old_phase,
        "Continue resumes next sequencer step and meter phase without resetting");
    {
        const uint8_t bpm[]={52u,1u};
        uint32_t phase=metro_clock.step_in_bar;
        check(command(TK_REMOTE_TEMPO,bpm,2u)==(TK_REMOTE_CHANGED|TK_REMOTE_DIRTY)
            && fixture.tempo==180u && metro_clock.step_in_bar==phase, "BPM retiming leaves the musical accent position unchanged");
    }
    op=1u; command(TK_REMOTE_TRANSPORT,&op,1u); fm1_ms=transport.next_ms;
    transport_tick(&transport,&fixture,fm1_ms);
    check(metro_clock.step_in_bar==1u && (tk_metronome_command&128u), "fresh restart returns to accented first beat");
    panel_panic(); transport_mode(&transport,TK_MODE_OFF,fm1_ms);
    check(!transport.running && transport.mode==TK_MODE_OFF && !(tk_metronome_command&127u),
        "physical panic sequence cancels metronome together with all musical transport");
    {
        const uint8_t rec[]={TK_TRACK_MELODY,1u,1u};
        metro_clock.step_in_bar=7u; command(TK_REMOTE_RECORD,rec,sizeof rec);
        check(transport.recording && transport.running && !metro_clock.step_in_bar,
            "REC from stopped transport starts a fresh meter");
        fm1_ms=transport.next_ms; transport_tick(&transport,&fixture,fm1_ms);
        check(metro_clock.step_in_bar==1u && (tk_metronome_command&128u), "REC's first sixteenth is an accented metronome downbeat");
    }
}

static uint32_t paint_parity(void)
{
    uint32_t pixels;
    host_lcd_pixels=0; draw_metronome(&fixture); pixels=host_lcd_pixels;
    memcpy(painted,fb,sizeof fb);
    lcd_fill(0,0,240,240,C_BLACK); draw_header(TK_PAGE_METRONOME); draw_metronome(&fixture);
    check(!memcmp(painted,fb,sizeof fb), "incremental metronome pixels exactly match a clean complete page");
    return pixels;
}

static void screen_and_knob_checks(void)
{
    uint32_t complete, pixels, knob, phase;
    reset_fixture(); lcd_fill(0,0,240,240,C_BLACK); draw_header(TK_PAGE_METRONOME);
    check(!metro_drawn_valid, "metronome page entry invalidates its paint cache");
    complete=paint_parity(); check(complete>20000u, "first metronome visit paints all labels, values and help");
    check(!paint_parity(), "unchanged metronome screen sends no LCD pixels");
    for(knob=0;knob<4u;knob++) {
        before=fixture;
        check(tk_page_knob(&fixture,&transport,TK_PAGE_METRONOME,knob,1,&panel_hold,&live_shade,&live_lfo)==7,
            "each metronome knob consumes and dirties an actual visible parameter");
        pixels=paint_parity(); check(pixels && pixels<complete/2u, "changed metronome knob repaints only a bounded portion");
        check(!paint_parity(), "new metronome value is cached without repeated repaint");
        check(!memcmp(&before.graft_base,&fixture.graft_base,sizeof fixture.graft_base)
            && !memcmp(before.fm,fixture.fm,sizeof fixture.fm) && !memcmp(before.pattern,fixture.pattern,sizeof fixture.pattern),
            "metronome knob cannot bake GRAFT, edit operators or replace notes");
    }
    fixture.metro_enabled=1u; check(paint_parity()<complete, "enabled click repaints status without redrawing page");
    fixture.metro_beats=12u; paint_parity(); fixture.metro_beats=1u; paint_parity();
    before=fixture; fixture.p.shade++;
    check(!paint_parity(), "unshown sound change does not repaint metronome controls");
    fixture=before;
    tk_banks.importing=1u; before=fixture; phase=metro_clock.step_in_bar;
    for(knob=0;knob<4u;knob++) check(tk_page_knob(&fixture,&transport,TK_PAGE_METRONOME,knob,1,
        &panel_hold,&live_shade,&live_lfo)==1 && !memcmp(&fixture,&before,sizeof fixture)
        && metro_clock.step_in_bar==phase, "metronome panel edits are blocked during staged import");
    tk_banks.importing=0u;
    check(tk_encoder_alias_inactive(TK_PAGE_METRONOME), "metronome SELECT/ALGORITHM aliases remain inactive");
    draw_header(TK_PAGE_ENV); draw_header(TK_PAGE_METRONOME);
    check(!metro_drawn_valid, "leaving and revisiting metronome invalidates stale pixels");
    check(paint_parity()>20000u, "metronome revisit restores all page contents");
}

static void save_silence_checks(void)
{
    uint32_t i;
    uint8_t op=1u;
    reset_fixture(); fixture.metro_enabled=1u; fixture.metro_level=64u;
    command(TK_REMOTE_TRANSPORT,&op,1u); fm1_ms=transport.next_ms;
    transport_tick(&transport,&fixture,fm1_ms);
    check(tk_quiet() && !tk_metronome_quiet(),
          "empty sequencer has no engine voices but pending metronome still blocks SAVE");
    memset(audio,0,sizeof audio); app_audio(audio,256u);
    check(tk_quiet() && !tk_metronome_quiet(),
          "click is outside engine voices and remains visible to its separate flash guard");
    /* Same stop path used by the physical SAVE button. */
    transport_mode(&transport,TK_MODE_OFF,fm1_ms);
    check(tk_quiet() && !tk_metronome_quiet(),
          "SAVE stop publication alone cannot establish click silence before audio consumes cancel");
    app_audio(audio,0u);
    check(!tk_metronome_quiet(), "zero-frame app callback cannot complete the pending click cancellation");
    memset(audio,0,sizeof audio); app_audio(audio,256u);
    check(tk_quiet() && tk_metronome_quiet(),
          "engine and click silence guards both become ready after a nonzero cancelled audio block");
    for(i=0;i<512u;i++) check(!audio[i], "completed SAVE cancellation leaves no click in the audio output");
}

int main(void)
{
    migration_checks(); remote_checks(); song_checks(); transport_checks(); screen_and_knob_checks(); save_silence_checks();
    printf("PASS metronome app: %u settings, MIDI, transport and pixel checks; host only\n",checks);
    return 0;
}
