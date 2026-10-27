# Licensing

TERKINSTEIN is derived from Dinghy and is free software under the GNU General Public License, version 3 only (`GPL-3.0-only`, full text in
`LICENSE`). You may use, study, change and share it. If you give someone a firmware built from it, give them its
complete source under the same licence too.

## Who made what

| | |
| --- | --- |
| The platform: `firmware/hal/`, `firmware/loader/`, `crt0.S`, `app.ld`, `firmware/src/` (`usb.c`, `ota.c`, `recovery.c`, `lcd.c`, `gfx.c`, `libc.c`, `bootguard.h`, `uac_fir.h`), `tools/` | Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments ([Felucca](https://github.com/hugelton/Felucca)), with the changes of [SLOOP](https://github.com/isod89/sloop-fm1) (isod89) |
| Original Dinghy app, the wiring `platform.c`, the original host mode, API and chip docs | zvenson |
| TERKINSTEIN app/engine and timbre-memory helper, project docs, build adaptations and host extensions | TERKINSTEIN contributors, 2026 |

## Third-party material

| What | Licence | Where |
| --- | --- | --- |
| Terminus font 8x16 (ter-u16n), rendered into `firmware/gen/felucca_font.h` | SIL OFL 1.1 | `assets/fonts/Terminus-LICENSE.txt` |
| JieLi AC79 SDK: `uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin` are read from your SDK checkout at build time and placed in the package; no SDK files are in this tree | Apache-2.0 | <https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK> |

## The logo

`assets/logo.svg` is the TERKINSTEIN mark, GPL-3.0-only. Dinghy's reserved
designburg mark and screenshot were removed from this fork.

The splash artwork in `assets/splash/` was supplied by Terkinstein for this
project and adapted with the built-in image generation tool. Its screen bitmap
and generated firmware header are included under the project's GPL-3.0-only
licence. The source artwork is credited in `assets/splash/README.md`.

## Trademarks

"Felucca" and "Hügelton Instruments" are names of Hügelton Instruments; "M-VAVE" and "FM-1" belong to their
owners. TERKINSTEIN is not made or endorsed by them.

## Homunculus artwork

The supplied character frames in assets/crepuscular-homunculus/ and their
deterministic generated C/GIF conversions are original project artwork under
GPL-3.0-only. Source frames, generation provenance and manifests are included.
The existing TERKINSTEIN creator splash is unchanged.
