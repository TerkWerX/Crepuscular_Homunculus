/* SPDX-License-Identifier: GPL-3.0-only */
/* Generate non-private browser fixtures from the actual current firmware. */
#define TERKINSTEIN_HOST 1
#define main original_app_main
#include "../firmware/src/terkinstein.c"
#undef main
static tk_settings_t state;
static tk_transport_t transport;
static tk_lfo_t lfo,track_lfo[4];
static uint8_t shade,track_shade[4];
static unsigned entries;
static void emit(unsigned command,uint8_t *p,unsigned n) {
 tk_remote_message_t q;uint8_t reply[TK_REMOTE_PAYLOAD_MAX];uint32_t length=0,i;
 q.direction=TK_REMOTE_REQUEST;q.command=(uint8_t)command;q.id=1;q.length=(uint8_t)n;q.payload=p;
 (void)tk_remote_command(&q,&state,&transport,&shade,&lfo,track_lfo,track_shade,TK_PAGE_PAD,0,reply,&length);
 if(!length||reply[0]!=TK_REMOTE_OK){fprintf(stderr,"fixture command %u failed\n",command);exit(1);}
 printf("%s\"%u:",entries++?",\n":"",command);for(i=0;i<n;i++)printf("%s%u",i?",":"",p[i]);
 printf("\":[");for(i=1;i<length;i++)printf("%s%u",i>1?",":"",reply[i]);printf("]");
}
int main(void){unsigned i,t,b,k;static const unsigned slots[4]={47,112,119,126};uint8_t p[3];
 tk_init();tk_visual_init();tk_metronome_init();tk_ready=1;settings_defaults(&state);memset(&transport,0,sizeof transport);
 tk_banks_init(&state,&transport);preset_apply(&state,112);state.tempo=97;state.swing=35;
 for(t=0;t<4;t++){track_factory_apply(&state,t,slots[t]);track_shade[t]=state.track[t].p.shade;tk_lfo_init(&track_lfo[t],0);}
 remote_ui_splash=0; /* Synthetic post-HOME state, never physical evidence. */
 shade=state.p.shade;tk_lfo_init(&lfo,0);printf("{\"queries\":{\n");
 {const unsigned reads[]={1,2,7,12,13,14,46,56,66};for(i=0;i<sizeof reads/sizeof reads[0];i++)emit(reads[i],0,0);}
 for(i=0;i<5;i++){p[0]=(uint8_t)i;emit(4,p,1);}
 for(t=0;t<4;t++){p[0]=(uint8_t)t;emit(6,p,1);emit(9,p,1);}
 for(i=0;i<12;i++){p[0]=(uint8_t)i;emit(48,p,1);}
 for(i=0;i<8;i++){p[0]=(uint8_t)i;emit(52,p,1);}
 for(t=0;t<4;t++)for(k=0;k<64;k++){p[0]=(uint8_t)t;p[1]=(uint8_t)k;emit(3,p,2);}
 for(t=0;t<4;t++)for(b=0;b<4;b++)for(k=0;k<64;k++){p[0]=(uint8_t)t;p[1]=(uint8_t)b;p[2]=(uint8_t)k;emit(8,p,3);emit(50,p,3);}
 printf("},\"expected\":{\"LIVE\":112,\"captured\":[47,112,119,126],\"tempo\":97,\"swing\":35}}\n");return 0;
}
