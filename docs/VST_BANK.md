# Crepuscular Homunculus firmware 0.26.23-ns1

The redesigned **64-sound Homunculus VST bank is now the FM-1 factory browser**. Names and browser order stay familiar. Sounds have distinct envelopes, FM operator patches, modulation and velocity defaults; swelling textures use fixed amplitude response where authored. There are eight Pad, twelve Metal, five kit and thirty-nine FM factory choices. Existing eight shared voices and four simultaneous FM voices remain.

NIGHT SUTURE now selects the fuller retained PAD47. The previous FM112 remains playable in saved sounds and displays OLD. See [current NIGHT SUTURE compatibility](NIGHT_SUTURE.md).

## Status

**Offline-validated preview; not installed on an FM-1.** Exact-build physical CPU/stack, USB, DAC, listening and updater installation checks remain pending. This does not claim identical FM-1 and computer loudness, physical timing or latency. Main publication was aligned with the VST for revised FX-off definitions; no new DSP algorithm, voice, audio callback allocation or sample assets were added. Inherited louder output stage, USB-audio code, graphics, updater/recovery and flash guards are retained.

## Installation and backup

Use the matching browser installer on terkinstein.com with **only the intended FM-1 connected**. Close other MIDI apps. Stop PLAY/REC and step editing, release keys and pedals, allow all tails to finish, press physical SAVE and wait for SAVED/dirty clear. The browser reads and checks all four tracks, sixteen banks, 1,024 stored steps, captured patches and queryable LIVE data. Save the backup file and select that same file for verification before updating. LIVE fields not fully queryable still require completed native SAVE; a song export is not a complete raw-settings or flash backup. No automatic SAVE or song restoration is performed.

Keep the earlier firmware and its verified backup. Verify the SHA256SUMS before installing. Do not use force options or interrupt USB/power. After boot press HOME once and use Check my song, then confirm version 0.26.23-ns1 on GLO. Do not claim preservation until the postboot comparison succeeds. The source includes the inherited Python installer and recovery instructions for users choosing that route.

## Finding the redesigned sounds

Turn PRESETS to select the new versions. Previously saved LIVE settings and captured tracks remain the original sounds; an **OLD** title identifies a retained definition. Select a new browser sound and explicitly capture it onto a track if desired. No existing song is silently remixed. New default settings use the new ASH CHOIR; existing saved settings are not reset.

## Compatibility

The original 66 definitions, stored IDs, FM patches and response policies remain immutable. Sixty-one revised definitions occupy IDs 66–126; three retained kits keep their original IDs. The visible browser still has 64 choices. Raw Program Change addresses stored IDs, not browser positions; the table below lists the new mapping.

Catalog revision 3 / native journal format 3 is explicit. Earlier catalog-2 saves load only with their valid original 66 IDs, preserving all four tracks and banks. Forged/stale catalog references refuse; previous-valid recovery remains. **Older firmware cannot read revision-3 saves.** Downgrading is not a project conversion or restoration. Older desktop FireLab profiles that expect 64/66 definitions must not be used as if they support this 127-definition catalog; use the matching browser installer or standalone device workflow until a matching profile is qualified. Firmware-track host-playback capabilities are not added by this sound-bank release.

## Offline evidence

All 64 actual compiled VST/firmware definitions match 58 fields each. Independent real-firmware and final VST engine translation units match 6,291,456 raw-engine stereo sample frames over 192 scenes: normal/light velocity, sustain, sostenuto, channel 16 and same-pitch retrigger. All four captured-track macro/velocity publications checked. All sounds reach finite release without Panic; Panic completes silence. This is pre-output-stage PCM, not identical USB/DAC or headphone output.

All 127 stored app_audio scenes retain exact reference PCM. All 127 stored definitions render non-silent bounded output in the synthetic scene. Twenty-two actual native suites pass with whole-fixture AddressSanitizer; optimized suite issues were confined to fixture arguments and stale pre-catalog/pre-lookahead assumptions and were corrected without weakening product guards. Native save/migration/recovery, controller/pedal ownership, afterimages, sequencing, display, metronome and updater-identity checks pass. Browser backup/update simulation covers old and current catalogs, exact version/count refusal, step-editing/dirty/held-key refusal, query corruption, disconnects, wrong loader, bounded update phases and preserved song verification; no physical MIDI was used.

Target pi32v2: image 224,016 / 581,564 bytes; static RAM 86,208 / 98,304; pool 319,876 / 344,064. Ram-text 909 instructions/no calls and HAL register guards pass. Package identity FM-1_986; header/image CRC independently parsed. No device CPU or real flash qualification is inferred.

## Factory-to-stored-ID mapping

