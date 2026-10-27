# Crepuscular Homunculus 0.26.19

A free, standalone synth and four-track groovebox for the **M-VAVE FM-1**, by TERKINSTEIN.
Play, edit, sequence and save on the instrument itself.

**[Try the virtual FM-1](https://terkinstein.com/#play)** · **[Quick start](https://github.com/TerkWerX/Crepuscular_Homunculus/blob/main/docs/QUICK_START.md)** · **[Full controls](https://github.com/TerkWerX/Crepuscular_Homunculus/blob/main/docs/PANEL_CONTROLS.md)**

## Download

- **Crepuscular-Homunculus-0.26.19.fwsc** — the firmware to install.
- **Crepuscular-Homunculus-0.26.19-source.zip** — the complete matching source.
- **SHA256SUMS.txt** — checksums for verifying the attached files.

Licensing, credits and these release notes are also attached.

## What's new since 0.22

- Crepuscular Homunculus naming, bubbling intro and reactive HOME artwork, with the creator splash and selectable Thorn Rift display retained.
- Native piano-roll editing and independent selected-track sound control.
- Saved song-wide swing: zero is straight; higher settings add a stronger lilt.
- Confirmation before a held HOME press can reset the song.
- Bounded display updates that keep page changes from blocking external MIDI clock service.
- More distinct **SULFUR TONGUE** and **WICKED FELT** sounds, with original definitions retained for older captures.
- Corrected USB/updater identity and the final **0.26.19** version label, without “dev.”

## Before installing

Stop PLAY, release keys and pedals, let tails finish, press SAVE and wait for SAVED.
Keep a separate song backup and your previous firmware. Follow the [installation guide](https://github.com/TerkWerX/Crepuscular_Homunculus/blob/main/docs/INSTALL.md).

Supported older Homunculus songs migrate explicitly. Unrelated firmware's projects
and samples do not. Returning to older firmware may require its own compatible
backup; see [save compatibility](https://github.com/TerkWerX/Crepuscular_Homunculus/blob/main/docs/CATALOG_REVISION_2.md).

## Validation and limits

The final label build passes the target build, host/AddressSanitizer checks,
piano-roll navigation and save checks, and exact audio/save comparisons with
0.26.19-dev. The only native source difference from that build is the version string.

The retained 0.26.19-dev passed hardware identity, custom/official V15 switching
and controlled transfer-recovery checks. Musical, USB audio and persistence results
remain attributed to their tested builds. The suffix-free build was not flashed
during release preparation. Read the [validation record](https://github.com/TerkWerX/Crepuscular_Homunculus/blob/main/docs/TESTING.md) for exact scope.

Eight voices are shared, with at most four FM voices. No extra voices or new effects
are added. Device timing excludes full USB/DAC/ISR load; exhaustive stack and
power-loss recovery qualification is not claimed.

Firmware SHA-256:
`985c1000dc58dd0afeafb18185a5de980795908166e9356d4c72fd07e656708f`

**GPL-3.0-only.** Matching source and inherited credits are included.
