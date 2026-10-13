# 0.22.0-dev validation scope

This release was built with the guarded target build. The app uses 160,968 bytes of its app slot, 72,048 bytes of 98,304 bytes of main RAM and 303,672 bytes of 344,064 bytes of pool memory. The packaged `.fwsc` is 610,110 bytes. The installer completed its write and reboot on one FM-1; a separate read-only identity query returned `FM-1_960 [running]`, and the post-update RAM song backup reported `0.22.0-dev` with all 16 banks and 1,024 steps readable. The pre-update backup was verified before installation.

The firmware test suite includes host DSP, sequencer, interface, MIDI and protocol checks. The 0.22 host checks passed before the target build. The new control paths and broader external-controller combinations still need attended device testing. The package identity `FM-1_960` is provisional and should be checked against other projects before large distribution.

After the 0.22 installation, the owner reported **3072 µs maximum render and 0 late blocks** while exercising the taxing CRUCIBLE FM preset and layered playback. The app's 256-frame block interval is about 5805 µs, so this observation uses about 53% of that interval. GLO times `tk_render`, the metronome and the optional visual tap; it does not cover the later USB-audio copy/resampler, MASTER scaling, DAC work or every nested interrupt. Repeat the test at both 44.1 and 48 kHz USB-audio rates, and with all four tracks active, before calling loaded audio fully qualified. See [performance notes](PERFORMANCE.md).

Visual previews in this repository were rendered by the host harness. They are representative UI captures, not photographs of a 0.22 device display.
