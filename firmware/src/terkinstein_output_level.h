/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef TK_OUTPUT_LEVEL_H
#define TK_OUTPUT_LEVEL_H
#include <stdint.h>
/* Audio-owned 256-frame lookahead. Linear 4x below the peak limit, linked L/R.
 * 19600 is below the exact existing 48-kHz FIR worst-case L1 bound (54638 Q15):
 * (19600*54638+16384)>>15 = 32682. No sample-clip is used as the normal limiter.
 * A whole-block ramp finishes attenuation BEFORE the loud buffered block plays.
 * Recover at <=1/32 gain per block, avoiding repeated fast gain pumping.
 * Q12 products fit signed32 for the engine's Q15 bounded output. No allocation,
 * float, register access, per-sample division or new audio/client/voice owner.
 * This is 256/44100 seconds additional audio latency, NOT physical sync proof.
 */
#define TK_OUTPUT_FRAMES 256u
#define TK_OUTPUT_GAIN 16384
#define TK_OUTPUT_CEILING 19600
static int32_t tk_output_delay[2u*TK_OUTPUT_FRAMES];
static uint32_t tk_output_peak;
static int32_t tk_output_gain=TK_OUTPUT_GAIN;
static volatile uint32_t tk_output_sources[2],tk_output_busy;
static uint32_t tk_output_input_faults;
static void tk_output_reset(void){unsigned i;for(i=0;i<2u*TK_OUTPUT_FRAMES;i++)tk_output_delay[i]=0;tk_output_peak=0;tk_output_gain=TK_OUTPUT_GAIN;tk_output_sources[0]=tk_output_sources[1]=tk_output_busy=0;tk_output_input_faults=0;}
static int tk_output_pending(void){return tk_output_busy!=0u;}
static int tk_output_source_pending(unsigned source){return source<64u && ((tk_output_sources[source>>5]>>(source&31u))&1u);}
static int32_t tk_output_limit(uint32_t peak){uint32_t limit;if(!peak)return TK_OUTPUT_GAIN;limit=((uint32_t)TK_OUTPUT_CEILING*4096u)/peak;return limit<(uint32_t)TK_OUTPUT_GAIN?(int32_t)limit:TK_OUTPUT_GAIN;}
static void tk_output_process(int32_t*out,uint32_t n,uint32_t sources0,uint32_t sources1){
 uint32_t i,peak=0u;int32_t end,current_limit,old_limit,start=tk_output_gain;
 /* The FM1 DMA and simulator contract are fixed HALF_FRAMES=256. Refuse an
  * invalid caller rather than inventing a shorter delay/performing a partial block. */
 if(n!=TK_OUTPUT_FRAMES){for(i=0;i<2u*n;i++)out[i]=0;tk_output_input_faults++;return;}
 for(i=0;i<2u*n;i++){int32_t x=out[i];uint32_t a;
  if(x>32767){x=32767;out[i]=x;tk_output_input_faults++;}else if(x< -32767){x= -32767;out[i]=x;tk_output_input_faults++;}
  a=(uint32_t)(x<0?-x:x);if(a>peak)peak=a;}
 current_limit=tk_output_limit(peak);old_limit=tk_output_limit(tk_output_peak);
 end=current_limit<old_limit?current_limit:old_limit;
 if(end>start+128)end=start+128;
 for(i=0;i<n;i++){int32_t gain=start+((end-start)*(int32_t)(i+1u))/256;unsigned k=2u*i;
  int32_t l=out[k],rr=out[k+1u];out[k]=(tk_output_delay[k]*gain)/4096;out[k+1u]=(tk_output_delay[k+1u]*gain)/4096;
  tk_output_delay[k]=l;tk_output_delay[k+1u]=rr;}
 tk_output_peak=peak;tk_output_gain=end;
 tk_output_sources[0]=peak?sources0:0u;tk_output_sources[1]=peak?sources1:0u;tk_output_busy=peak!=0u;
}
#endif
