# Audio performance scope

Crepuscular Homunculus0.26.10-dev uses eight shared voices, with at most four
six-operator FM voices. Routing is cached; slower controls advance in bounded
blocks and silent voices skip per-sample work. Display, USB and flash stay out
of the synthesis callback.

The 256-frame interval at44.1kHz is about5805us. GLO times render, metronome and
the optional visual tap. Later MASTER processing, USB ring copies/48kHz FIR,
DAC work and nested interrupts are outside that timer. MAX/LATE accumulate
since boot, so record workload and boot context. Idle results do not qualify
loaded playback. Host tests do not measure JieLi cycles or stack high-water.

Historical0.22 CRUCIBLE/layered playback showed MAX3072us/LATE0. It is not a
measurement of this0.26.10-dev build. Repeat representative four-track/dense
playing with metronome, HOME animation, normal mirroring and USB audio44.1/48kHz
before a new release, recording clean sound, responsiveness and working margins.
See TESTING.md. No demanding DSP was introduced by this source-only export.
