/* SPDX-License-Identifier: GPL-3.0-only */
/* Real application catalog admission, independent captures and native recovery. */
#define TERKINSTEIN_HOST 1
#define main catalog_app_main
#include "../firmware/src/terkinstein.c"
#undef main
static tk_settings_t s, expected, before;
static tk_transport_t t;
static tk_song_stage_t song;
static unsigned checks;
static void ck(int ok,const char *why){checks++;if(!ok){fprintf(stderr,"FAIL %u: %s\n",checks,why);exit(1);}}
static void init(void){
    tk_init();tk_metronome_init();tk_visual_init();tk_ready=1;
    memset(&t,0,sizeof t);settings_defaults(&s);tk_banks_init(&s,&t);
}
static void dense(int revised){
    unsigned tr,b,i;static const unsigned old[4]={24,51,32,30},fresh[4]={64,65,24,51};
    init();preset_apply(&s,revised?65:51);s.p.bloom=67;graft_reset(&s);
    s.graft_target=revised?32:0;s.graft_amount=39;graft_apply(&s);s.swing=50;s.tempo=90;
    for(tr=0;tr<4;tr++){
        track_factory_apply(&s,tr,revised?fresh[tr]:old[tr]);
        s.track[tr].p.shade=(uint8_t)(tr*19+7);s.fm[tr+1].op[2].level=(uint8_t)(tr*17+3);
        for(b=0;b<4;b++)for(i=0;i<64;i++){
            tk_pattern_step_t *step=&tk_bank_pattern(&s,tr,b)->step[i];
            step->count=1;step->note[0].pitch=(uint8_t)(36+tr*5+b);
            step->note[0].velocity=(uint8_t)(25+i);step->note[0].duration_q4=(uint8_t)(i+1);
            tk_arr_song.step[tr][b][i].probability=(uint8_t)(50+i%51);
        }
        tk_bank_activate(&s,&t,tr,tr,0);
    }
    tk_drum_mix[1].decay=101;tk_arr_song.scene[7].enabled=1;tk_arr_song.scene[7].tempo=97;
    memset(&tk_bank_pool.stage,0,sizeof tk_bank_pool.stage);tk_song_clone(&s,&t);
    expected=s;song=tk_bank_pool.stage;
    ck(settings_valid(&s)&&tk_song_valid(&song),"dense all-bank/captured/LIVE/GRAFT fixture valid");
}
static void reload(void){
    init();ck(!tk_persist_load(&s,&t),"journal loads without defaulting");
    ck(!memcmp(&s,&expected,sizeof s),"complete LIVE/GRAFT/FM/settings byte-exact");
    memset(&tk_bank_pool.stage,0,sizeof tk_bank_pool.stage);tk_song_clone(&s,&t);
    ck(!memcmp(&tk_bank_pool.stage,&song,sizeof song),"all1024 steps/captures/FM/arrangement/drums/swing byte-exact");
    ck(!t.running&&!t.recording,"recovery never starts transport");
}
static int legacy_pull(void *ctx,uint32_t at,uint8_t *out,uint32_t n){
    uint32_t i;if(tk_persist_pull(ctx,at,out,n))return -1;
    for(i=0;i<n;i++){
        if(at+i<4)out[i]=(at+i)==0?2:0;
        if(at+i>=12&&at+i<16)out[i]=0;
    }return 0;
}
static int foreign_pull(void *ctx,uint32_t at,uint8_t *out,uint32_t n){
    uint32_t i;if(tk_persist_pull(ctx,at,out,n))return -1;
    for(i=0;i<n;i++)if(at+i==12)out[i]=3;
    return 0;
}
int main(void){
    unsigned i,j,tr,slot,len,gen,seen[66]={0};tk_track_patch_t patch;tk_fm_patch_t fm;
    host_journal_import();
    ck(TK_PRESET_COUNT==66&&TK_FACTORY_COUNT==64&&TK_CATALOG_REVISION==2,"explicit storage/selection/catalog capacities");
    ck(TK_VOICES==8&&TK_FM_VOICE_LIMIT==4,"existing eight/four limits retained");
    for(i=0;i<64;i++){
        unsigned id=TK_FACTORY_SOUND[i];ck(id<66&&!seen[id]++,"unique current factory choice");
        ck(factory_position(id)==i,"stable visible positions");
        ck(factory_next(id,1)==TK_FACTORY_SOUND[(i+1)%64],"forward traversal and wrap");
        ck(factory_next(id,-1)==TK_FACTORY_SOUND[(i+63)%64],"reverse traversal and wrap");
        ck(factory_next(id,128)==id,"accelerated full turns");
    }
    ck(!seen[24]&&!seen[51]&&seen[64]&&seen[65],"old definitions retained outside normal browsing");
    ck(PRESETS[51].instrument==TK_INSTRUMENT_PAD&&PRESETS[65].instrument==TK_INSTRUMENT_FM,"old PAD never reinterpreted as new FM");
    for(i=0;i<66;i++)for(tr=0;tr<4;tr++){
        init();before=s;track_factory_apply(&s,tr,i);
        ck(settings_valid(&s),"every stored sound usable on every track");
        track_patch_factory(&patch,i);fm_for_preset(&fm,i);
        ck(!memcmp(&s.track[tr],&patch,sizeof patch)&&!memcmp(&s.fm[tr+1],&fm,sizeof fm),"complete captured patch/FM");
        s.track[tr]=before.track[tr];s.fm[tr+1]=before.fm[tr+1];
        ck(!memcmp(&s,&before,sizeof s),"other tracks and LIVE unchanged");
    }
    dense(0);ck(!tk_store_save(TK_PERSIST_BYTES,legacy_pull,&s),"exact old format2 admitted");reload();
    ck(!tk_persist_save(&s,&t,0),"legacy native SAVE upgrades explicitly to catalog2");reload();
    dense(1);ck(!tk_persist_save(&s,&t,0),"revised and original captures saved together");reload();
    ck(!tk_store_save(TK_PERSIST_BYTES,foreign_pull,&s),"CRC-valid foreign catalog fixture");reload();
    ck(!tk_store_latest(&slot,&len,&gen),"newest slot exists");
    memset(host_journal+slot*TK_STORE_SLOT_BYTES,255,TK_STORE_HEADER);
    reload(); /* Header-last power-loss: previous complete slot remains valid. */
    dense(1);ck(!tk_store_save(TK_PERSIST_BYTES,legacy_pull,&s),"invalid old-format new-ID fixture stored");
    reload(); /* Refuse impossible pre-revision IDs, keep previous complete song. */
    for(i=64;i<66;i++){
        init();preset_apply(&s,i);publish_params(&s,s.p.shade);
        tk_note_on(60,100,1);{int32_t pcm[512]={0};app_audio(pcm,256);}
        ck(tk_persist_save(&s,&t,0)!=0,"sound defers SAVE");tk_note_off(60,1);
        for(j=0;j<6000&&!tk_quiet();j++){int32_t pcm[512]={0};app_audio(pcm,256);}
        ck(tk_quiet(),"revised sound completes natural tails");
        ck(!tk_persist_save(&s,&t,0),"silent SAVE succeeds");
    }
    printf("PASS catalog revision: %u checks; legacy/new complete native SAVE, four independent tracks and bounded tails\n",checks);
    return 0;
}
