/* SPDX-License-Identifier: GPL-3.0-only */
/* Two-slot song journal. Main/quiet only; never called by the audio ISR.
 * Every sector's last 256 bytes remain erased so neither the SPL nor the
 * inherited updater can mistake song bytes for an update record. */
#ifndef TERKINSTEIN_STORE_H
#define TERKINSTEIN_STORE_H
#include <stdint.h>
#define TK_STORE_BASE 0x000C0000u
#define TK_STORE_SLOT_BYTES 0x8000u
#define TK_STORE_END (TK_STORE_BASE + 2u * TK_STORE_SLOT_BYTES)
#define TK_STORE_PAGE_DATA 0xF00u
#define TK_STORE_LOGICAL (8u * TK_STORE_PAGE_DATA)
#define TK_STORE_HEADER 32u
#define TK_STORE_MAX (TK_STORE_LOGICAL - TK_STORE_HEADER)
#define TK_STORE_MAGIC 0x324A4B54u
typedef int (*tk_store_pull_t)(void *, uint32_t, uint8_t *, uint32_t);
static int tk_store_flash_read(uint32_t, void *, uint32_t);
static int tk_store_flash_write(uint32_t, const uint8_t *, uint32_t);
static int tk_store_flash_erase(uint32_t);
static void tk_store_idle(void);
static uint32_t tk_store_crc(uint32_t c, const uint8_t *p, uint32_t n)
{
    uint32_t i;
    while(n--) { c ^= *p++; for(i=0;i<8u;i++) c=c&1u?(c>>1)^0xEDB88320u:c>>1; }
    return c;
}
static uint32_t tk_store_u32(const uint8_t *p)
{ return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static void tk_store_put32(uint8_t *p,uint32_t value)
{ p[0]=(uint8_t)value;p[1]=(uint8_t)(value>>8);p[2]=(uint8_t)(value>>16);p[3]=(uint8_t)(value>>24); }
static int tk_store_slice(uint32_t slot,uint32_t logical,void *data,uint32_t n,int writing)
{
    uint8_t *p=(uint8_t *)data;
    if(slot>1u||logical>TK_STORE_LOGICAL||n>TK_STORE_LOGICAL-logical||(!p&&n))return -1;
    while(n) {
        uint32_t within=logical%TK_STORE_PAGE_DATA,chunk=TK_STORE_PAGE_DATA-within;
        uint32_t off=TK_STORE_BASE+slot*TK_STORE_SLOT_BYTES+(logical/TK_STORE_PAGE_DATA)*0x1000u+within;
        int rc;
        if(chunk>n)chunk=n;
        rc=writing?tk_store_flash_write(off,p,chunk):tk_store_flash_read(off,p,chunk);
        if(rc)return rc;
        logical+=chunk;p+=chunk;n-=chunk;
    }
    return 0;
}
static int tk_store_probe(uint32_t slot,uint32_t *length,uint32_t *generation)
{
    uint8_t h[TK_STORE_HEADER],buf[256];uint32_t at,n,c=0xFFFFFFFFu;
    if(tk_store_slice(slot,0,h,sizeof h,0)||tk_store_u32(h)!=TK_STORE_MAGIC||tk_store_u32(h+4)!=1u)return -1;
    n=tk_store_u32(h+8);
    if(!n||n>TK_STORE_MAX||tk_store_u32(h+20)!=(n^0xFFFFFFFFu)||tk_store_u32(h+24)!=0u
        ||(tk_store_crc(0xFFFFFFFFu,h,28u)^0xFFFFFFFFu)!=tk_store_u32(h+28))return -1;
    for(at=0;at<n;) {
        uint32_t chunk=n-at;if(chunk>sizeof buf)chunk=sizeof buf;
        if(tk_store_slice(slot,TK_STORE_HEADER+at,buf,chunk,0))return -1;
        c=tk_store_crc(c,buf,chunk);at+=chunk;tk_store_idle();
    }
    if((c^0xFFFFFFFFu)!=tk_store_u32(h+16))return -1;
    *length=n;*generation=tk_store_u32(h+12);return 0;
}
static int tk_store_latest(uint32_t *slot,uint32_t *length,uint32_t *generation)
{
    uint32_t a=0,b=0,ga=0,gb=0;int va=!tk_store_probe(0u,&a,&ga),vb=!tk_store_probe(1u,&b,&gb);
    if(!va&&!vb)return -1;
    *slot=vb&&(!va||(int32_t)(gb-ga)>0)?1u:0u;
    *length=*slot?b:a;*generation=*slot?gb:ga;return 0;
}
static int tk_store_save(uint32_t length,tk_store_pull_t pull,void *context)
{
    uint8_t h[TK_STORE_HEADER],buf[256];uint32_t slot=1u,old_length=0,generation=0,at,c=0xFFFFFFFFu;
    uint32_t verified_length,verified_generation;
    if(!pull||!length||length>TK_STORE_MAX)return -1;
    /* Validate the complete serialization before erasing any slot. */
    for(at=0;at<length;) {
        uint32_t chunk=length-at;if(chunk>sizeof buf)chunk=sizeof buf;
        if(pull(context,at,buf,chunk))return -1;
        c=tk_store_crc(c,buf,chunk);at+=chunk;
    }
    if(tk_store_latest(&slot,&old_length,&generation))slot=1u;
    slot^=1u;generation++;
    for(at=0;at<TK_STORE_SLOT_BYTES;at+=0x1000u) {
        if(tk_store_flash_erase(TK_STORE_BASE+slot*TK_STORE_SLOT_BYTES+at))return -1;
        tk_store_idle();
    }
    for(at=0;at<length;) {
        uint32_t chunk=length-at;if(chunk>sizeof buf)chunk=sizeof buf;
        if(pull(context,at,buf,chunk)||tk_store_slice(slot,TK_STORE_HEADER+at,buf,chunk,1))return -1;
        at+=chunk;tk_store_idle();
    }
    /* Read back before publishing a valid header. */
    { uint32_t check_crc=0xFFFFFFFFu;
      for(at=0;at<length;) {
        uint32_t chunk=length-at;if(chunk>sizeof buf)chunk=sizeof buf;
        if(tk_store_slice(slot,TK_STORE_HEADER+at,buf,chunk,0))return -1;
        check_crc=tk_store_crc(check_crc,buf,chunk);at+=chunk;tk_store_idle();
      }
      if(check_crc!=c)return -1;
    }
    for(at=0;at<sizeof h;at++)h[at]=0;
    tk_store_put32(h,TK_STORE_MAGIC);tk_store_put32(h+4,1u);tk_store_put32(h+8,length);
    tk_store_put32(h+12,generation);tk_store_put32(h+16,c^0xFFFFFFFFu);
    tk_store_put32(h+20,length^0xFFFFFFFFu);tk_store_put32(h+28,tk_store_crc(0xFFFFFFFFu,h,28u)^0xFFFFFFFFu);
    if(tk_store_slice(slot,0,h,sizeof h,1)||tk_store_probe(slot,&verified_length,&verified_generation))return -1;
    return verified_length==length&&verified_generation==generation?0:-1;
}
#endif
