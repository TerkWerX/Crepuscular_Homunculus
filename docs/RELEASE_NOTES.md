# Crepuscular Homunculus 0.26.23-ns1

The current 64-sound bank matches the Homunculus VST definitions, including the fuller NIGHT SUTURE pad. Original stored sounds remain playable; no existing song is silently remixed. Read [current release, backup and compatibility details](NIGHT_SUTURE.md). This exact build is an offline-validated preview; older hardware observations below apply only to their named builds.

---

# Crepuscular Homunculus 0.26.19

A standalone dark synth and four-track groovebox for the M-VAVE FM-1. Play,
edit, sequence and SAVE on the instrument without a companion application.

The supplied firmware remains the exact hardware-tested **0.26.19-dev** build,
tag `v0.26.19-dev`. No binary was rebuilt or relabeled to remove the suffix.

## Since the earlier public 0.22.0 preview

- Crepuscular Homunculus naming, bubbling intro and reactive character display,
  with the existing creator splash and selectable Thorn Rift display retained.
- Native piano-roll editing and independent selected-track sound control.
- Saved song-wide swing: 0 is straight, 50 has a moderate lilt and 100 a stronger
  lilt. Supported older songs load with straight timing.
- Confirmation protects against accidentally resetting a song by holding HOME.
- Bounded LCD service keeps page changes from blocking external-clock service.
- More distinct SULFUR TONGUE and dry FM WICKED FELT at factory positions 25 and
  52. There are still 64 factory choices; original stored definitions remain
  available to older captures, with 66 internal definitions in total.
- Corrected updater identity: `FM-1_986` in the package/application and
  `ota-FM-1_986` in the loader. USB names use Crepuscular Homunculus.

## Install and preserve music

Download `Crepuscular-Homunculus-0.26.19-dev.fwsc` and verify its SHA-256 below.
Stop PLAY, release keys/pedals, let tails finish, press SAVE and wait for SAVED.
Keep a separate compatible song backup and the previous firmware file before
switching. See [installation and recovery](INSTALL.md).

Supported older Homunculus saves migrate through the original definitions.
Journal format 3 records catalog revision 2; older firmware cannot read the new
revision IDs or full journal. Other firmwares' projects/samples are not imported
automatically. See [compatibility](CATALOG_REVISION_2.md).

## Validation and limits

Exact 0.26.19 passed real-device installation from custom firmware and official V15,
outgoing update, USB identity and a controlled host-transfer interruption
followed by one completion of the same package. Full song/captured-sound and
queryable LIVE readbacks matched; SAVE was clear. Physical power and USB stayed
connected during the recovery check.

Revised-sound listening, standalone mixed playback, sustained 48 kHz USB music
and saved-song restart checks passed on 0.26.18. The 0.26.19 target differs only in identity/
version strings. Earlier 44.1 kHz, swing, controls, clock and pedal tests remain
attributed to their earlier builds. Twenty optimized and ASan host groups and
target/package checks pass. See [full validation scope](TESTING.md).

Eight voices are shared, at most four FM. App timing excludes complete USB/DAC/
ISR/main-loop load; no exhaustive CPU/stack or power-loss recovery claim is made.
No new effects or increased polyphony are included.

Firmware SHA-256:
`6d345d659275634492c7cb4ed65bfea4b492c83981abd1789d4518453ab9baa5`

GPL-3.0-only. Complete matching source, build instructions and inherited credits
are included. No companion or specialist firmware implementation is bundled.
