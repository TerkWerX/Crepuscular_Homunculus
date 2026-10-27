/* SPDX-License-Identifier: GPL-3.0-only */
/* Main-owned song grid. Integer-only; no audio state or note ownership. */
#ifndef TERKINSTEIN_SWING_H
#define TERKINSTEIN_SWING_H
#define TK_SWING_MAX 100u
/* Amount0 is50:50; amount100 is75:25. A pair always lasts2*period. */
static uint32_t tk_swing_shift(uint32_t period,uint32_t amount)
{
    return period * (amount > TK_SWING_MAX ? TK_SWING_MAX : amount) / 200u;
}
static uint32_t tk_swing_interval(uint32_t period,uint32_t amount,uint32_t step)
{
    uint32_t shift=tk_swing_shift(period,amount);
    return step&1u ? period-shift : period+shift;
}
/* Stored quarter-step note values follow the warped grid. Whole two-step
 * pairs stay exact; fractional gates interpolate only their final interval. */
static uint32_t tk_swing_gate(uint32_t period,uint32_t amount,uint32_t step,uint32_t q4)
{
    uint32_t whole=q4/4u,rem=q4%4u,result=whole*period;
    if (!amount) return period*q4/4u;
    if (whole&1u) result=step&1u ? result-tk_swing_shift(period,amount) : result+tk_swing_shift(period,amount);
    return result+tk_swing_interval(period,amount,step+whole)*rem/4u;
}
/* Inverse grid used only for REC durations;1024 units per sixteenth. */
static uint32_t tk_swing_position_q8(uint32_t elapsed,uint32_t period,uint32_t amount,uint32_t parity)
{
    uint32_t pair=period*2u,cycles,rest,first;
    if (!period) return 0u;
    cycles=elapsed/pair;rest=elapsed%pair;
    first=tk_swing_interval(period,amount,parity);
    return cycles*2048u+(rest<first ? rest*1024u/first :
        1024u+(rest-first)*1024u/(pair-first));
}
#endif
