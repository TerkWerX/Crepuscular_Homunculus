# Bounded LCD service during MIDI clock

Introduced in 0.26.16 and retained unchanged in 0.26.19. Physical GLO previously
could stop external-clock playback while a full LCD update blocked main service.

Ordinary painting updates the existing240×240 RGB565 framebuffer. Main uploads
at most two 16×16 tiles (512 pixels) per iteration under the transport slack guard,
returning to USB/input/clock/transport between batches. A round-robin cursor
prevents animated areas starving other tiles; later paints coalesce.

A 744-byte main-owned queue holds 225 dirty flags, one 256-pixel immutable DMA tile
and cursor. No new framebuffer, DSP buffer or allocation is added. DMA completion
precedes mirror eligibility; existing snapshots remain immutable. Mirror disabled
does not stop physical delivery. Native display detail and the optional reduced
Thorn Rift mirror codec remain intact.

LCD initialization/rescue stays immediate. Updater, USB boot and fault paths
finish DMA, cancel ordinary paints and draw immediately. Missing-clock, burst
and delayed-swing guards remain; no raised timeout or catch-up burst is added.

Optimized/ASan tests cover fake asynchronous DMA, fairness, phase-varied clock
stalls, pixels, mirror ownership and emergency paths. Repeated owner HOME/GLO
tests on 0.26.16 passed with responsive displays and no musical interruption. These
are observed results, not a hard real-time or full CPU/stack guarantee.
See [validation](TESTING.md) and [current allocation](PERFORMANCE.md).
