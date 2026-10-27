/* SPDX-License-Identifier: GPL-3.0-only */
/* Compile the actual target lcd.c against an asynchronous fake SPI panel. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#ifdef _MSC_VER
#define __attribute__(x)
#endif
#define TERKINSTEIN_SCREEN_MIRROR 1
#define TERKINSTEIN_LCD_COOPERATIVE 1
#include "terkinstein_screen.h"
static uint16_t panel[240u*240u];
static const uint8_t *dma;
static uint32_t dma_n,op,x0,x1,y0,y1,px,py,bytes,waits;
static void fm1_lcd_hw_init(void){}
static void fm1_lcd_baud(uint32_t n){(void)n;}
static void fm1_lcd_deselect(void){}
static void fm1_lcd_wait(void)
{
    uint32_t i;
    ++waits;
    if(!dma_n)return;
    if(op==0x2Au){assert(dma_n==4);x0=((uint32_t)dma[0]<<8)|dma[1];x1=((uint32_t)dma[2]<<8)|dma[3];}
    else if(op==0x2Bu){assert(dma_n==4);y0=((uint32_t)dma[0]<<8)|dma[1];y1=((uint32_t)dma[2]<<8)|dma[3];}
    else if(op==0x2Cu){
        assert(!(dma_n&1u));bytes+=dma_n;
        for(i=0;i<dma_n;i+=2){assert(px<240&&py<240);panel[py*240u+px]=(uint16_t)(((uint32_t)dma[i]<<8)|dma[i+1]);
            if(++px>x1){px=x0;if(++py>y1)py=y0;}}
    }
    dma=0;dma_n=0;
}
static void fm1_lcd_send_cmd(uint8_t command)
{assert(!dma_n);op=command;if(op==0x2Cu){px=x0;py=y0;}}
static void fm1_lcd_send_data(const void *p,uint32_t n)
{assert(!dma_n);dma=p;dma_n=n;}
#include "../firmware/src/lcd.c"
static uint32_t checks;
static void check(int yes,const char *why)
{++checks;if(!yes){fprintf(stderr,"FAIL target LCD: %s\n",why);exit(1);}}
static void drain(void)
{
    uint32_t n=0,count,old;
    do{old=bytes;count=lcd_cooperative_upload();check(bytes-old<=TK_LCD_BATCH_PIXELS*2u,"native SPI batch bound");
        check(!dma_n&&!lcd_busy,"physical DMA completed before mirror eligibility");
        check(++n<=TK_SCREEN_TILES,"native flush converges");}while(count);
    check(!memcmp(panel,tk_screen_pool.pixels,sizeof panel),"actual target backend byte order and delivered pixels match");
}
int main(void)
{
    uint16_t small[9]={0x1F00u,0xE007u,0x00F8u,0xFFFFu,0,0xAAAAu,0x5555u,0xE0FFu,0xFF07u};
    uint32_t old,i;
    lcd_init();lcd_sync();check(!tk_lcd_queue.active,"boot LCD init stays immediate");
    lcd_fill(0,0,240,240,0x3456u);lcd_sync();
    check(!memcmp(panel,tk_screen_pool.pixels,sizeof panel),"immediate boot frame exact");
    lcd_cooperative_begin();old=bytes;
    lcd_fill(0,0,240,240,0x1234u);lcd_blit(238,238,3,3,small);
    check(bytes==old,"target fill/blit enqueue without blocking full-page SPI");
    drain();check(panel[238u*240u+238u]==0x001Fu&&panel[239u*240u+239u]==0,"clipped blit retains original input stride");
    for(i=0;i<17;++i){lcd_fill(i*13u%240u,i*17u%240u,17,9,(uint16_t)(i*3001u));lcd_cooperative_upload();}
    drain();old=bytes;lcd_fill(0,0,16,16,0xABCDu);lcd_fill(0,0,16,16,0x1234u);drain();
    check(bytes-old==512u,"superseded same-tile paints coalesce into one snapshot");
    lcd_fill(0,0,240,240,0xF800u);lcd_cooperative_upload();
    lcd_cooperative_immediate();old=bytes;
    lcd_fill(0,0,240,240,0x07E0u);lcd_sync();
    check(bytes-old==240u*240u*2u,"emergency screen draws immediately and completely");
    check(!lcd_cooperative_upload(),"emergency clear cancels stale pending screen");
    check(!memcmp(panel,tk_screen_pool.pixels,sizeof panel),"emergency display exact");
    lcd_cooperative_begin();lcd_fill(0,0,1,1,0x001Fu);drain();
    check(panel[0]==0x001Fu,"ordinary queue resumes after noncommitting updater return");
    lcd_fill(0,0,240,240,0xFFFFu);drain();
    memset(tk_screen.dirty,0,sizeof tk_screen.dirty); /* mirror already current */
    lcd_fill(0,0,240,240,0); /* desired black, physical/mirrored white */
    lcd_cooperative_immediate();lcd_fill(0,0,240,240,0);lcd_sync();
    for(i=0;i<TK_SCREEN_TILES;++i)
        check(tk_screen.dirty[i],"same-color immediate override still republishes formerly queued tile");
    check(!memcmp(panel,tk_screen_pool.pixels,sizeof panel),"immediate override after cancel remains exact");
    printf("PASS actual target LCD backend: %u checks; DMA, clipping, coalescing, bounds and immediate recovery; queue %u bytes\n",checks,(unsigned)sizeof tk_lcd_queue);
    return 0;
}
