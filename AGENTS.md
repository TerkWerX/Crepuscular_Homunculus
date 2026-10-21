# Crepuscular Homunculus contributor notes

This repository is exclusively the free Crepuscular Homunculus firmware and its
corresponding source, artwork, tests, build tools and docs. Do not add the paid
companion or the separate Lead, Bass, Rhythm and Drums product implementations.
The free instrument retains four tracks.

Read README.md, docs/DESIGN.md, docs/BUILD.md and docs/RULES.md before native
changes. Preserve GPL notices, credits, creator splash, sound identifiers,
updater, recovery and boot/flash guards. Keep DSP portable and integer/fixed-point.
Audio owns DSP/buffers; main publishes bounded atomic commands. No display, USB,
allocation or flash in audio. SAVE waits for stopped playback and full silence.

Run focused host checks and inspect PNGs after UI changes. Build the target when
dependencies are available. These checks do not qualify device timing, stack,
listening or real flash. Measure the exact device workload before costly DSP.

Build/preview does not authorize installation. Install only with explicit owner
approval after inspecting the current firmware/RAM. When music must be retained,
verify complete song/captured-patch backup; if sound fields cannot be queried,
verify completed SAVE/dirty-clear first. Keep previous firmware for recovery.
Never replace historical release bytes with a new build under an old identity.
