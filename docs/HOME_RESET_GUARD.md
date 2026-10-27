# HOME reset confirmation

Introduced in 0.26.17 and retained in 0.26.19. Holding physical HOME alone for 700ms
opens RESET DEFAULTS?. Every physical button must then be released, followed by
a fresh physical SEL-only press to confirm. Releasing HOME alone does not reset.

HOME cancels immediately. Other buttons/notes, MASTER activity, musical remote
edits, SAVE/import activity and encoder edits cancel; the encoder then retains
its normal edit. The prompt expires after 10 seconds. Preheld SEL, stale held bits,
continued holds and synthetic/virtual taps cannot confirm.

The prompt is a main-owned HOME overlay, with exact text even when Thorn Rift's
mirror uses reduced detail. Opening/canceling does not stop playback. Audio draws
nothing. Confirmed reset changes RAM only; native SAVE waits for silence as usual.
Current settings schema 13 is 3,704 bytes; no field was added for the prompt.

Optimized/ASan tests cover confirmation, cancellation, timeout, stale inputs,
time wrap and full song/PCM preservation. Owner cancellation/timeout and complete
song preservation passed on 0.26.17. The destructive confirmation branch was checked
offline, without deliberately erasing the owner's song. See [validation](TESTING.md).
