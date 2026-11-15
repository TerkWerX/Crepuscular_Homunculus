# Validation — 0.26.23-ns1

Current evidence is in [data/release-0.26.23-ns1.json](data/release-0.26.23-ns1.json).22 native whole-fixture ASan suites passed, including 75 factory/save-reload checks and independent VST/firmware parity. All 127 stored app_audio reference scenes remain unchanged. Target image 224016/581564, static RAM 86208/98304, pool 319876/344064; ram_text 909/no calls, register guards, identity/headerCRC pass. No exact-build physical CPU/USB/DAC/listening/install qualification is claimed. Historical evidence below retains its original scope.

---

# Validation and limits — 0.26.19-dev

The package is the exact tested19 binary, SHA-256
`6d345d659275634492c7cb4ed65bfea4b492c83981abd1789d4518453ab9baa5`.
Exact 0.26.19 hardware results and unchanged-lineage musical results are distinguished
below. Host runs and compilation are not hardware qualification.

## Exact 0.26.19 hardware checks completed October 26, 2026

| Check | Result |
| --- | --- |
| Custom 0.26.18 to19 | Passed after full RAM/captured-sound backup and completed SAVE |
| USB/updater identity | Normal FM-1_986, loader ota-FM-1_986, native 0.26.19-dev |
| Other custom firmware entry | Spare Sloop 2.8.1 to exact 0.26.19 passed |
| Outgoing update | Exact 0.26.19 to official M-VAVE V15 passed |
| Official V15 entry | V15 to the same exact 0.26.19 passed |
| Preservation | All 16 banks/1,024 steps/four captures/FM and queryable LIVE matched; SAVE clear |
| Interrupted transfer | Exact18 setup, partial 0.26.19, then one ordinary same 0.26.19 completion passed |

The additional interruption test stopped host responses before logical 0x5400,
after eight confirmed 512-byte first-sector responses. At 20.344 seconds after
closing that connection, the same loader still answered. A normal full-package
transfer completed. Full song bytes and queryable LIVE matched the fresh backup;
all 7,137 read queries across three snapshots succeeded.

Power and physical USB stayed connected. First-sector processing is inferred
from the inspected loader/request sequence, not independent raw-flash readback.
This is a sector-boundary host-transfer test, not power-cut, mid-erase/program
or universal recovery qualification. It was an additional project test, not an
explicit hub submission requirement.

[Public hardware summary](data/release-0.26.19-hardware.json) contains aggregate
results only. Personal songs, raw backups and controller settings are excluded.

## Exact 0.26.19 offline and target checks

Twenty host groups pass optimized and ASan. Normal/recovery/loader replies,
compile overrides and USB descriptors pass. Target app bytes equal18 except
identity/version strings; the loader differs only in its identity string.
Three silent scripts produce identical PCM and settings/journal files across
18/19 and optimized/ASan. Target memory allocations are unchanged.

Package/header/member CRCs and embedded loader pass. Source include paths,
archive membership and local documentation links are checked. The current
240×240 HOME host render has been inspected. [Offline build evidence](data/release-0.26.19-validation.json)
retains its historical pre-installation status; hardware acceptance above supersedes it.

## Retained musical and persistence evidence

- On18, the owner accepted revised SULFUR TONGUE and WICKED FELT notes/chords and
  clean tails. WICKED captured independently of LIVE; SULFUR captured and saved.
- On18, a 60-second standalone mixed workload sounded clean with responsive HOME;
  MAX 4276→4677 µs, LATE 0, final voices 0. Exact peak voice count was not recorded.
- On18, a 151.06-second stereo 16-bit 48 kHz musical WAV contained 146.925 seconds of
  signal and no full-scale samples. Owner reported clean music and unchanged
  4677 µs/0late. This is an observed capture, not proof of every USB sample.
- On18, normal restart retained full song/captures/FM, swing, banks and queryable
  LIVE. Completed native SAVE was verified before and after.
- Earlier unchanged-lineage hardware checks covered 44.1 kHz USB music, swing 0/50/
  100 and live changes, import/legacy/restart, native track controls, HOME reset
  cancellation, both HOME displays, note/pedal ownership, eight shared/four-FM
  limits and repeated external-clock HOME/GLO switching.
- Offline checks preserve 296 original PCM probes; eight revised probes match
  accepted designs. Native save/recovery and fake-USB tests cover all 16 banks,
  captures/FM and migration.

## Practical limits

Eight voices are shared; at most four use FM. Release tails and pedal-held notes
use the same budget. GLO MAX/LATE accumulate since boot. The timer excludes full
USB/DAC/ISR and other main-loop work. Stack high-water and exhaustive worst-case
margins remain unmeasured.

Readback verifies all song banks/captures and available LIVE fields; some LIVE/
local settings cannot be exported. Completed native SAVE is required for those
fields. No raw settings-sector equality or universal cross-firmware preservation
is promised. Interrupted saves were checked offline; deliberate hardware
power-cut fault injection was not performed.

Source must be browsable at its release tag. GitHub build attestation is not claimed.
