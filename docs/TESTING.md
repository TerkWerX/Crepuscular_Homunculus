# Validation and release status

## Current source: 0.26.10-dev

The firmware-only source builds independently with the guarded pi32v2 toolchain.
The 610110-byte package SHA256 is
`68a86887d6202d2f79e1eb7d4ad12f39da0f1dc244350b50daed789b5ebf834f`, exactly
matching the checked local Homunculus0.26.10-dev candidate. No desktop or
specialist product source is required. The export resolves only the disabled
optional instrument-profile branches; the free engine/platform remain intact.

Target bytes: image216368/581564, staticRAM79096/98304, pool319868/344064,
RAM flash-code2888/24576. These are separate from stack reservations and measured
working margins; their remainders are not a measured DSP or spare-stack budget.

Focused optimized/ASan checks cover engine/controllers/FM/transport, both HOME
renderers and intro, saved display choice, journal, captured editing, piano roll,
protocol, screen codec, solo and metronome. Silent actual-app PAD/METAL/KIT/FM
renders compare PCM, outgoing MIDI and saved bytes with the unchanged local
source before export. Actual LCD PNGs are inspected. Evidence summary:
[homunculus-0.26.10-validation.json](data/homunculus-0.26.10-validation.json).

An inherited controller test still expected40 sources, predating Fire/keyboard
leases. It now checks the implemented45 owners and their exact source IDs;
ownership checks remain. The separate codec-budget fixture requires recorded
LCD frames; the focused runner uses the self-contained screen-codec tests.

## Before a new Hub release

This build has **not completed final attended hardware release checks**.
No installation or new release publication accompanies the source update.
Earlier loaded measurements do not qualify the changed animation/build.

On the exact build, check startup/dismissal, both HOME choices, completed
SAVE/restart, four-track playback/captured editing and note/pedal releases.
Confirm clean audio and responsive controls with HOME animation, normal LCD
mirroring and representative dense playing. Capture LAST/MAX/LATE and available
stack/working-margin evidence; check USB audio44.1/48kHz separately. App-render
timing excludes later USB/DAC/ISR work.

Then verify the final package, matching complete source tag, checksums,
current-name download/card text and recovery guide. Check provisionalFM-1_960
identity collision before Hub submission. Installation, release publication
and Hub submission remain separate owner-approved actions.

## Historical v0.22.0-dev

The existing tag/package remains unchanged. One FM-1 installed it and all
16banks/1024steps were readable. One CRUCIBLE/layered session reported
MAX3072us/LATE0, a limited observation that does not qualify0.26.10-dev.
That release predates the Homunculus intro and saved display choice.
