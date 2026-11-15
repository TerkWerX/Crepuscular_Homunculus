/* SPDX-License-Identifier: GPL-3.0-only */
#define TERKINSTEIN_HOST 1
#define main actual_firmware_main
#ifdef PALETTE_OLD_SOURCE
#include "../../firmware-legacy/firmware/src/terkinstein.c"
#else
#include "../firmware/src/terkinstein.c"
#endif
#undef main
int main(void){unsigned p,n,k;static tk_settings_t s;static tk_transport_t t;
 for(p=0;p<TK_PRESET_COUNT;p++){
  uint64_t hash=14695981039346656037ull,energy=0;uint32_t peak=0;
  tk_init();tk_metronome_init();tk_visual_init();tk_ready=1;memset(&t,0,sizeof t);settings_defaults(&s);tk_banks_init(&s,&t);
  preset_apply(&s,p);publish_params(&s,s.p.shade);
  tk_note_on(PRESETS[p].instrument==TK_INSTRUMENT_DRUM_KIT?36:60,100,1);
  for(n=0;n<180;n++){
   int32_t out[512]={0};if(n==90)tk_note_off(PRESETS[p].instrument==TK_INSTRUMENT_DRUM_KIT?36:60,1);
   app_audio(out,256);
   for(k=0;k<512;k++){uint32_t a=(uint32_t)(out[k]<0?-out[k]:out[k]);if(a>peak)peak=a;energy+=a;hash=(hash^(uint32_t)out[k])*1099511628211ull;}
  }
  if(!energy||peak>=32768u){fprintf(stderr,"FAIL audio p=%u peak=%u\n",p,peak);return 1;}
  printf("%u,%llu,%u,%llu\n",p,(unsigned long long)hash,peak,(unsigned long long)energy);
 }
 return 0;
}
