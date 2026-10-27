/* SPDX-License-Identifier: GPL-3.0-only */
/* Real updater handshake over memory-only SysEx hooks. No hardware/backend. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../firmware/src/terkinstein_identity.h"
#ifndef FELUCCA_LOADER
static uint8_t recovery_active;
#define OTA_IDENTITY (recovery_active ? "FM-1_000" : FELUCCA_ID)
#endif
#include "../firmware/src/ota.c"

static const uint8_t query[] = {0x00,0x32,0x45,0x00,0x00,0x00,0x40,0x7F};
static uint8_t response[64];
static uint32_t response_n, pending, replies;
static int ota_wire_send(const uint8_t *p, uint32_t n)
{
    if (n > sizeof response) abort();
    memcpy(response,p,n); response_n=n; replies++; return 0;
}
static int ota_frame_get(const uint8_t **p,uint32_t *n)
{
    if (!pending) return 0;
    *p=query; *n=sizeof query; return 1;
}
static void ota_frame_done(void) { pending=0; }
static uint32_t ota_now_ms(void) { return 0; }
static void ota_idle(void) { abort(); }
static int ota_erase(uint32_t o) { (void)o; abort(); return -1; }
static int ota_prog(uint32_t o,const void *p,uint32_t n) { (void)o;(void)p;(void)n;abort();return -1; }
static int ota_fread(uint32_t o,void *p,uint32_t n) { (void)o;(void)p;(void)n;abort();return -1; }
static void ota_show(uint32_t s,int32_t c) { (void)s;(void)c;abort(); }
static void ota_commit(const uint8_t *p) { (void)p;abort(); }

static void check(const char *expected)
{
    uint8_t decoded[64]; uint32_t i,n,sum=0;
    pending=1; replies=0; response_n=0;
    ota_service(); ota_service();
    if (replies!=1 || pending || response_n<2 || response[0]!=0xF0 || response[response_n-1]!=0xF7) abort();
    n=ota_unpack7(response+1,response_n-2,decoded,sizeof decoded);
    if (n!=34 || memcmp(decoded,"\0\x59\x11\x1B\0\0",6) || strlen(expected)>26) abort();
    for (i=0;i<27;i++) {
        if (decoded[6+i]!=(i<strlen(expected)?(uint8_t)expected[i]:0)) abort();
        sum+=decoded[6+i];
    }
    if (decoded[33]!=(uint8_t)~sum) abort();
    printf("WIRE "); for (i=0;i<response_n;i++) printf("%02X",response[i]); puts("");
}
int main(int argc,char **argv)
{
#ifdef FELUCCA_LOADER
    check(argc>1?argv[1]:"ota-FM-1_986");
#else
    check(argc>1?argv[1]:"FM-1_986");
    recovery_active=1; check("FM-1_000");
    recovery_active=0; check(argc>1?argv[1]:"FM-1_986");
#endif
    puts("PASS updater identity, zero padding, checksum, idle handshake and recovery; no writes");
    return 0;
}