| Browser | Name | Stored ID | Engine |
|---|---|---|---|
| 01 | ASH CHOIR | 66 | Pad |
| 02 | RUST HALO | 67 | FM |
| 03 | BLACK GLASS | 68 | FM |
| 04 | VEIL ENGINE | 69 | Pad |
| 05 | CHAPEL GLASS | 70 | FM |
| 06 | WIRE HYMNS | 71 | FM |
| 07 | ASHEN TIDE | 72 | Pad |
| 08 | GRAVE CLOCK | 73 | FM |
| 09 | NOCTURNE 13 | 74 | Pad |
| 10 | SALT STATIC | 75 | FM |
| 11 | CATHODE MOTH | 76 | FM |
| 12 | IRON PSALM | 77 | Metal |
| 13 | FURNACE VOW | 78 | Metal |
| 14 | SHARD CROWN | 79 | FM |
| 15 | ANVIL SERAPH | 80 | Metal |
| 16 | VOLT TEETH | 81 | Metal |
| 17 | RUST TALON | 82 | FM |
| 18 | BLADE REQUIEM | 83 | Metal |
| 19 | TOMB CURRENT | 84 | FM |
| 20 | SLAG PULSE | 85 | Metal |
| 21 | QUAKE ENGINE | 86 | Metal |
| 22 | HOLLOW ROOT | 87 | FM |
| 23 | GRIT PILGRIM | 88 | Metal |
| 24 | OBSIDIAN KEYS | 89 | FM |
| 25 | SULFUR TONGUE | 90 | FM |
| 26 | TARNISH BELL | 91 | FM |
| 27 | SABLE NEEDLES | 92 | FM |
| 28 | GRAVE HAMMER | 93 | FM |
| 29 | PALLID CHOIR | 94 | FM |
| 30 | MIRROR INK | 95 | FM |
| 31 | SCRAP ALTAR | 30 | Drum kit |
| 32 | RIVEN SIGNAL | 96 | FM |
| 33 | NERVE CHOIR | 97 | FM |
| 34 | CRUCIBLE | 98 | FM |
| 35 | BONE WIRE | 99 | FM |
| 36 | SPECTRAL GRAFT | 100 | FM |
| 37 | TAR MARROW | 101 | Metal |
| 38 | COIL BURROW | 102 | FM |
| 39 | MOSS CAPACITOR | 103 | Pad |
| 40 | CINDER PISTON | 104 | Metal |
| 41 | HUSK CURRENT | 105 | FM |
| 42 | ROOT TERMINAL | 106 | FM |
| 43 | FANG RELAY | 107 | FM |
| 44 | ION STITCH | 108 | FM |
| 45 | VELVET FAULT | 109 | FM |
| 46 | SCAR VOLTAGE | 110 | Metal |
| 47 | MOTH CIRCUIT | 111 | FM |
| 48 | NIGHT SUTURE | 47 | Pad |
| 49 | BONE SPARKS | 113 | FM |
| 50 | VELLUM PRONG | 114 | FM |
| 51 | OXIDE CHIMES | 115 | FM |
| 52 | WICKED FELT | 116 | FM |
| 53 | GLASS VERTEBRA | 117 | FM |
| 54 | LANTERN RATTLE | 118 | FM |
| 55 | RIVET WAKE | 54 | Drum kit |
| 56 | BOILER TEETH | 55 | Drum kit |
| 57 | DUST CASKET | 119 | Drum kit |
| 58 | CHROME FUNERAL | 120 | Drum kit |
| 59 | VEIN WEATHER | 121 | Pad |
| 60 | NOCTILUCE VAT | 122 | FM |
| 61 | LICHEN VECTOR | 123 | Pad |
| 62 | SUTURE FOG | 124 | Metal |
| 63 | HOLLOW EMBER | 125 | FM |
| 64 | PHANTOM LOOM | 126 | FM |

## Build and licensing

Full corresponding source, generated bank, creator artwork, platform/HAL, loader, focused tests and licenses accompany this download. No private songs, recordings or desktop companion payloads are included. Extract the source ZIP, enter `firmware/` and follow `docs/BUILD.md`. The matching Linux pi32v2 compiler and AC79 SDK are external dependencies. Use `python tools/build.py --release 0.26.23-ns1` with the documented toolchain/SDK environment. Target building does not install firmware.

Optional native comparison: root CMake builds the independent VST/firmware comparison and host checks. `cmake -S . -B native -A x64`, `cmake --build native --config Release`, then `ctest --test-dir native -C Release --output-on-failure`. On MSVC, `-DCHECK_ASAN=ON` enables AddressSanitizer. Tests use synthetic hardware only.

GPL-3.0-only. Preserve Dinghy/Felucca/SLOOP authorship, inherited copyright notices and SDK/third-party license notices. See LICENSE and LICENSING.md in the firmware source.
