/* SPDX-License-Identifier: GPL-3.0-only */
/* Real app protocol ownership helpers, no device access. */
#define TERKINSTEIN_HOST 1
#define main panel_input_host_main
#include "../firmware/src/terkinstein.c"
#undef main
static uint32_t checks;
static uint8_t out[16];
static void check(int value,const char *why)
{
    checks++;
    if(!value){fprintf(stderr,"FAIL panel input %u: %s\n",checks,why);exit(1);}
}
static void claim(void)
{
    uint8_t p=0;
    check(tk_panel_input_command(&p,1u,out)==4u && out[0]==0u,"claim");
}
static uint32_t send(uint32_t action,uint32_t token,uint32_t serial,uint32_t a,uint32_t b)
{
    uint8_t p[7];
    p[0]=(uint8_t)action;p[1]=token&127;p[2]=token>>7;p[3]=serial&127;p[4]=serial>>7;p[5]=(uint8_t)a;p[6]=(uint8_t)b;
    return tk_panel_input_command(p,action==3u?5u:7u,out);
}
int main(void)
{
    uint32_t i,token;
    fm1_ms=100u;
    check(tk_panel_input_command(0,0,out)==6u && out[1]==1u && out[2]==14u && out[3]==7u && (out[4]|out[5]<<7)==350u,"read-only descriptor");
    check(!tk_panel_input.token,"query cannot claim");claim();token=tk_panel_input.token;
    check(send(1,token,1,64,0)==5u && out[0]==0u && tk_panel_input.held==64u,"EDIT press");
    check(send(1,token,1,0,0)==1u && out[0]==3u && tk_panel_input.held==64u,"duplicate cannot release");
    check(send(1,token+1,2,0,0)==1u && out[0]==3u,"wrong session rejected");
    check(send(1,token,2,0,0)==5u && tk_panel_input.released==64u,"normal release");
    check(send(2,token,3,7,65)==1u && out[0]==TK_REMOTE_BAD_ARGUMENT,"invalid encoder");
    check(send(2,token,3,0,64)==1u && out[0]==TK_REMOTE_BAD_ARGUMENT,"zero delta rejected");
    for(i=3;i<20;i++)check(send(2,token,i,0,72)==5u,"bounded rotation accepted");
    check(tk_panel_input_encoder(0)==32 && tk_panel_input_encoder(0)==0,"accumulator bounded and consumed");
    tk_panel_input.serial=16383u;
    check(send(1,token,0,4,0)==5u && tk_panel_input.held==4u,"serial wrap");
    check(send(1,token,8192,0,0)==1u && out[0]==3u,"ambiguous serial rejected");
    fm1_ms=449u;tk_panel_input_expire(fm1_ms);check(tk_panel_input.held==4u,"lease 349ms");
    fm1_ms=450u;tk_panel_input_expire(fm1_ms);
    check(!tk_panel_input.token && !tk_panel_input.held && !tk_panel_input.released && tk_panel_input.canceled==4u,"expiry cancels without tap");
    check(send(1,token,1,4,0)==1u && out[0]==3u,"expired token cannot resurrect hold");
    claim();check(tk_panel_input.token!=token,"claim advances session");
    token=tk_panel_input.token;
    check(send(1,token,1,64,0)==5u,"second EDIT press");
    claim();check(!tk_panel_input.held && (tk_panel_input.canceled&64u),"reclaim cancels prior hold");
    tk_panel_input.next_token=16383u;claim();check(tk_panel_input.token==1u,"token wrap skips zero");
    fm1_ms=0xffffff00u;claim();token=tk_panel_input.token;
    send(1,token,1,4,0);fm1_ms=93u;tk_panel_input_expire(fm1_ms);
    check(tk_panel_input.held==4u,"clock wrap 349ms");
    fm1_ms=94u;tk_panel_input_expire(fm1_ms);check(!tk_panel_input.held,"clock wrap 350ms");
    printf("Panel input: %u checks passed\n",checks);return 0;
}
