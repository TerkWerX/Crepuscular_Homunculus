# Crepuscular Homunculus

**A dark, standalone synth and four-track groovebox for the M-VAVE FM-1.**

Created by **TERKINSTEIN** Â· Free and open source

Turn your FM-1 into an instrument for atmospheric pads, heavy bass, metallic
textures, synthesized drums and six-operator FM. Play, build patterns and save
complete songs on the FM-1 itself. No companion app or Fire controller is required.

**[Download 0.26.19](https://github.com/TerkWerX/Crepuscular_Homunculus/releases/tag/v0.26.19)**
Â· **[Try the virtual FM-1](https://terkinstein.com/#play)**
Â· **[Visit terkinstein.com](https://terkinstein.com/)**

![Crepuscular Homunculus HOME display](assets/hub/home-0.26.19.png)

## What you can do

- **Explore 64 factory sounds**, then shape them with the FM-1's controls.
- **Build a song on four tracks:** DRUM, MELODY, ATMOS and BASS, each with its own captured sound.
- **Create variations** with four Aâ€“D banks per track and up to 64 steps per bank.
- **Edit notes directly** with step entry, live recording and the native piano roll.
- **Add swing**, use the arpeggiator, or play from a MIDI keyboard with pedal support.
- **Choose your HOME display:** the reactive Homunculus or Thorn Rift, following the creator startup logo.

## Get started

1. **Download** the `.fwsc` firmware from the [0.26.19 release](https://github.com/TerkWerX/Crepuscular_Homunculus/releases/tag/v0.26.19).
2. **Protect your music.** Stop playback, release keys and pedals, press SAVE and wait for SAVED. Keep a separate backup and your previous firmware.
3. **Install from a computer** using the [installation guide](docs/INSTALL.md).
4. **Start playing** with the [standalone quick start](docs/QUICK_START.md).

The release also includes matching source code, checksums and licensing information.
After installation, the instrument works without a computer.

## Find the right guide

| I want toâ€¦ | Read this |
| --- | --- |
| Play a sound and make a pattern | [Quick start](docs/QUICK_START.md) |
| Learn the buttons and knobs | [Panel controls](docs/PANEL_CONTROLS.md) |
| Edit individual notes and chords | [Piano roll](docs/PIANO_ROLL.md) |
| Explore the sound collection | [Sound guide](docs/PRESET_GUIDE.md) |
| Install, switch back or recover | [Installation and recovery](docs/INSTALL.md) |
| Understand older-song compatibility | [Saved songs and sound revisions](docs/CATALOG_REVISION_2.md) |
| See what changed in this release | [Release notes](docs/RELEASE_NOTES.md) |

## A few useful limits

Eight voices are shared across the four tracks, LIVE playing and release tails;
at most four voices can use FM. Large chords and overlapping tails share that
budget. The FX button bypasses corrosion/BITE rather than every sound control.

Supported older Homunculus songs migrate explicitly. Projects, sounds and samples
from unrelated firmware are not imported automatically. See [performance limits](docs/PERFORMANCE.md)
and [validation](docs/TESTING.md) for the details.

## Source and credits

This repository contains only Crepuscular Homunculus firmware, artwork, platform
and update code, build tools, tests and documentation. The companion application
and the separate specialist firmwares are not included.

Developers can start with the [build guide](docs/BUILD.md) and [architecture](docs/DESIGN.md).
The complete source for this release is at tag **v0.26.19**.

Licensed under **GPL-3.0-only**. Derived from Dinghy, with Felucca/SLOOP platform
work; see [licensing and credits](LICENSING.md). M-VAVE and FM-1 are trademarks of
their owners. This is an independent project.

*The HOME picture is a native host render, not a hardware photograph.*
