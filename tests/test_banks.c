/* SPDX-License-Identifier: GPL-3.0-only */
/* Execute the actual application, fixed-point engine and main-owned transport.
 * Offline checks do not qualify device timing, USB hardware or controller LEDs. */
#define TERKINSTEIN_HOST 1
#define main tk_banks_host_main
#include "../firmware/src/terkinstein.c"
#undef main

static uint32_t bank_checks;
static tk_settings_t fixture;
static tk_transport_t clock_state;
static tk_lfo_t fixture_lfo, fixture_track_lfo[TK_TRACKS];
static uint8_t fixture_shade, fixture_track_shade[TK_TRACKS], reply[80];
static uint32_t reply_length;
static void check(int condition, const char *why)
{
    bank_checks++;
    if (!condition) { fprintf(stderr, "FAIL bank: %s\n", why); exit(1); }
}
static uint32_t invoke(uint8_t command, const uint8_t *p, uint32_t n)
{
    tk_remote_message_t request;
    request.command = command; request.direction = 0; request.id = 0;
    request.payload = p; request.length = (uint8_t)n;
    return tk_remote_command(&request, &fixture, &clock_state, &fixture_shade,
        &fixture_lfo, fixture_track_lfo, fixture_track_shade, TK_PAGE_SEQ, 1,
        reply, &reply_length);
}
static void reset_fixture(void)
{
    uint32_t track;
    memset(&clock_state, 0, sizeof clock_state); settings_defaults(&fixture);
    for (track = 0; track < TK_TRACKS; track++) tk_pattern_clear(&fixture.pattern[track]);
    tk_init(); tk_banks_init(&fixture, &clock_state); tk_ready = 1; fm1_ms = 0;
    tk_lfo_init(&fixture_lfo, 0); fixture_shade = fixture.p.shade;
    for (track = 0; track < TK_TRACKS; track++) {
        tk_lfo_init(&fixture_track_lfo[track], 0);
        fixture_track_shade[track] = fixture.track[track].p.shade; publish_track(&fixture, track);
    }
    remote_observed_valid = 0;
}
static void render(void)
{
    int32_t pcm[512] = {0};
    tk_render(pcm, 256, &fixture.p);
}
static void set_note(uint32_t track, uint32_t bank, uint32_t step, uint32_t pitch, uint32_t gate)
{
    uint8_t p[8] = {(uint8_t)track,(uint8_t)bank,(uint8_t)step,1,
        (uint8_t)pitch,96,(uint8_t)(gate & 127u),(uint8_t)(gate >> 7)};
    invoke(TK_REMOTE_SET_BANK_STEP,p,sizeof p); check(reply[0]==0,"accept playable bank note");
}
static void content_and_lengths(void)
{
    uint32_t track, bank, pass;
    uint8_t p[3];
    reset_fixture();
    check(sizeof(tk_pattern_t)==832&&sizeof(tk_pattern_step_t)==13,"dense pattern wire-independent storage remains832bytes");
    check(sizeof(tk_settings_t)==3700 && TK_SCHEMA==12,"bank feature preserves the saved 3700-byte layout in schema12");
    for (track=0;track<4;track++) for(bank=0;bank<4;bank++) {
        set_note(track,bank,63,48+track*4+bank,255);
        p[0]=(uint8_t)track;p[1]=(uint8_t)bank;p[2]=(uint8_t)((track+bank)%3);
        invoke(TK_REMOTE_SET_BANK_LENGTH,p,3);check(reply[0]==0,"each bank owns its independent length");
    }
    for(pass=0;pass<7;pass++) for(track=0;track<4;track++) for(bank=0;bank<4;bank++) {
        uint32_t chosen=(bank+pass)%4;
        p[0]=(uint8_t)track;p[1]=(uint8_t)chosen;invoke(TK_REMOTE_SELECT_BANK,p,2);
        check(reply[0]==0&&!clock_state.running,"idle selection never starts playback");
        check(fixture.pattern[track].step[63].note[0].pitch==48+track*4+chosen,"many bank swaps retain all hidden notes and owners");
        check(fixture.pattern[track].step[63].note[0].duration_q4==255,"full64step gate survives bank swaps");
        check(fixture.length[track]==(16u<<((track+chosen)%3)),"bank swaps retain length with note data");
    }
    p[0]=1;p[1]=0;p[2]=3;invoke(TK_REMOTE_COPY_BANK,p,3);
    p[1]=3;invoke(TK_REMOTE_SELECT_BANK,p,2);
    check(fixture.pattern[1].step[63].note[0].pitch==52,"copy includes hidden steps beyond source length");
    pass=fixture.length[1];invoke(TK_REMOTE_CLEAR_BANK,p,2);
    check(!fixture.pattern[1].step[63].count&&fixture.length[1]==pass,"clear preserves chosen bank length");
}
static void boundary_and_held_owners(void)
{
    uint8_t p[3]={0,1,2}; uint32_t step,period;
    reset_fixture();
    set_note(0,0,0,36,0);set_note(0,1,0,38,0);set_note(0,1,16,41,0);
    set_note(2,0,0,48,255);
    fixture.length[2]=64;tk_banks_observe(&fixture);
    invoke(TK_REMOTE_SET_BANK_LENGTH,p,3);
    check(tk_note_on(60,90,1),"independentUSB live note starts");
    render();clock_state.mode=TK_MODE_SEQ;transport_start(&clock_state,0);
    check(transport_tick(&clock_state,&fixture,0),"first sequencer step sounds"); render();
    check(tk_note_is_held(48,TK_SOURCE_ATMOS),"long atmosphere gate is active before drum switch");
    invoke(TK_REMOTE_SELECT_BANK,p,2);
    check(tk_banks.pending[0]==1&&tk_banks.active[0]==0,"only desired drum bank is queued");
    period=15000u/fixture.tempo;
    for(step=1;step<=16;step++) {
        fm1_ms=step*period;transport_tick(&clock_state,&fixture,fm1_ms);render();
        if(step<16) check(tk_banks.active[0]==0,"loop finishes before replacement");
    }
    check(tk_banks.active[0]==1&&clock_state.phase_origin[0]==16,"switch records a track-local phase origin");
    check(tk_transport_local_step(&clock_state,0,clock_state.shown_step,fixture.length[0])==0,"longerbank starts its own stepone");
    check(tk_note_is_held(38,TK_SOURCE_DRUM)&&!tk_note_is_held(41,TK_SOURCE_DRUM),"actualstepone snare plays rather than newbank stepseventeen");
    check(tk_note_is_held(48,TK_SOURCE_ATMOS)&&tk_note_is_held(60,1),"switch cannot release unrelated sustained atmosphere or liveUSB key");
    invoke(TK_REMOTE_BANK_STATE,NULL,0);
    check(reply_length==73&&reply[13]==0&&reply[15]==16,"confirmed four local playheads distinguish different track phase");
    transport_stop(&clock_state);
    check(tk_note_is_held(60,1)&&!tk_note_is_held(48,TK_SOURCE_ATMOS),"STOP releases generated gates while keeping independent live key");
    transport_start(&clock_state, fm1_ms);
    for(step=0;step<4;step++) check(clock_state.phase_origin[step]==0,"explicitSTART resets every local track phase");
    clock_state.phase_origin[2]=16;clock_state.shown_step=19;clock_state.selected=2;
    clock_state.recording=1;clock_state.record_length=64;clock_state.record_pattern=&fixture.pattern[2];
    tk_pattern_clear(&fixture.pattern[2]);
    check(transport_capture_start(&clock_state,29,65,88,1000,period),"held input capture starts after phasechange");
    transport_capture_end(&clock_state,29,65,1000+period*4);
    check(fixture.pattern[2].step[3].count==1&&!fixture.pattern[2].step[19].count,"REC uses local phase for quantized onset");
    check(fixture.pattern[2].step[3].note[0].duration_q4==16,"captured heldnote retains musical duration after bank switch");
}
static void isolated_import(void)
{
    static tk_settings_t original;
    static tk_pattern_t song_before[4][4];
    uint8_t p[55];uint32_t track,bank;
    reset_fixture();
    for(track=0;track<4;track++)for(bank=0;bank<4;bank++)set_note(track,bank,63,48+track*4+bank,255);
    original=fixture;
    for(track=0;track<4;track++)for(bank=0;bank<4;bank++)song_before[track][bank]=*tk_bank_pattern(&fixture,track,bank);
    clock_state.mode=TK_MODE_SEQ;transport_start(&clock_state,0);
    invoke(TK_REMOTE_SONG_BEGIN,NULL,0);check(reply[0]==0&&tk_banks.importing&&!clock_state.running,"BEGIN explicitlystops and stagescompleteSong");
    p[0]=2;p[1]=3;p[2]=5;p[3]=1;p[4]=72;p[5]=81;p[6]=16;p[7]=0;
    invoke(TK_REMOTE_SONG_STEP,p,8);
    check(reply[0]==0&&!memcmp(&fixture,&original,sizeof fixture),"stagednotes leave currentactivepatch untouched");
    for(track=0;track<4;track++)for(bank=0;bank<4;bank++)check(!memcmp(tk_bank_pattern(&fixture,track,bank),&song_before[track][bank],832),"allinactivebanks remain intact during partialload");
    check(tk_note_on(60,90,1),"liveUSBnotes remain usable during songimport");render();
    invoke(TK_REMOTE_SONG_ABORT,NULL,0);
    check(!tk_banks.importing&&!memcmp(&fixture,&original,sizeof fixture)&&tk_note_is_held(60,1),"ABORT preservesallpatterns sharedpatch andliveownership");
    invoke(TK_REMOTE_SONG_BEGIN,NULL,0);
    p[0]=2;memcpy(p+1,&fixture.track[2],14);tk_remote_fm_pack(&fixture.fm[3],p+15);p[15]=8;
    invoke(TK_REMOTE_SONG_PATCH,p,55);check(reply[0]==TK_REMOTE_BAD_ARGUMENT,"fullcapturedFMpatchvalidatesroutingbounds");
    invoke(TK_REMOTE_SONG_COMMIT,NULL,0);
    check(reply[0]==TK_REMOTE_BAD_ARGUMENT&&!memcmp(&fixture,&original,sizeof fixture),"invalidFMstage cannotcommit partialSong");
    invoke(TK_REMOTE_SONG_ABORT,NULL,0);invoke(TK_REMOTE_SONG_BEGIN,NULL,0);
    p[0]=2;p[1]=3;p[2]=5;p[3]=1;p[4]=72;p[5]=81;p[6]=16;p[7]=0;invoke(TK_REMOTE_SONG_STEP,p,8);
    p[0]=2;p[1]=3;p[2]=2;invoke(TK_REMOTE_SONG_LENGTH,p,3);
    p[0]=130 & 127;p[1]=1;p[2]=1;p[3]=2;p[4]=0;p[5]=0;p[6]=3;p[7]=0;p[8]=127;p[9]=0;
    invoke(TK_REMOTE_SONG_GLOBAL,p,10);check(reply[0]==0,"stagedglobals validaterealMASK127range");
    p[0]=2;memcpy(p+1,&fixture.track[2],14);p[1]=99;
    tk_remote_fm_pack(&fixture.fm[3],p+15);invoke(TK_REMOTE_SONG_PATCH,p,55);
    check(reply[0]==0,"completecapturedpatch accepts custommacrovalues");
    invoke(TK_REMOTE_SONG_COMMIT,NULL,0);
    check(reply[0]==0&&!tk_banks.importing&&!clock_state.running,"commit publishescompletevalidatedSong andstaysstopped");
    check(tk_banks.active[2]==3&&fixture.pattern[2].step[5].note[0].pitch==72
        &&fixture.pattern[2].step[63].note[0].pitch==59,"activeincomingbank includeschangedandhiddenoriginalsteps");
    check(fixture.track[2].p.shade==99&&fixture.length[2]==64&&fixture.tempo==130
        &&fixture.ghost_mask==127&&!fixture.fx&&clock_state.selected==2,"completeSongpublishupdates allspecifiedsharedfields");
    check(tk_note_is_held(60,1),"Songcommit never steals existingliveUSBowner");
    check(settings_valid(&fixture),"committedfullSong remainsvalidschema10settings");
}
int main(void)
{
    content_and_lengths();boundary_and_held_owners();isolated_import();
    printf("PASS actual bank/transport/song: %u checks; main pool storage %u bytes\n",
        bank_checks,(unsigned)sizeof(tk_bank_pool));
    return 0;
}
