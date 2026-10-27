/* SPDX-License-Identifier: GPL-3.0-only */
/* stdin/stdout fake query owner for complete-song adapter tests. No real MIDI. */
#define TERKINSTEIN_HOST 1
#define main tk_swing_query_original_main
#include "../firmware/src/terkinstein.c"
#undef main
int main(void)
{
    static tk_settings_t s;static tk_transport_t t;
    tk_lfo_t lfo={0},tracks[4]={{0}};uint8_t shades[4]={0},shade,p[80],out[80];
    uint32_t command,n,i,track,bank,step,length;tk_remote_message_t request={0};
    settings_defaults(&s);s.swing=67;tk_init();tk_ready=1;tk_metronome_init();
    tk_metronome_clock_init(&metro_clock);tk_banks_init(&s,&t);shade=s.p.shade;publish_params(&s,shade);
    for(track=0;track<4;track++)for(bank=0;bank<4;bank++){
        tk_pattern_t *pattern=tk_bank_pattern(&s,track,bank);tk_pattern_clear(pattern);
        for(step=0;step<64;step++)tk_pattern_record(pattern,step,36+track*12+step%8,90+bank);
    }
    while(scanf("%u %u",&command,&n)==2){
        if(n>80)return 2;
        for(i=0;i<n;i++){uint32_t x;if(scanf("%u",&x)!=1||x>127)return 2;p[i]=(uint8_t)x;}
        request.command=(uint8_t)command;request.payload=p;request.length=(uint8_t)n;
        tk_remote_command(&request,&s,&t,&shade,&lfo,tracks,shades,TK_PAGE_SEQ,0,out,&length);
        printf("REPLY");for(i=0;i<length;i++)printf(" %u",out[i]);putchar('\n');fflush(stdout);
    }
    return 0;
}
