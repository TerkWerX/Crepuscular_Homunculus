# Crepuscular Homunculus architecture

Current0.26.19 retains the0.26.18 architecture and musical/save code; only updater identity and displayed version change. Settings schema13 is3,704 bytes; full journal format3/catalog revision2 is23,556 bytes. The older schema12 details below describe lineage; see CATALOG_REVISION_2.md for current migration and TESTING.md for completed hardware evidence.

The isolated0.26.11-dev mirror correction makes the existing compact HOME
context apply only when the saved artwork choice is Thorn Rift. Homunculus
uses the existing lossless raw/RLE/local-palette tiles at native detail, even
on a negotiated compact subscription. The existing decoder accepts both types.
Changing the context cancels an incomplete tile and refreshes coverage, avoiding
stale coarse pixels. Menus, startup, actual LCD rendering and all audio remain
unchanged. No new state, buffer or wire field is added.
Main retains the same slack guard and bounded USB admission. Measured device
load with the higher-detail portrait is still an attended acceptance step.

Four captured tracks have A-D banks, up to 64 steps and four notes per step.
LIVE and captured sounds remain independent. Original catalog identifiers stay
stable. Eight shared synthesis voices and the four-FM cap are separate limits.

The portable fixed-point engine owns voices, envelopes and audio buffers. Main
publishes packed bounded commands, with one writer per shared word. Source
ownership preserves USB/TRS channels, generated parts and leased controllers,
including note-off, sustain, sostenuto and soft-pedal releases. Editing focus
does not transfer an already-held gesture to another sound.

Chord afterimages retain four timbre snapshots and finite, source-filtered
nearest-pitch offsets. They are not audio samples or extra voices. MEMORY=0
preserves original pad PCM; FORGET/panic prevents held gestures repopulating history.

Main owns the LCD. The unchanged creator splash precedes a bounded A-B-C-B
Homunculus intro. A key/button dismisses it and still acts. HOME renders the
sound-reactive character or Thorn Rift from immutable bounded audio snapshots,
using cooperative geometry and dirty-region uploads. Audio draws nothing.

GLO → SEL opens CONTROL FEEL. Knob 1 keeps LINEAR/ACCEL; Knob 2 chooses HOME
artwork. Former padding byte3697 holds the cosmetic choice in the unchanged
schema12/3700-byte settings core. Only0xA5 selects Rift; other bytes select
Homunculus without rejecting music. Older firmware ignores the choice and
schema11 migration initializes it. Song import keeps this local preference.

Native SAVE retains complete banks, captures and arrangement in its CRC journal,
waiting for stopped playback, no recording/import and complete physical/audio
silence. Migration and previous-valid recovery remain. The inherited platform
keeps USB update, OCT− rescue and boot-fault recovery.

On-device MIDI/screen/backup protocols are firmware features. No desktop app is
required or included. Exact-build CPU, USB/DAC/ISR load and stack margin remain
device measurements; see TESTING.md and PERFORMANCE.md.
