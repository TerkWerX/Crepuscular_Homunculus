/* SPDX-License-Identifier: GPL-3.0-only */
#define TERKINSTEIN_HOST 1
#define main tk_swing_host_main
#include "../firmware/src/terkinstein.c"
#undef main
#include <assert.h>
static tk_settings_t s,expected;
static tk_transport_t t;
static tk_song_stage_t song;
static uint32_t checks;
static void check(int yes,const char *why){checks++;if(!yes){fprintf(stderr,"FAIL swing: %s\n",why);exit(1);}}
static void setup(uint32_t amount)
{
    settings_defaults(&s);s.swing=amount;memset(&t,0,sizeof t);tk_init();tk_ready=1u;
    tk_metronome_init();tk_metronome_clock_init(&metro_clock);tk_visual_init();tk_clock_disable();
    tk_banks_init(&s,&t);publish_params(&s,s.p.shade);fm1_ms=0u;
}
static void arithmetic(void)
{
    uint32_t tempo,amount,parity,q4,p;
    for(tempo=40;tempo<=180;tempo++)for(amount=0;amount<=100;amount+=25){
        p=15000u/tempo;
        check(tk_swing_interval(p,amount,0)+tk_swing_interval(p,amount,1)==2*p,"pair duration exact");
        for(parity=0;parity<2;parity++)for(q4=1;q4<=256;q4++){
            uint32_t gate=tk_swing_gate(p,amount,parity,q4);
            check(gate>0&&gate<=p*256u/4u+p/2u,"finite positive musical gate");
            if(!amount)check(gate==p*q4/4u,"zero gate rounding exact");
            if(q4%8u==0u)check(gate==p*q4/4u,"whole pairs preserve note values");
        }
    }
}
static void internal(void)
{
    uint32_t amount,n,p,start;
    for(amount=0;amount<=100;amount+=25){
        setup(amount);s.tempo=120;p=125;transport_mode(&t,TK_MODE_SEQ,0u);
        for(n=0;n<64;n++){
            uint32_t want=(n/2u)*p*2u+(n&1u? p+tk_swing_shift(p,amount):0u);
            fm1_ms=t.next_ms;check(fm1_ms==want,"all64 global sixteenths share exact pair grid");
            check(transport_tick(&t,&s,fm1_ms)==1&&t.shown_step==n,"single correctly indexed step");
            check(!transport_tick(&t,&s,fm1_ms),"no duplicate tick or catch-up burst");
        }
        transport_stop(&t);
    }
    setup(100);start=0xFFFFFFA0u;transport_mode(&t,TK_MODE_SEQ,start);
    for(n=0;n<8;n++){fm1_ms=t.next_ms;check(transport_tick(&t,&s,fm1_ms),"wrap-safe due comparison");}
    setup(100);transport_mode(&t,TK_MODE_SEQ,0);transport_tick(&t,&s,0);
    s.swing=0;check(t.next_ms==249u,"current long interval retained until pair boundary");
    transport_tick(&t,&s,t.next_ms);check(t.next_ms==332u,"old short interval completes current pair");
    transport_tick(&t,&s,t.next_ms);check(t.next_ms==498u,"zero applies at next pair boundary");
    setup(100);s.tempo=120;transport_mode(&t,TK_MODE_SEQ,0);transport_tick(&t,&s,0);
    fm1_ms=50;transport_retime_seq(&t,125,100,50);s.tempo=150;
    check(t.next_ms==160u,"BPM change preserves phase of swung deadline");
}
static void external(void)
{
    uint32_t now,n=0,want[8]={0,180,240,420,480,660,720,900};
    setup(100);transport_mode(&t,TK_MODE_SEQ,0);tk_clock_arm(0);tk_clock_event(0xFAu,0);
    for(now=0;now<=930;now++){
        fm1_ms=now;if(now%20u==0u)tk_clock_event(0xF8u,now);
        if(transport_tick(&t,&s,now)){check(n<8&&now==want[n],"24PPQN swing keeps external even grid");n++;}
    }
    check(n==8,"external clock step count exact");
    setup(100);transport_mode(&t,TK_MODE_SEQ,0);tk_clock_arm(0);tk_clock_event(0xFAu,0);
    for(now=0;now<=125;now++){fm1_ms=now;if(now%20u==0)tk_clock_event(0xF8u,now);transport_tick(&t,&s,now);}
    check(t.swing_pending&&t.next_step==1,"odd clock event is pending");
    transport_stop(&t);tk_clock_event(0xFCu,125);
    check(!t.swing_pending&&!t.running&&t.next_step==2,"Stop cancels pending sound and consumes grid position");
    check(!transport_tick(&t,&s,200),"no delayed sound after Stop");
    tk_clock_event(0xFBu,200);t.running=1;
    for(now=200;now<=320;now+=20){fm1_ms=now;tk_clock_event(0xF8u,now);transport_tick(&t,&s,now);}
    check(t.shown_step>=2,"Continue retains sequence position");
    check(t.swing_clock_period==120u,"Continue never measures stopped time as a clock period");
    setup(100);transport_mode(&t,TK_MODE_SEQ,0);tk_clock_arm(0);tk_clock_event(0xFAu,0);
    for(now=0;now<=120;now+=20){fm1_ms=now;tk_clock_event(0xF8u,now);transport_tick(&t,&s,now);}
    for(now=140;now<=240;now+=20)tk_clock_event(0xF8u,now);
    fm1_ms=240;check(!transport_tick(&t,&s,240)&&!t.running,"stalled delayed step stops instead of burst");
}
static uint32_t command(uint8_t code,const uint8_t *payload,uint32_t n,uint8_t *out,uint32_t *length)
{
    tk_remote_message_t request={0};tk_lfo_t lfo={0},tracks[4]={{0}};uint8_t shades[4]={0},shade=s.p.shade;
    request.command=code;request.payload=payload;request.length=(uint8_t)n;
    return tk_remote_command(&request,&s,&t,&shade,&lfo,tracks,shades,TK_PAGE_SEQ,0,out,length);
}
static void controls_and_transaction(const char *folder)
{
    uint32_t hold=0,n,flags;uint8_t shade,out[80],p[19]={90,0,0,1,0,0,0,0,0,0,0,32,4,4,0,0,0,0,63};tk_lfo_t lfo={0};
    setup(0);shade=s.p.shade;
    check(tk_page_knob(&s,&t,TK_PAGE_SEQ,2,63,&hold,&shade,&lfo)==7&&s.swing==63,"overview K3 edits saved swing");
    t.editing=1;tk_page_knob(&s,&t,TK_PAGE_SEQ,2,1,&hold,&shade,&lfo);
    check(t.edit_step==1&&s.swing==63,"EDIT K3 still navigates steps");t.editing=0;
    flags=command(TK_REMOTE_SWING,0,0,out,&n);check(!flags&&n==5&&out[2]==63,"versioned swing readback");
    transport_mode(&t,TK_MODE_SEQ,0);
    command(TK_REMOTE_SONG_BEGIN,0,0,out,&n);check(out[0]==TK_REMOTE_UNSUPPORTED&&t.running&&!tk_banks.importing,"legacy BEGIN refuses before Stop");
    p[0]=1;command(TK_REMOTE_SONG_BEGIN,p,1,out,&n);check(out[0]==0&&tk_banks.importing&&!t.running,"negotiated import stages stopped song");
    command(TK_REMOTE_SONG_COMMIT,0,0,out,&n);check(out[0]!=0&&s.swing==63,"COMMIT requires explicit staged swing");
    p[0]=90;command(TK_REMOTE_SONG_GLOBAL,p,19,out,&n);check(out[0]==0,"complete globals stage swing");
    command(TK_REMOTE_SONG_COMMIT,0,0,out,&n);check(out[0]==0&&s.swing==63,"COMMIT/readback keep swing");
    p[0]=101;command(TK_REMOTE_SWING,p,1,out,&n);check(out[0]==TK_REMOTE_BAD_ARGUMENT&&s.swing==63,"malformed swing cannot mutate");
    if(folder){char path[1024];memset(&seq_drawn,0,sizeof seq_drawn);lcd_fill(0,0,240,240,0);draw_header(TK_PAGE_SEQ);draw_seq(&s,&t);snprintf(path,sizeof path,"%s/swing-overview.ppm",folder);write_ppm_as(path);
        t.editing=1;memset(&seq_drawn,0,sizeof seq_drawn);lcd_fill(0,0,240,240,0);draw_header(TK_PAGE_SEQ);draw_seq(&s,&t);snprintf(path,sizeof path,"%s/swing-edit.ppm",folder);write_ppm_as(path);}
}
static void capture(void)
{
    uint32_t now;setup(100);s.tempo=120;t.selected=TK_TRACK_MELODY;tk_pattern_clear(&s.pattern[1]);
    transport_mode(&t,TK_MODE_SEQ,0);transport_tick(&t,&s,0);t.recording=1;t.record_pattern=&s.pattern[1];t.record_length=16;
    check(transport_capture_start(&t,0,60,100,0,125),"REC starts immediate live gesture");
    now=t.next_ms;fm1_ms=now;transport_tick(&t,&s,now);
    transport_capture_end(&t,0,60,now);check(s.pattern[1].step[0].note[0].duration_q4==4,"long swung interval records one sixteenth");
    check(transport_capture_start(&t,0,62,100,now,125),"REC offbeat gesture");
    now=t.next_ms;fm1_ms=now;transport_tick(&t,&s,now);transport_capture_end(&t,0,62,now);
    check(s.pattern[1].step[1].note[0].duration_q4==4,"short swung interval records one sixteenth");
}
static void four_tracks(void)
{
    uint32_t track,step,i;setup(100);s.tempo=120;
    for(track=0;track<4;track++){
        track_factory_apply(&s,track,0);publish_track(&s,track);tk_pattern_clear(&s.pattern[track]);
        for(step=0;step<16;step++)tk_pattern_record(&s.pattern[track],step,48+track*3+step%2,100);
    }
    transport_mode(&t,TK_MODE_SEQ,0);
    for(step=0;step<16;step++){
        uint32_t now=t.next_ms;fm1_ms=now;check(transport_tick(&t,&s,now),"four-track global tick");
        for(track=0;track<4;track++){
            uint32_t found=0;
            for(i=0;i<TK_VOICES;i++)if(t.playing[i].active&&t.playing[i].part==track){
                check(t.playing[i].started_ms==now,"all four tracks start on same swung event");
                check(t.playing[i].due_ms==now+tk_swing_interval(125,100,step)*75u/100u,"independent default gates share same grid");found++;
            }
            check(found==1,"each track retains its own voice/part");
        }
    }
    transport_stop(&t);check(!transport_playing_count(&t),"Stop releases all four source owners");
}
static int old_pull(void *context,uint32_t at,uint8_t *out,uint32_t n)
{
    uint32_t i;uint8_t meta[16];tk_settings_t *core=context;
    tk_store_put32(meta,1);tk_store_put32(meta+4,3700);tk_store_put32(meta+8,19832);tk_store_put32(meta+12,0);
    for(i=0;i<n;i++,at++)out[i]=at<16?meta[at]:at<3716?((uint8_t*)core)[at-16]:((uint8_t*)&song)[at-3716];
    return 0;
}
static void saves(void)
{
    uint32_t slot,length,generation;uint8_t header[32];setup(67);host_journal_import();memset(host_journal,255,sizeof host_journal);
    s.home_visual=TK_HOME_VISUAL_RIFT;s.section_mutes=0x4218;expected=s;
    check(!tk_persist_save(&s,&t,0),"native SAVE complete swing song");
    s.swing=0;check(!tk_persist_load(&s,&t)&&s.swing==67,"native reboot retains swing");
    check(!memcmp(&s,&expected,sizeof s),"all settings and cosmetic fields survive");
    s.swing=25;check(!tk_persist_save(&s,&t,0),"second journal generation");
    tk_store_latest(&slot,&length,&generation);
    memcpy(header,host_journal+slot*TK_STORE_SLOT_BYTES,32u);
    memset(host_journal+slot*TK_STORE_SLOT_BYTES,255,32u);
    check(!tk_persist_load(&s,&t)&&s.swing==67,"interrupted publication keeps previous complete swing/song");
    memcpy(host_journal+slot*TK_STORE_SLOT_BYTES,header,32u);
    host_journal[slot*TK_STORE_SLOT_BYTES+TK_STORE_HEADER+16+3700]^=1u;
    check(!tk_persist_load(&s,&t)&&s.swing==67,"CRC corruption recovers prior swing/song");
    memset(host_journal,255,sizeof host_journal);tk_song_clone(&s,&t);song=tk_bank_pool.stage;s.schema=12;s.swing=0;
    check(!tk_store_save(TK_PERSIST_V1_BYTES,old_pull,&s),"legacy complete journal fixture");
    check(!tk_persist_load(&s,&t)&&s.schema==13&&s.swing==0,"old journal migrates to exact straight timing");
    check(s.section_mutes==0x4218&&s.home_visual==TK_HOME_VISUAL_RIFT,"legacy visual/mutes not mistaken for swing");
    expected=s;expected.schema=12;check(settings_upgrade_v12(&s,&expected)&&s.swing==0,"schema12 active settings migration");
    check(sizeof s==3704&&sizeof(tk_song_stage_t)==19836&&TK_PERSIST_BYTES==23556,"separate save budgets exact");
}
int main(int argc,char **argv)
{
    arithmetic();internal();external();capture();four_tracks();saves();controls_and_transaction(argc>1?argv[1]:0);
    printf("PASS swing: %u checks, native0/100 grid/gates/REC/clock/Stop/Continue/legacy/save/controls\n",checks);return 0;
}
