# Performance and allocation limits

Crepuscular Homunculus has eight shared synthesis voices, with at most four FM
voices. Captured tracks, live notes, pedal-held notes and release tails share them.
The number of stored sounds or tracks does not increase simultaneous voice capacity.
Display, USB and flash stay outside the synthesis callback; main services LCD work
in bounded batches and flash SAVE waits for complete silence.

## Exact0.26.18 and0.26.19 target allocation (unchanged)

| Allocation | Used | Limit |
| --- | ---: | ---: |
| Application image | 220,032 bytes | 581,564 bytes |
| Static RAM | 81,904 bytes | 98,304 bytes |
| Pool | 319,876 bytes | 344,064 bytes |
| RAM flash code | 2,888 bytes | 24,576 bytes |

These are separate allocations. Their remainders and reserved stack sizes are not
measured free working memory or spare stack for additional DSP.

The nominal256-frame44.1kHz block budget is5,804us. Exact18's attended mixed run
reported MAX4,677us, no late blocks, clean audio and responsive HOME. That leaves
1,127us within this timer only; it is not a complete-device safety margin. GLO's
MAX/LATE accumulate since boot. The later48kHz musical recording remained clean
with the same reportedMAX and0late.

USB audio, DAC processing, nested interrupts and other main-loop work are outside
the timed application interval. Stack high-water and exhaustive worst-case load
remain unmeasured. Host tests and compilation do not measure device cycles.
See [validation and scope](TESTING.md). No new DSP was added during packaging.
