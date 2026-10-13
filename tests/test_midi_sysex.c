/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual USB SysEx collector, app inbox and TX reservation with fake USB/IRQ
 * helpers. Physical USB timing and electrical hardware are outside this test. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "terkinstein_protocol.h"
#define FELUCCA_OTA 1
#ifndef FELUCCA_REMOTE
#define FELUCCA_REMOTE 1
#endif
#define RING_PUBLISH() ((void)0)
static struct { uint8_t config, sysex[16], sx_len, sx_on, uboot_req, ota_req; } usb;
static uint8_t sx_frame[640];
static uint32_t sx_pos, sx_frame_len;
static volatile uint8_t sx_ready, sx_collect, sx_busy;
#define SXQ 64u
static uint32_t sx_out_q[SXQ];
static volatile uint32_t so_w, so_r;
#if FELUCCA_REMOTE
static volatile uint32_t mo_w, mo_r;
#endif
static uint8_t sx_open;
#if FELUCCA_REMOTE
static uint8_t usb_remote_frame[TK_REMOTE_DATA_MAX];
static volatile uint32_t usb_remote_length, usb_remote_generation;
static volatile uint8_t usb_remote_ready;
#endif
static uint32_t irq_calls, irq_restores;
static int reset_at_publish;
static uint32_t irq_save(void)
{
    irq_calls++;
#if FELUCCA_REMOTE
    if (reset_at_publish) {
        usb.config=0; usb_remote_generation++; usb_remote_ready=0;
        so_r=so_w; reset_at_publish=0;
    }
#endif
    return 23;
}
static void irq_restore(uint32_t flags) { if(flags!=23) exit(2); irq_restores++; }
#include "midi_sysex.c"
static uint32_t checks;
static void expect(int ok, const char *name)
{ checks++; if(!ok) { fprintf(stderr,"FAIL: %s\n",name); exit(1); } }
static void feed(const uint8_t *wire, uint32_t n)
{ uint32_t i;for(i=0;i<n;i++)sysex_byte(wire[i]); }
static void fresh(void)
{
    memset(&usb,0,sizeof usb);usb.config=1;sx_pos=sx_frame_len=0;
    sx_ready=sx_collect=sx_busy=sx_open=0;so_w=so_r=0;
#if FELUCCA_REMOTE
    mo_w=mo_r=0;
    usb_remote_reset();
#endif
}
static void updater_checks(void)
{
    static const uint8_t boot[]={0xF0,0x22,0x24,0x35,0x7D,0xF7};
    static const uint8_t update[]={0xF0,0x22,0x24,0x35,0x7F,0xF7};
    static const uint8_t identity[]={0xF0,0,0x59,0x11,0,0,0,0x7F,0xF7};
    uint32_t i;
    fresh();feed(boot,sizeof boot);expect(usb.uboot_req&&!usb.ota_req&&!sx_ready,"UBOOT exact soft key unchanged");
    fresh();feed(update,sizeof update);expect(usb.ota_req&&!usb.uboot_req&&!sx_ready,"OTA upgrade soft key unchanged");
    fresh();feed(identity,sizeof identity);expect(sx_ready&&sx_frame_len==7&&!memcmp(sx_frame,identity+1,7),"updater identity frame retained intact");
    /* A pending updater frame retains its own one-frame backpressure contract. */
    feed(boot,sizeof boot);expect(usb.uboot_req&&sx_ready&&sx_frame_len==7,"soft key remains reachable with pending frame");
    fresh();sysex_byte(0xF0);for(i=0;i<700;i++)sysex_byte(1);sysex_byte(0xF7);
    expect(!sx_ready&&!usb.ota_req&&!usb.uboot_req,"oversized SysEx ignored");
    feed(identity,sizeof identity);expect(sx_ready,"valid updater frame recovers after oversized SysEx");
    fresh();sysex_byte(0xF0);sysex_byte(0);sysex_byte(0x59);sysex_byte(0x90);sysex_byte(60);sysex_byte(0xF7);
    expect(!sx_ready&&!usb.sx_on,"channel status aborts partial SysEx");
    fresh();sysex_byte(0xF0);sysex_byte(3);feed(identity,sizeof identity);
    expect(sx_ready&&sx_frame_len==7,"nested F0 replaces unfinished collector");
}
#if FELUCCA_REMOTE
static void remote_checks(void)
{
    uint8_t wire[TK_REMOTE_WIRE_MAX], second[TK_REMOTE_WIRE_MAX], payload[80], huge[96];
    uint32_t n,n2,i,j,k,pk,cin,count,old_write,generation; const uint8_t *input;uint32_t length;
    tk_remote_message_t message;
    fresh(); n=tk_remote_encode(wire,sizeof wire,0,TK_REMOTE_TRANSPORT,7,(const uint8_t*)"\x01",1);
    feed(wire,n);expect(!sx_ready&&!usb.ota_req&&!usb.uboot_req,"remote command cannot enter updater or soft keys");
    expect(usb_remote_get(&input,&length)&&length==n-2u&&!memcmp(input,wire+1,length),"actual collector publishes bounded app inbox");
    n2=tk_remote_encode(second,sizeof second,0,TK_REMOTE_TRANSPORT,8,(const uint8_t*)"\x00",1);
    feed(second,n2);expect(usb_remote_get(&input,&length)&&input[6]==7,"busy app inbox does not overwrite first request");
    usb_remote_done();feed(second,n2);expect(usb_remote_get(&input,&length)&&input[6]==8,"retry received after inbox release");
    usb_remote_done();
    wire[4]=2;wire[n-2u]=(uint8_t)((wire[n-2u]+127u)&127u);
    feed(wire,n);expect(usb_remote_get(&input,&length)&&tk_remote_decode(input,length,&message)==-2,"unsupported app version stays isolated");
    usb_remote_done();wire[4]=1;wire[n-2u]=(uint8_t)((wire[n-2u]+1u)&127u);
    fresh();for(i=0;i<n;i++) { sysex_byte(wire[i]);if(i>0&&i+1<n)sysex_byte(0xF8); }
    expect(usb_remote_get(&input,&length)&&!memcmp(input,wire+1,length),"realtime interleave preserves application frame");
    usb_remote_done();
    fresh();memset(huge,1,sizeof huge);huge[0]=0xF0;huge[1]=0x7D;huge[2]=0x54;huge[3]=0x4B;huge[95]=0xF7;
    feed(huge,sizeof huge);expect(!usb_remote_ready&&!sx_ready,"oversized recognized app frame cannot leak to updater");
    feed(wire,n);expect(usb_remote_ready,"app framing recovers after oversized frame");
    generation=usb_remote_generation;usb_remote_reset();
    expect(!usb_remote_ready&&usb_remote_generation==generation+1u,"USB reset flushes inbox and advances generation");
    /* The application and updater inboxes are independent after collection. */
    fresh();feed(wire,n);
    { static const uint8_t foreign[]={0xF0,0x7D,0x12,0x23,0xF7}; feed(foreign,sizeof foreign); }
    expect(usb_remote_ready&&sx_ready&&sx_frame_len==3,"foreign SysEx retains original updater path alongside app inbox");
    /* Real TX packetization/reservation across every supported frame length. */
    for(i=0;i<=80u;i++) {
        fresh();for(j=0;j<i;j++)payload[j]=(uint8_t)((j+i)&127u);
        n=tk_remote_encode(wire,sizeof wire,1,TK_REMOTE_STATE,(uint8_t)i,payload,i);
        so_w=so_r=0xFFFFFFF0u;old_write=so_w;count=tk_remote_packet_count(n);
        expect(usb_remote_send(wire,n)&&so_w-old_write==count,"nonblocking whole-frame reservation survives ring index wrap");
        k=0;
        for(j=0;j<count;j++) {
            uint32_t bytes;
            pk=sx_out_q[(old_write+j)%SXQ];cin=pk&15u;bytes=cin==4u||cin==7u?3u:cin-4u;
            expect((j+1u<count)==(cin==4u),"SysEx TX no premature F7 packet");
            while(bytes--) { expect((uint8_t)(pk>>8)==wire[k++],"actual TX ring reconstructs exact bytes");pk>>=8; }
        }
        expect(k==n,"no missing or extra TX bytes");
        old_write=so_w;so_r=so_w-(SXQ-count+1u);
        expect(!usb_remote_send(wire,n)&&so_w==old_write,"insufficient room publishes no partial frame");
        so_r=so_w;sx_busy=1;expect(!usb_remote_send(wire,n),"active updater transmission takes priority");sx_busy=0;
        sx_open=1;expect(!usb_remote_send(wire,n),"truncated updater frame must close before application traffic");sx_open=0;
        usb.config=0;expect(!usb_remote_send(wire,n),"disconnected USB never queues stale feedback");usb.config=1;
        old_write=so_w;reset_at_publish=1;
        expect(!usb_remote_send(wire,n)&&so_w==old_write,"USB reset during reservation cancels publication");
    }
    fresh();n=tk_remote_encode(wire,sizeof wire,1,TK_REMOTE_SCREEN_TILE,1,payload,80);
    mo_w=1;expect(!usb_remote_screen_send(wire,n)&&so_w==0,"screen yields to pending generated note");
    expect(usb_remote_send(wire,n),"ordinary control reply still uses original reservation path");
    mo_r=mo_w;old_write=so_w;
    expect(!usb_remote_screen_send(wire,n)&&so_w==old_write,"screen cannot queue behind an ordinary reply");
    so_r=so_w;
    expect(usb_remote_screen_send(wire,n),"screen admits one complete frame after queues drain");
    old_write=so_w;
    expect(!usb_remote_screen_send(wire,n)&&so_w==old_write,"screen cannot maintain a continuous SysEx backlog");
    so_r=so_w;mo_w++;
    expect(!usb_remote_screen_send(wire,n)&&so_w==old_write,"note arriving after a screen frame gets next admission priority");
    mo_r=mo_w;
    expect(usb_remote_screen_send(wire,n),"screen resumes after musical event delivery");
    expect(irq_calls==irq_restores,"all guarded publication paths restore IRQ state");
}
#endif
int main(void)
{
    updater_checks();
#if FELUCCA_REMOTE
    remote_checks();
#endif
    printf("PASS USB SysEx collector (remote=%d): %u checks\n",FELUCCA_REMOTE,checks);
    return 0;
}
