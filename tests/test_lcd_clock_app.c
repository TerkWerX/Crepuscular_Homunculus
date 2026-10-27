/* SPDX-License-Identifier: GPL-3.0-only */
#define TERKINSTEIN_HOST 1
#define TK_TEST_LCD_HOOK 1
#define APP_VER "0.26.15-dev" /* same-version final-pixel comparison */
#define main tk_clock_display_host_main
#include "../firmware/src/terkinstein.c"
#undef main
#include <assert.h>
static tk_settings_t fixture;
static tk_transport_t clock_transport;
static uint16_t expected[240u * 240u];
static uint32_t checks, modeled_us, pulse, max_batch, max_gap, last_service;
static void check(int yes, const char *why)
{ ++checks; if (!yes) { fprintf(stderr,"FAIL LCD/clock: %s\n",why); exit(1); } }
static void reset_case(void)
{
    uint32_t track;
    host_lcd_transfer_hook = 0;
    lcd_cooperative_immediate();
    memset(&tk_screen,0,sizeof tk_screen);
    memset(&tk_screen_pool,0,sizeof tk_screen_pool);
    memset(&tk_midi_clock,0,sizeof tk_midi_clock);
    memset(&clock_transport,0,sizeof clock_transport);
    memset(fb,0,sizeof fb);
    tk_init(); tk_metronome_init(); tk_visual_init();
    tk_metronome_clock_init(&metro_clock);
    settings_defaults(&fixture);
    tk_banks_init(&fixture,&clock_transport);
    for (track=0;track<TK_TRACKS;++track) tk_pattern_clear(&fixture.pattern[track]);
    fm1_ms=host_lcd_pixels=modeled_us=pulse=max_batch=max_gap=last_service=0;
}
static void drain(void)
{
    uint32_t count=0;
    while (lcd_cooperative_upload()) check(++count<=TK_SCREEN_TILES,"finite full-screen convergence");
    check(!memcmp(fb,tk_screen_pool.pixels,sizeof fb),"LCD and exact RGB565 framebuffer converge");
}
static void paint(uint32_t page)
{
    lcd_fill(0,0,240,240,C_BLACK); draw_header(page);
    if(page==TK_PAGE_GLO) draw_diagnostics();
    else if(page==TK_PAGE_SEQ) draw_seq(&fixture,&clock_transport);
    else if(page==TK_PAGE_ENV) draw_envelope(&fixture,0);
    else if(page==TK_PAGE_FX) draw_fx(&fixture);
    else {
        /* Match app_main's PAD page-entry cache invalidation. */
        home_preset_drawn=home_modified_drawn=home_status_drawn=home_graft_drawn=~0u;
        draw_home(&fixture,0,0,0);
    }
}
static void pixels_and_queue(const char *folder)
{
    static const uint32_t pages[]={TK_PAGE_PAD,TK_PAGE_GLO,TK_PAGE_SEQ,TK_PAGE_ENV,TK_PAGE_FX};
    uint32_t i,k;
    for(i=0;i<sizeof pages/sizeof pages[0];++i){
        reset_case();paint(pages[i]);memcpy(expected,fb,sizeof expected);
        reset_case();lcd_cooperative_begin();host_lcd_pixels=0;
        paint(pages[i]);check(!host_lcd_pixels,"full UI paint starts no physical LCD transfer");
        drain();check(!memcmp(expected,fb,sizeof fb),"final page pixels identical to immediate original renderer");
        if(folder&&pages[i]==TK_PAGE_GLO){char path[1024];snprintf(path,sizeof path,"%s/diagnostics.ppm",folder);write_ppm_as(path);}
    }
    reset_case();lcd_cooperative_begin();paint(TK_PAGE_GLO);lcd_cooperative_upload();
    paint(TK_PAGE_PAD);drain();memcpy(expected,fb,sizeof expected);
    reset_case();paint(TK_PAGE_PAD);
    check(!memcmp(expected,fb,sizeof fb),"rapid GLO-to-HOME cancels stale desired content without old replay");

    reset_case();lcd_cooperative_begin();lcd_fill(0,0,240,240,C_WHITE);
    for(i=0;i<113;++i){lcd_fill(0,0,1,1,i&1?C_BLACK:C_WHITE);lcd_cooperative_upload();}
    for(k=1;k<TK_SCREEN_TILES;++k) check(!tk_lcd_queue.dirty[k],"round-robin services quiet tiles under continuous animation");
    drain();
    lcd_fill(0,0,240,240,TK_GREEN);check(tk_lcd_queue.dirty[224],"pending physical frame exists");
    lcd_cooperative_immediate();check(!tk_lcd_queue.active,"emergency/updater switches to immediate drawing");
    lcd_fill(0,0,240,240,C_BLACK);
    check(!memcmp(fb,tk_screen_pool.pixels,sizeof fb),"emergency full clear matches capture immediately");
    check(!lcd_cooperative_upload(),"no queued ordinary image overwrites emergency screen");
    lcd_cooperative_begin();lcd_fill(239,239,50,50,TK_GREEN);drain();
    check(fb[240u*240u-1u]==TK_GREEN,"edge clipping retains final pixel");
}
static int send_ok(const uint8_t *data,uint32_t n){(void)data;(void)n;return 1;}
static void mirror_ownership(void)
{
    uint32_t color;
    reset_case();lcd_cooperative_begin();lcd_fill(0,0,16,16,TK_GREEN);
    tk_screen_subscribe(3,0,0);
    check(tk_screen_pump(1,0,1,send_ok)&&tk_screen.tile!=0,"mirror skips tile awaiting physical completion");
    tk_screen.transmitting=0;tk_screen.cursor=0;
    lcd_cooperative_upload();
    check(tk_screen_pump(2,0,1,send_ok)&&tk_screen.tile==0,"completed physical tile becomes mirror eligible");
    color=tk_screen_pool.raw[0]|((uint32_t)tk_screen_pool.raw[1]<<8);
    check(color==fb[0]&&color==TK_GREEN,"mirror snapshot contains actually delivered pixels");
    lcd_fill(0,0,16,16,C_WHITE);
    color=tk_screen_pool.raw[0]|((uint32_t)tk_screen_pool.raw[1]<<8);
    check(color==TK_GREEN&&fb[0]==TK_GREEN,"later queued paint cannot tear immutable mirror or physical snapshot");
    tk_screen_subscribe(0,3,0);drain();check(fb[0]==C_WHITE,"physical queue runs with mirror disabled");
    tk_screen_reset(42);lcd_fill(0,0,16,16,TK_GREEN);drain();
    check(fb[0]==TK_GREEN,"mirror generation reset never cancels physical queue");
    lcd_fill(0,0,240,240,C_WHITE);drain();memset(tk_screen.dirty,0,sizeof tk_screen.dirty);
    lcd_fill(0,0,240,240,C_BLACK);lcd_cooperative_immediate();lcd_fill(0,0,240,240,C_BLACK);
    for(color=0;color<TK_SCREEN_TILES;++color)
        check(tk_screen.dirty[color],"canceled desired-color clear republishes the actual changed physical tile");
}
static void transfer_cost(uint32_t pixels)
{
    /* Stress model:2us/pixel, not a target measurement or SPI specification.
     * Do not service the simulated main/clock while one physical batch runs. */
    if(pixels>max_batch)max_batch=pixels;
    modeled_us+=pixels*2u;fm1_ms=modeled_us/1000u;
}
static int clock_case(uint32_t amount,uint32_t phase,int cooperative)
{
    uint32_t painted=0,first=600u+phase;
    reset_case();fixture.tempo=90;fixture.swing=amount;
    lcd_fill(0,0,240,240,C_WHITE);
    if(cooperative)lcd_cooperative_begin();
    host_lcd_transfer_hook=transfer_cost;
    transport_mode(&clock_transport,TK_MODE_SEQ,0);tk_clock_arm(0);tk_clock_event(0xFAu,0);
    for(;modeled_us<2200000u;modeled_us+=1000u){
        uint32_t gap=modeled_us-last_service;
        if(gap>max_gap)max_gap=gap;last_service=modeled_us;fm1_ms=modeled_us/1000u;
        transport_tick(&clock_transport,&fixture,fm1_ms);
        if(!clock_transport.running){host_lcd_transfer_hook=0;return 1;}
        while(pulse*250000u/9u<=modeled_us){tk_clock_event(0xF8u,fm1_ms);++pulse;}
        if(!painted&&fm1_ms>=first){paint(TK_PAGE_GLO);painted=1;}
        else if(painted==1&&fm1_ms>=first+300u){paint(TK_PAGE_PAD);painted=2;}
        else if(painted==2&&fm1_ms>=first+600u){paint(TK_PAGE_GLO);painted=3;}
        if(cooperative)lcd_cooperative_upload();
    }
    host_lcd_transfer_hook=0;
    if(cooperative){check(max_batch<=TK_LCD_BATCH_PIXELS,"every physical transfer stays within bounded batch");
        check(max_gap<=2024u,"clock service gap bounded under explicit transfer stress model");
        check(!tk_midi_clock.fault,"no accumulated clock or pending swing fault after bounded GLO painting");}
    return 0;
}
static void clock_reproduction(void)
{
    uint32_t amount,phase,old_stops,new_stops,total=0;
    for(amount=0;amount<=100;amount+=50){
        old_stops=new_stops=0;
        for(phase=0;phase<167;++phase){old_stops+=clock_case(amount,phase,0);new_stops+=clock_case(amount,phase,1);total+=2;}
        check(old_stops>0,"same GLO draw with unbounded transfers reproduces native stop");
        check(!new_stops,"bounded physical GLO batches preserve playback at every modeled phase");
        printf("MODEL swing=%u immediate_stops=%u cooperative_stops=%u phases=167\n",amount,old_stops,new_stops);
    }
    reset_case();transport_mode(&clock_transport,TK_MODE_SEQ,0);tk_clock_arm(0);tk_clock_event(0xFAu,0);
    tk_clock_event(0xF8u,0);fm1_ms=600;
    transport_tick(&clock_transport,&fixture,600);
    check(!clock_transport.running&&tk_midi_clock.fault,"real missing-clock guard remains intact");
    printf("MODEL %u actual-native transport/draw combinations; explicit synthetic transfer latency only\n",total);
}
int main(int argc,char **argv)
{
    pixels_and_queue(argc>1?argv[1]:0);mirror_ownership();clock_reproduction();
    printf("PASS LCD/clock: %u checks; pending/mirror/physical ownership, pixels and phase-varied service model\n",checks);
    return 0;
}
