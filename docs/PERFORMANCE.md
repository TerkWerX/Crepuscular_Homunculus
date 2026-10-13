# Audio performance review

The current 0.22 build is deliberately conservative. It has six shared voices; up to four may be six-operator FM voices at once. FM routing is cached per voice when the algorithm changes, operator envelopes and other slow controls advance every 32 frames, and silent voices are skipped by the per-sample renderer. Screen drawing, USB commands and flash saves do not enter the synthesis callback.

On one FM-1, the owner reported **MAX RENDER 3072 µs, LATE BLOCKS 0** while stress-testing CRUCIBLE (preset 34) with layered playback. The measured app interval is about 5805 µs. This establishes useful headroom for that session, not a worst-case benchmark of every preset and controller combination.

GLO's timer begins just before `tk_render` and ends after click mixing and the optional sound-to-visual capture. The platform's later MASTER, USB-audio ring copy or 48 kHz FIR resampling, DAC output and nested USB/timer interrupts are outside that timed section. Thus a clean GLO result alone cannot rule out USB-audio underruns, host monitoring delay or an external audio-routing crackle. The Windows “Listen to this device” route has already produced audible latency independent of FM-1 synthesis.

The remaining qualification run should record GLO values and USB audio stability at 44.1 and 48 kHz while four tracks, a full CRUCIBLE chord, metronome and HOME animation are active. A host build measures correctness and relative changes, not JieLi target cycles. Any DSP optimization should be compared against deterministic PCM hashes and then device timing and listening; there is no unverified audio rewrite in this release.
