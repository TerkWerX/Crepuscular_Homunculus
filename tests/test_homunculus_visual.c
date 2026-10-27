/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual HOME renderer: bounded cooperative work, immutable snapshot, portrait
 * integrity and finite silence. Host results do not qualify device CPU/SPI. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _MSC_VER
#define __attribute__(x)
#endif
#define RGB(r,g,b) ((uint16_t)((((r)>>3)<<11)|(((g)>>2)<<5)|((b)>>3)))
#define C_BLACK 0u
#define GUARD 0x7193A5C6u
static uint32_t checks, pixels, batches, width, height;
static struct { uint32_t before; uint16_t pixels[512]; uint32_t after; } canvas;
static struct { uint32_t before; uint16_t pixels[240*240]; uint32_t after; } panel;
#define cv_px canvas.pixels
static void expect(int yes, const char *what)
{
    checks++;
    if (!yes) { fprintf(stderr,"FAIL: %s\n",what); exit(1); }
}
static uint16_t swap16(uint32_t color) { return (uint16_t)((color>>8)|((color&255u)<<8)); }
static void cv_begin(uint32_t w,uint32_t h,uint16_t color);
static void cv_blit(uint32_t x,uint32_t y);
#include "../firmware/src/terkinstein_engine.h"
#include "../firmware/src/terkinstein_visual.h"
#include "../firmware/src/terkinstein_homunculus_visual.h"
static uint16_t frozen[TK_RIFT_W*TK_RIFT_H];
static void cv_begin(uint32_t w,uint32_t h,uint16_t color)
{
    uint32_t i;
    expect(w && w<=64u && h==8u && w*h<=512u,"bounded canvas transfer");
    width=w;height=h;
    for(i=0;i<w*h;i++) canvas.pixels[i]=swap16(color);
}
static void cv_blit(uint32_t x,uint32_t y)
{
    uint32_t row,col;
    expect(x>=TK_RIFT_X && y>=TK_RIFT_Y && x+width<=TK_RIFT_X+TK_RIFT_W &&
        y+height<=TK_RIFT_Y+TK_RIFT_H,"upload confined to HOME viewport");
    for(row=0;row<height;row++) for(col=0;col<width;col++) {
        uint32_t at=(y-TK_RIFT_Y+row)*TK_RIFT_W+x-TK_RIFT_X+col;
        expect(canvas.pixels[row*width+col]==tk_rift_back[at] &&
            canvas.pixels[row*width+col]==tk_rift_front[at],"LCD matches immutable frame");
        panel.pixels[(y+row)*240u+x+col]=canvas.pixels[row*width+col];
    }
    pixels+=width*height;batches++;
}
static uint32_t hash(void)
{
    uint32_t i,h=2166136261u;
    for(i=0;i<TK_RIFT_W*TK_RIFT_H;i++) h=(h^tk_rift_front[i])*16777619u;
    return h;
}
static uint32_t frame(const tk_visual_snapshot_t *sound,uint32_t now,uint32_t preset)
{
    uint32_t i,before=pixels,x,y;
    tk_visual_snapshot_t saved=*sound,other;
    memset(&other,255,sizeof other);
    for(i=0;i<256u;i++) {
        tk_homunculus_visual_prepare(sound,now+i,preset);
        expect(tk_rift_line_count<=TK_RIFT_LINE_MAX,"bounded geometry list");
        if(tk_homunculus_visual_pending() || (!tk_rift_phase && !tk_rift_force)) break;
    }
    expect(i<256u,"cooperative construction terminates");
    memcpy(frozen,tk_rift_back,sizeof frozen);
    for(i=0;i<256u && tk_homunculus_visual_pending();i++) {
        uint32_t sent,old=pixels;
        tk_homunculus_visual_prepare(&other,now+100000u,preset+1u);
        expect(!memcmp(frozen,tk_rift_back,sizeof frozen),"pending back image remains immutable");
        sent=tk_homunculus_visual_upload();
        expect(sent && sent<=512u && sent==pixels-old,"one upload <=512 pixels");
    }
    expect(i<256u && !tk_homunculus_visual_pending(),"finite upload completes");
    expect(!memcmp(sound,&saved,sizeof saved),"renderer never modifies sound snapshot");
    expect(canvas.before==GUARD && canvas.after==GUARD && panel.before==GUARD && panel.after==GUARD,"guards intact");
    for(y=0;y<240u;y++) for(x=0;x<240u;x++)
        if(x<TK_RIFT_X || x>=TK_RIFT_X+TK_RIFT_W || y<TK_RIFT_Y || y>=TK_RIFT_Y+TK_RIFT_H)
            expect(panel.pixels[y*240u+x]==0xBEEFu,"header/footer remain untouched");
    for(y=TK_HOMUNCULUS_HOME_Y;y<TK_HOMUNCULUS_HOME_Y+TK_HOMUNCULUS_HOME_H;y++)
        for(x=TK_HOMUNCULUS_HOME_X;x<TK_HOMUNCULUS_HOME_X+TK_HOMUNCULUS_HOME_W;x++) {
            uint16_t portrait=tk_homunculus_visual_pixel(x,y,tk_homunculus_visual_frame);
            if(portrait)expect(tk_rift_front[y*TK_RIFT_W+x]==portrait,"lightning never covers opaque portrait pixels");
        }
    return pixels-before;
}
static uint32_t independent(tk_visual_snapshot_t *sound)
{
    tk_homunculus_visual_reset(); frame(sound,1000u,3u); return hash();
}
static void write_preview(const char *path)
{
    FILE *f=fopen(path,"wb");uint32_t i;
    expect(f!=NULL,"preview file opens");fprintf(f,"P6\n240 240\n255\n");
    for(i=0;i<240u*240u;i++) {
        uint32_t c=panel.pixels[i]==0xBEEFu?0u:swap16(panel.pixels[i]);
        unsigned char rgb[3];
        rgb[0]=(unsigned char)(((c>>11)&31u)*255u/31u);
        rgb[1]=(unsigned char)(((c>>5)&63u)*255u/63u);
        rgb[2]=(unsigned char)((c&31u)*255u/31u);fwrite(rgb,1,3,f);
    }
    fclose(f);
}
int main(int argc,char **argv)
{
    tk_visual_snapshot_t silent,sound;uint32_t i,t,base,active,before;
    canvas.before=canvas.after=panel.before=panel.after=GUARD;
    for(i=0;i<240u*240u;i++)panel.pixels[i]=0xBEEFu;
    memset(&silent,0,sizeof silent);
    silent.voice[0]=60u|(TK_SOURCE_DRUM<<16)|(1u<<24);
    tk_homunculus_visual_reset();
    expect(frame(&silent,0u,0u)==TK_RIFT_W*TK_RIFT_H,"entry paints full viewport");
    for(t=500u;t<=5000u;t+=500u)frame(&silent,t,0u);
    expect(tk_rift_asleep,"held silent kit reaches sleep");base=hash();before=pixels;
    for(t=5001u;t<9000u;t++) {
        tk_homunculus_visual_prepare(&silent,t,0u);
        expect(!tk_rift_phase && !tk_homunculus_visual_pending() && !tk_homunculus_visual_upload(),"zero LCD work at rest");
    }
    expect(pixels==before && hash()==base,"resting portrait remains static");
    if(argc>1)write_preview(argv[1]);
    sound=silent;sound.peak=6000u;
    sound.voice[7]=67u|(220u<<8)|(TK_SOURCE_ATMOS<<16)|(1u<<24);
    for(i=0;i<64u;i++)sound.mono[i]=(int16_t)(tk_sine(i*201326592u)>>2);
    expect(frame(&sound,10000u,0u)>0u && !tk_rift_asleep,"sound wakes lightning");
    expect(hash()!=base,"lightning visibly responds");
    if(argc>2)write_preview(argv[2]);
    for(t=10500u;t<15000u;t+=500u)frame(&silent,t,0u);
    expect(tk_rift_asleep && hash()==base,"finite fade restores exact resting portrait");
    base=independent(&sound);
    for(i=0;i<64u;i++)sound.side[i]=(int16_t)(tk_sine(i*335544320u)>>3);
    active=independent(&sound);expect(active!=base,"stereo independently changes lightning");base=active;
    sound.rough=30000u;active=independent(&sound);expect(active!=base,"roughness changes branching");base=active;
    sound.voice[7]=71u|(220u<<8)|(TK_SOURCE_ATMOS<<16)|(1u<<24);
    active=independent(&sound);expect(active!=base,"voice eight pitch changes discharge");
    sound.voice[7]=0u;base=independent(&sound);expect(base!=active,"voice eight has visible discharge");
    sound.peak=1u;expect(independent(&sound)!=base,"peak changes the field");
    memset(&sound,255,sizeof sound);independent(&sound);
    for(i=0;i<64u;i++){sound.mono[i]=(int16_t)(i&1u?32767:-32768);sound.side[i]=(int16_t)(i&1u?-32768:32767);}
    tk_homunculus_visual_reset();frame(&sound,0xFFFFFFF0u,0xFFFFFFFFu);frame(&sound,500u,0u);
    tk_homunculus_visual_reset();tk_homunculus_visual_prepare(&sound,1000u,0u);
    expect(tk_rift_phase && !tk_homunculus_visual_pending(),"partial frame unpublished");
    tk_homunculus_visual_reset();expect(frame(&sound,2000u,0u)==TK_RIFT_W*TK_RIFT_H,"reset discards partial frame");
    tk_homunculus_visual_reset();
    for(i=0;i<256u && !tk_homunculus_visual_pending();i++)tk_homunculus_visual_prepare(&sound,3000u+i,0u);
    expect(tk_homunculus_visual_upload()>0u,"upload starts");tk_homunculus_visual_reset();
    expect(frame(&silent,4000u,0u)==TK_RIFT_W*TK_RIFT_H,"reset repairs interrupted upload");
    expect(sizeof tk_rift_front+sizeof tk_rift_back==129024u,"existing pool framebuffer budget preserved");
    printf("Homunculus visual passed: %u checks; %u bounded tile batches\n",checks,batches);return 0;
}
