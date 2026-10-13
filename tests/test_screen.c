/* SPDX-License-Identifier: GPL-3.0-only */
/* Portable actual-LCD capture/stream checks; no target timing qualification. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _MSC_VER
#define __attribute__(x)
#endif
#include "terkinstein_screen.h"
static unsigned checks, sends;
static int busy;
static uint8_t sent[TK_REMOTE_WIRE_MAX];
static uint32_t sent_n;
static void expect(int ok, const char *message)
{ checks++; if (!ok) { fprintf(stderr,"FAIL %s\n",message); exit(1); } }
static int send_frame(const uint8_t *p, uint32_t n)
{ if (busy) return 0; memcpy(sent,p,n); sent_n=n; sends++; return 1; }
static uint32_t unpack(uint8_t *out, const uint8_t *data, uint32_t n)
{
    uint32_t at=0,count=0;
    while(at<n) {
        uint32_t i,mask=data[at++],amount=n-at<7u?n-at:7u;
        for(i=0;i<amount;i++) out[count++]=(uint8_t)(data[at++]|((mask>>i&1u)<<7));
    }
    return count;
}
static void reset(void)
{ memset(&tk_screen,0,sizeof tk_screen); memset(&tk_screen_pool,0,sizeof tk_screen_pool); sends=sent_n=0; busy=0; }
static void check_palette_and_burst(void)
{
    uint8_t decoded[512];
    uint32_t colors, pixel, row, count, bits, base;
    uint16_t color;
    for (colors = 1; colors <= 17; colors++) {
        reset();
        for (pixel = 0; pixel < 256u; pixel++) {
            color = (uint16_t)(((pixel % colors) * 3711u) ^ 0xAC71u);
            tk_screen_pool.pixels[pixel / 16u * 240u + pixel % 16u] = color;
        }
        tk_screen_subscribe(3, 100, 0); tk_screen_prepare(0);
        if (colors == 1) {
            expect(tk_screen.encoding == 1 && tk_screen.length == 6, "uniform RLE beats palette overhead");
        } else if (colors <= 16) {
            expect(tk_screen.encoding == 2, "advanced low-color noisy tile selects lossless palette");
            count = tk_screen_pool.rle[0]; bits = count <= 2 ? 1u : count <= 4 ? 2u : 4u;
            base = 1u + count * 2u;
            expect(count == colors && tk_screen.length == base + 32u * bits, "palette has exact derived index length");
            for (pixel = 0; pixel < 256u; pixel++) {
                uint32_t index = tk_screen_pool.rle[base + (pixel * bits >> 3)] >> (pixel * bits & 7u) & ((1u << bits) - 1u);
                expect(index < count, "palette indices stay within distinct colors");
                decoded[pixel * 2u] = tk_screen_pool.rle[1u + index * 2u];
                decoded[pixel * 2u + 1u] = tk_screen_pool.rle[2u + index * 2u];
            }
            expect(!memcmp(decoded, tk_screen_pool.raw, 512), "palette reconstruction preserves every RGB565 byte");
        } else {
            expect(tk_screen.encoding == 0 && tk_screen.length == 512, "more than16 colors keeps bounded raw fallback");
        }
        tk_screen_subscribe(1, 101, 0); tk_screen_prepare(0);
        expect(!tk_screen.advanced && tk_screen.encoding < 2, "legacy subscribe never receives palette encoding");
        for (row = 0; row < 225; row++) expect(tk_screen.dirty[row] == (row != 0), "mode switch refreshes all remaining tiles");
    }
    reset();
    for (pixel = 0; pixel < 256u; pixel++) tk_screen_pool.pixels[pixel / 16u * 240u + pixel % 16u] = (uint16_t)(pixel % 16u * 3711u);
    tk_screen_subscribe(3, 100, 0); tk_screen_prepare(0);
    expect(tk_screen.advanced && tk_screen.length == 161, "advanced subscribe enables compact161-byte tile");
    busy = 1;
    expect(!tk_screen_pump(100, 0, 1, send_frame) && !tk_screen.burst_count && !tk_screen.offset,
        "output backpressure consumes neither burst budget nor snapshot offset");
    busy = 0;
    expect(!tk_screen_pump(100, 0, 0, send_frame) && !tk_screen.burst_count, "input/control priority also preserves burst budget");
    expect(tk_screen_pump(100, 0, 1, send_frame) && tk_screen.offset == 60, "first advanced admission");
    count = tk_screen.serial;
    tk_screen_subscribe(4, 100, 0);
    expect(tk_screen.offset == 60 && tk_screen.serial == count && tk_screen.transmitting, "advanced heartbeat retains immutable partial tile");
    expect(tk_screen_pump(100, 0, 1, send_frame) && tk_screen.offset == 120, "second advanced admission");
    expect(tk_screen_pump(100, 0, 1, send_frame) && !tk_screen.transmitting, "third admission completes exact tile");
    expect(!tk_screen_pump(100, 0, 1, send_frame) && sends == 3, "advanced hard budget is three successful frames per millisecond");
    tk_screen_subscribe(3, 100, 0);
    expect(!tk_screen_pump(100, 0, 1, send_frame), "repeat full refresh cannot bypass advanced rate budget");
    expect(tk_screen_pump(101, 0, 1, send_frame), "next millisecond restores bounded burst allowance");
    tk_screen_subscribe(2, 102, 0);
    expect(tk_screen_pump(102, 0, 1, send_frame) && !tk_screen.advanced && tk_screen.encoding < 2, "legacy refresh restores old codec and pacing");
    expect(!tk_screen_pump(105, 0, 1, send_frame) && tk_screen_pump(106, 0, 1, send_frame), "legacy retains exact four-millisecond cap");
}
static void check_compact_profile(void)
{
    static uint16_t source[240u * 240u];
    uint32_t tile, i, x, y, serial, offset, n;
    uint8_t snapshot[TK_SCREEN_COMPACT_BYTES], decoded[TK_SCREEN_COMPACT_BYTES];
    static const uint16_t input[4] = {0x0000u, 0x7936u, 0xD33Fu, 0xA7EFu};
    tk_remote_message_t message;
    reset(); tk_screen_set_home(1u);
    tk_screen_subscribe_profile(3u, 1u, 100u, 0u);
    expect(tk_screen.compact && tk_screen.home && tk_screen.advanced,
           "compact profile requires explicit advanced opt-in and HOME context");
    tk_screen_capture_fill(0u, 0u, 240u, 240u, 0xFFFFu);
    memcpy(source, tk_screen_pool.pixels, sizeof source);
    for (tile = 0; tile < 225u; tile++) {
        x = tile % 15u; y = tile / 15u;
        tk_screen_prepare(tile);
        expect((tk_screen.encoding == 3u) == (x >= 1u && x <= 13u && y >= 5u && y <= 12u),
               "only wholly contained HOME viewport tiles are coarse");
        if (tk_screen.encoding == 3u)
            expect(tk_screen.length == 17u && tk_screen_pool.rle[0] == 1u,
                   "compact samples have fixed exact version and length");
    }
    expect(!memcmp(source, tk_screen_pool.pixels, sizeof source),
           "coarse preparation never changes captured physical LCD pixels");
    tile = 5u * 15u + 1u;
    for (i = 0; i < 64u; i++) {
        x = 16u + (i % 8u) * 2u; y = 80u + (i / 8u) * 2u;
        tk_screen_capture_fill(x, y, 2u, 2u, input[i % 4u]);
    }
    tk_screen_prepare(tile);
    for (i = 0; i < 64u; i++)
        expect((tk_screen_pool.rle[1u + i / 4u] >> ((i % 4u) * 2u) & 3u) == i % 4u,
               "fixed palette samples pack row-major and LSB-first");
    tk_screen_capture_fill(16u, 80u, 2u, 2u, input[2]);
    tk_screen_capture_fill(17u, 81u, 1u, 1u, input[3]);
    tk_screen_prepare(tile);
    expect((tk_screen_pool.rle[1] & 3u) == 3u, "one green waveform pixel wins a bright-purple block");
    tk_screen_capture_fill(18u, 80u, 2u, 2u, input[1]);
    tk_screen_capture_fill(19u, 81u, 1u, 1u, input[2]);
    tk_screen_prepare(tile);
    expect((tk_screen_pool.rle[1] >> 2 & 3u) == 2u, "bright thorns win middle-purple trails");
    memcpy(snapshot, tk_screen_pool.rle, sizeof snapshot);
    busy = 1;
    expect(!tk_screen_pump(101u, 0u, 1, send_frame) && !tk_screen.offset,
           "compact backpressure preserves immutable payload and burst budget");
    tk_screen_capture_fill(16u, 80u, 16u, 16u, input[0]);
    expect(!memcmp(snapshot, tk_screen_pool.rle, sizeof snapshot) && tk_screen.dirty[tile],
           "new HOME writes redirty without mutating an in-flight coarse snapshot");
    busy = 0;
    expect(tk_screen_pump(101u, 0u, 1, send_frame), "compact tile is sent in one bounded chunk");
    expect(tk_remote_decode(sent + 1u, sent_n - 2u, &message) == 2,
           "compact chunk uses existing checksummed screen envelope");
    n = unpack(decoded, message.payload + 11u, message.length - 11u);
    expect(message.payload[6] == 3u && message.payload[7] == 17u && !message.payload[8] &&
           !message.payload[9] && !message.payload[10] && n == 17u && !memcmp(snapshot, decoded, 17u),
           "actual emitted compact payload has exact encoding, length, offset and bytes");
    serial = tk_screen.serial;
    memset(tk_screen.dirty, 0, sizeof tk_screen.dirty);
    tk_screen.cursor = tile; tk_screen.dirty[tile] = 1u;
    expect(tk_screen_pump(102u, 0u, 1, send_frame), "redirtied compact tile sends the latest captured image");
    expect(tk_screen.serial == ((serial + 1u) & 16383u) && !(tk_screen_pool.rle[1] & 3u),
           "compact serial advances monotonically and replacement sees latest pixels");
    /* A partial boundary tile still uses the lossless codec. */
    for (i = 0; i < 256u; i++)
        tk_screen_pool.pixels[i / 16u * 240u + i % 16u] = (uint16_t)(i * 257u);
    tk_screen_prepare(0u); serial = tk_screen.serial;
    expect(tk_screen_pump(103u, 0u, 1, send_frame) && tk_screen.transmitting && tk_screen.offset == 60u,
           "full boundary tile retains ordinary immutable multi-chunk transfer");
    offset = tk_screen.offset;
    tk_screen_subscribe_profile(4u, 1u, 104u, 0u);
    expect(tk_screen.transmitting && tk_screen.offset == offset && tk_screen.serial == serial,
           "compact heartbeat retains in-flight lossless snapshot");
    tk_screen_set_home(0u);
    expect(!tk_screen.transmitting && !tk_screen.offset && tk_screen.serial == serial,
           "page/splash exit cancels partial coarse-session transfer without resetting serial");
    for (i = 0; i < 225u; i++) expect(tk_screen.dirty[i], "context change restores every full-resolution tile");
    tk_screen_prepare(tile);
    expect(tk_screen.encoding < 3u, "every non-HOME page retains exact pixels despite compact subscription");
    tk_screen_set_home(1u); tk_screen_prepare(tile);
    expect(tk_screen.encoding == 3u, "returning HOME resumes bounded coarsening");
    tk_screen_subscribe(4u, 105u, 0u);
    expect(!tk_screen.compact && !tk_screen.transmitting, "legacy one-byte heartbeat exits compact mode");
    tk_screen_prepare(tile);
    expect(tk_screen.encoding < 3u, "old clients can never receive coarse encoding");
    tk_screen_subscribe_profile(3u, 1u, 200u, 0u);
    expect(!tk_screen_pump(5200u, 0u, 1, send_frame) && !tk_screen.enabled,
           "compact mode retains exact five-second abandonment lease");
    tk_screen_subscribe_profile(3u, 1u, 600u, 0u);
    serial = tk_screen.serial;
    expect(!tk_screen_pump(601u, 1u, 1, send_frame) && !tk_screen.enabled && !tk_screen.compact,
           "USB generation reset clears negotiated compact profile");
    expect(tk_screen.serial == serial, "generation reset never rolls tile serial backward");
    tk_screen_subscribe(3u, 602u, 1u); tk_screen_prepare(tile);
    expect(tk_screen.encoding < 3u, "reconnected legacy client remains lossless");
}
int main(void)
{
    uint32_t i,j,n,offset,decoded_n=0;
    uint8_t raw[512],packed[600],decoded[512],before[TK_REMOTE_WIRE_MAX];
    uint16_t swap[8]={0x3412,0xCDAB,0xFFFF,0x0100,0x7856,0xEFBE,0xFFFF,0x0100};
    tk_remote_message_t message;
    reset();
    tk_screen_capture_fill(230,235,UINT32_MAX,UINT32_MAX,0xABCD);
    for(j=0;j<240;j++) for(i=0;i<240;i++)
        expect(tk_screen_pool.pixels[j*240+i]==(i>=230&&j>=235?0xABCD:0),"fill clips without unsigned overflow");
    tk_screen_capture_blit(238,0,4,2,swap);
    expect(tk_screen_pool.pixels[238]==0x1234&&tk_screen_pool.pixels[239]==0xABCD&&
        tk_screen_pool.pixels[240+238]==0x5678&&tk_screen_pool.pixels[240+239]==0xBEEF,"blit endian and original clipped stride");
    memset(tk_screen.dirty,0,sizeof tk_screen.dirty);
    tk_screen_capture_blit(238,0,4,2,swap);
    expect(!tk_screen.dirty[14],"identical blit does not dirty tile");
    tk_screen_capture_fill(238,0,2,1,0x1234);
    expect(tk_screen.dirty[14],"changed pixels dirty their real tile");
    expect(!tk_screen_pump(10,0,1,send_frame)&&!sends,"disabled stream emits nothing");
    for(n=1;n<=60;n++) {
        for(i=0;i<n;i++) raw[i]=(uint8_t)(i*173u+n);
        j=tk_screen_pack7(packed,raw,n);
        expect(j==n+(n+6u)/7u&&j<=69,"bounded seven-bit chunk size");
        expect(unpack(decoded,packed,j)==n&&!memcmp(raw,decoded,n),"seven-bit binary roundtrip");
        for(i=0;i<j;i++) expect(packed[i]<128,"wire is seven bit clean");
    }
    reset(); tk_screen_capture_fill(0,0,16,16,0xF81F);
    tk_screen_prepare(0);
    expect(tk_screen.encoding==1&&tk_screen.length==6&&tk_screen_pool.rle[0]==255&&tk_screen_pool.rle[3]==1,"uniform256 run split and RLE");
    for(i=0;i<256;i++) tk_screen_pool.pixels[(i/16)*240+i%16]=(uint16_t)(i*257u);
    tk_screen_prepare(0);
    expect(tk_screen.encoding==0&&tk_screen.length==512,"incompressible tile uses bounded raw snapshot");
    for(i=0;i<512;i++) raw[i]=tk_screen_pool.raw[i];
    tk_screen.enabled=1; tk_screen.heartbeat=0;
    busy=1; offset=tk_screen.offset;
    expect(!tk_screen_pump(10,0,1,send_frame)&&tk_screen.offset==offset&&!tk_screen.sent,"busy sender preserves snapshot/offset");
    busy=0;
    expect(!tk_screen_pump(10,0,0,send_frame)&&!sends,"control/deadline priority blocks mirror");
    expect(tk_screen_pump(10,0,1,send_frame),"first chunk sent");
    memcpy(before,sent,sent_n);
    expect(tk_remote_decode(sent+1,sent_n-2,&message)==2&&message.command==0x0B&&message.payload[0]==0,"separate asynchronous reply command");
    expect(!tk_screen_pump(13,0,1,send_frame)&&sends==1,"four ms cap");
    tk_screen_capture_fill(0,0,1,1,0x00FF);
    expect(tk_screen.dirty[0]&&tk_screen_pool.raw[0]==raw[0],"drawing during send preserves immutable snapshot and redirties");
    for(i=0;i<9;i++) {
        if(i) expect(tk_screen_pump(10+i*4,0,1,send_frame),"next raw chunk sent");
        expect(tk_remote_decode(sent+1,sent_n-2,&message)==2,"each chunk checksum/frame valid");
        expect(message.length<=80&&message.payload[6]==0&&message.payload[7]==0&&message.payload[8]==4,"raw encoding total512");
        offset=message.payload[9]|(uint32_t)message.payload[10]<<7;
        expect(offset==decoded_n,"contiguous snapshot offsets");
        n=unpack(decoded+decoded_n,message.payload+11,message.length-11); decoded_n+=n;
    }
    expect(decoded_n==512&&!memcmp(raw,decoded,512)&&!tk_screen.transmitting,"full raw tile roundtrip unchanged by new draw");
    tk_screen.cursor=0;
    expect(tk_screen_pump(50,0,1,send_frame)&&tk_screen_pool.raw[0]==0xFF&&tk_screen_pool.raw[1]==0,"redirtied tile sends latest pixels");
    expect(tk_screen_subscribe(1,100,0)==1&&tk_screen.enabled,"heartbeat enables/retains lease");
    expect(!tk_screen_pump(5100,0,1,send_frame)&&!tk_screen.enabled,"lease expires exactly after5s");
    expect(tk_screen_subscribe(2,5200,0)==1&&!tk_screen.transmitting,"explicit refresh abandons stale partial snapshot");
    for(i=0;i<225;i++) expect(tk_screen.dirty[i],"explicit refresh includes all225 tiles");
    tk_screen_subscribe(1,UINT32_MAX-1000u,0);
    expect(tk_screen_pump(1000,0,1,send_frame)&&tk_screen.enabled,"lease survives clock wrap");
    expect(!tk_screen_pump(1004,1,1,send_frame)&&!tk_screen.enabled,"USB generation change stops prior stream");
    tk_screen_subscribe(1,2000,1);
    expect(tk_screen_subscribe(0,2001,1)==0&&!tk_screen_pump(2005,1,1,send_frame),"explicit unsubscribe stops immediately");
    check_palette_and_burst(); check_compact_profile();
    puts("PASS main-owned LCD capture, exact raw/RLE/palette, legacy compatibility, bounded advanced burst, priority/backpressure, lease/wrap/disconnect");
    printf("%u screen checks\n",checks);
    return 0;
}
