/* SPDX-License-Identifier: GPL-3.0-only */
#define TERKINSTEIN_HOST 1
#define main original_main
#include "../firmware/src/terkinstein.c"
#undef main
static tk_settings_t s,expected;static tk_transport_t t;static tk_song_stage_t song;
static unsigned checks;
static void ck(int v,const char*w){checks++;if(!v){fprintf(stderr,"FAIL NIGHT %s\n",w);exit(1);}}
int main(void){char title[24];unsigned i;tk_init();tk_metronome_init();tk_visual_init();settings_defaults(&s);memset(&t,0,sizeof t);tk_banks_init(&s,&t);host_journal_import();
 ck(TK_PRESET_COUNT==127&&TK_CATALOG_REVISION==3,"immutable127/catalog3");
 ck(TK_FACTORY_SOUND[47]==47&&PRESETS[47].instrument==0,"approved PAD in factory browser");
 ck(factory_position(47)==47&&factory_position(112)==47,"both revisions have correct ordinal48");
 preset_apply(&s,112);preset_title(title,&s);ck(!strcmp(title,"48 NIGHT SUTURE OLD"),"old FM displayed honestly");
 ck(PRESETS[112].instrument==3&&s.p.shade==25&&s.fm[0].algorithm==6,"old FM exact patch retained");
 track_factory_apply(&s,0,47);track_factory_apply(&s,1,112);track_factory_apply(&s,2,119);track_factory_apply(&s,3,126);
 memset(&tk_bank_pool.stage,0,sizeof tk_bank_pool.stage);tk_song_clone(&s,&t);expected=s;song=tk_bank_pool.stage;
 ck(!tk_persist_save(&s,&t,0),"old FM LIVE/new PAD capture save");
 tk_init();tk_metronome_init();settings_defaults(&s);tk_banks_init(&s,&t);
 ck(!tk_persist_load(&s,&t)&&!memcmp(&s,&expected,sizeof s),"full settings reload without reinterpretation");
 memset(&tk_bank_pool.stage,0,sizeof tk_bank_pool.stage);tk_song_clone(&s,&t);ck(!memcmp(&song,&tk_bank_pool.stage,sizeof song),"all banks/captures/FM byte exact");
 ck(s.track[0].preset==47&&s.track[0].instrument==0&&s.track[1].preset==112&&s.track[1].instrument==3,"old and new revisions coexist");
 preset_apply(&s,TK_FACTORY_SOUND[47]);preset_title(title,&s);ck(!strcmp(title,"48 NIGHT SUTURE"),"full PAD is current, not OLD");
 ck(s.p.shade==104&&s.p.fracture==34&&s.p.drift==43&&s.p.bloom==46&&s.attack==12&&s.release==53&&s.lfo_on==1&&s.lfo_rate==24&&s.lfo_depth==9,"all approved voice fields");
 for(i=0;i<64;i++)ck(factory_next(TK_FACTORY_SOUND[i],64)==TK_FACTORY_SOUND[i],"factory traversal remains bounded");
 printf("PASS NIGHT factory/OLD FM ordinal/native all-bank save compatibility checks=%u\n",checks);return 0;}
