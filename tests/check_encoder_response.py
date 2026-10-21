#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual-main FM-1 encoder response, persistence and MIDI neutrality checks.

The fake platform supplies detents and timestamps; this is not a physical feel,
USB bandwidth or hardware audio-deadline measurement.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import struct

from PIL import Image

from check_external_midi import Panel, ordered, peak
from check_remote import decode, sysex

RESPONSE_AT = 3455  # Four-byte host storage header plus payload byte 3451.
FM_AT = 3456


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    folder = args.output.resolve()
    folder.mkdir(parents=True, exist_ok=True)
    panel = Panel(args.executable.resolve(), folder / "response", folder)
    checks = 0

    def check(good, why):
        nonlocal checks
        checks += 1
        assert good, why

    def run(label, lines, *, store=None, end=2000, fps=0):
        target = folder / f"{label}.bin"
        if target.exists():
            target.unlink()
        result = panel.run(label, ordered(*[line + "\n" for line in lines], f"{end} end\n"),
                           store=store, fps=fps)
        check(result[2] is not None, f"{label}: silent physical SAVE did not finish")
        check(struct.unpack_from("<II", result[2]) == (3700, 12),
              f"{label}: encoder response changed the sector size or schema")
        return result

    def replies(result):
        found = {}
        for match in re.finditer(r"\d+ ms\s+REMOTE out ([0-9A-F]+)",
                                 result[3].with_suffix(".log").read_text()):
            _, request, payload = decode(bytes.fromhex(match[1]))
            check(payload and payload[0] == 0, "A read-only state query failed")
            found[request] = payload
        return found

    factory = run("factory", ["10 btn 8", "1800 btn 9"])[2]
    check(factory[RESPONSE_AT] == 0, "Factory control feel must retain LINEAR compatibility")
    base = bytearray(factory)
    for offset in (8, 9, 10, 11, 15, 17, 18, 22, 23):
        base[offset] = 64
    base[19] = 0
    base[21] = 0
    base[3409] = 64  # GHOST MASK: host header + payload 3405.
    # Loading a manual live patch refreshes its zero-amount GRAFT base. Use the
    # completed real SAVE as a coherent baseline rather than stale factory base.
    linear = run("normalized-linear", ["10 btn 8", "1800 btn 9"], store=bytes(base))[2]
    base = bytearray(linear)
    base[RESPONSE_AT] = 1
    adaptive = bytes(base)

    # Enter through the real GLO -> SEL menu; unrelated knobs and navigation
    # cannot edit sound while changing the saved hardware feel preference.
    feel = run("feel-menu", ["10 btn 8", "100 btn 7", "140 btn 3", "200 enc 2 1",
               "220 enc 2 1", "260 enc 4 -32767", "280 enc 5 32767",
               "300 enc 0 1", "320 enc 1 1", sysex(400, 0x0C, 1), "1800 btn 9"],
               store=linear, fps=2)
    wanted = bytearray(linear)
    wanted[RESPONSE_AT] = 1
    check(feel[2] == wanted, "CONTROL FEEL must edit only its saved mode byte")
    check(not feel[1] and peak(feel[0], 0, 1700) == 0, "CONTROL FEEL caused sound or generated MIDI")
    check(replies(feel)[1][3] == 15, "GLO then SEL did not open the appended CONTROL FEEL page")
    with Image.open(Path(f"{feel[3]}-00002.ppm")) as picture:
        picture.resize((480, 480), Image.Resampling.NEAREST).save(folder / "control-feel-preview.png")
    reloaded = run("feel-reload", ["10 btn 8", "1800 btn 9"], store=feel[2])[2]
    check(reloaded == feel[2], "Control feel was not retained by a fresh-process SAVE/reload")
    menu_linear = run("feel-back-linear", ["10 btn 8", "100 btn 7", "140 btn 3",
                      "200 enc 2 -1", "1800 btn 9"], store=adaptive)[2]
    check(menu_linear == linear, "Choosing LINEAR could not restore the original mode exactly")

    setup = ["10 btn 8", "200 btn 4"]
    strokes = ["300 enc 2 1", "320 enc 2 1", "360 enc 2 1", "500 enc 2 1", "1800 btn 9"]
    slow = run("linear-fast", setup + strokes, store=linear)
    quick = run("adaptive-fast", setup + strokes, store=adaptive)
    check(slow[2][22] == 68, "LINEAR did not keep one stored unit per detent")
    check(quick[2][22] == 72, "Adaptive fast/middle/slow turns missed 1x/4x/2x/1x response")
    check(quick[2][23] == 64, "Attack encoder changed the release control")
    check(quick[2][24:3408] == linear[24:3408], "Panel feel rewrote a captured sound or sequence")
    check(quick[2][FM_AT + 40:FM_AT + 200] == linear[FM_AT + 40:FM_AT + 200],
          "Panel feel changed any captured six-operator FM patch")
    fine = run("adaptive-slow", setup + ["300 enc 2 1", "400 enc 2 1", "500 enc 2 1", "1800 btn 9"],
               store=adaptive)
    check(fine[2][22] == 67, "Slow adaptive turns must preserve single-unit fine control")
    reverse = run("reverse", setup + ["300 enc 2 1", "320 enc 2 1", "340 enc 2 -1",
                  "360 enc 2 -1", "380 enc 2 1", "1800 btn 9"], store=adaptive)
    check(reverse[2][22] == 65, "Reversing direction failed to reset to a single-unit correction")
    separate = run("independent-knobs", setup + ["300 enc 2 1", "320 enc 2 1",
                   "330 enc 3 1", "350 enc 3 1", "1800 btn 9"], store=adaptive)
    check(separate[2][22:24] == bytes((69, 69)), "Another knob inherited the preceding knob's speed")

    # Leaving a page and returning within the acceleration interval must still
    # reset response. Looking only at context on nonzero detents misses this.
    returned = run("leave-and-return", setup + ["300 enc 2 1", "320 enc 2 1",
                   "330 btn 8", "340 btn 4", "350 enc 2 1", "1800 btn 9"], store=adaptive)
    check(returned[2][22] == 70, "Returning to ENV inherited speed from the previous page visit")
    preset_lines = setup + ["300 enc 2 1", "320 enc 2 1", sysex(330, 0x16, 1, (1,)),
                           sysex(340, 0x16, 2, (0,))]
    fresh = run("preset-reset-reference", preset_lines + ["1800 btn 9"], store=adaptive)
    reset = run("preset-away-and-back", preset_lines + ["350 enc 2 1", "1800 btn 9"], store=adaptive)
    check(reset[2][22] == fresh[2][22] + 1 and reset[2][RESPONSE_AT] == 1,
          "Preset away/back retained old speed or reset the user control preference")

    # End stops cannot leave a fast outgoing gesture primed for a tiny inward
    # correction. Clamping also must be safe with hostile signed detent counts.
    edge = bytearray(adaptive)
    edge[22] = 126
    high = run("upper-edge", setup + ["300 enc 2 1", "320 enc 2 1", "340 enc 2 -1", "1800 btn 9"],
               store=bytes(edge))
    check(high[2][22] == 126, "Upper endpoint was sticky or overshot a one-unit inward correction")
    edge[22] = 1
    low = run("lower-edge", setup + ["300 enc 2 -1", "320 enc 2 -1", "340 enc 2 1", "1800 btn 9"],
              store=bytes(edge))
    check(low[2][22] == 1, "Lower endpoint overshot a one-unit inward correction")
    huge = run("bounded-extrema", setup + ["300 enc 2 32767", "320 enc 2 -32767",
               "340 enc 2 1", "1800 btn 9"], store=adaptive)
    check(huge[2][22] == 1, "Extreme bounded main-loop detents overflowed the target or broke endpoint recovery")

    # Continuous sound values accelerate; tempo, enum choices, routing,
    # navigation, note-entry cursors, switch values and controller IDs do not.
    seq = run("seq-discrete", ["10 btn 8", "200 btn 11", "220 btn 6",
              "300 enc 2 1", "320 enc 2 1", "340 enc 2 1", "360 enc 4 1",
              "380 enc 4 1", "400 enc 4 1", "420 enc 5 1", "440 enc 5 1",
              sysex(500, 0x02, 1), "1800 btn 9"], store=adaptive)
    seq_state = replies(seq)[1]
    check(seq[2][16] == adaptive[16] + 3 and seq_state[6] == 3,
          "SEQ tempo or edit cursor accelerated and skipped values")
    check(seq_state[26] == 69, "SEQ continuous Ghost Mask did not support adaptive turns")
    arp = run("arp-order", ["10 btn 8", "100 hold 10 1", "850 hold 10 0",
              "1000 enc 3 1", "1020 enc 3 1", "1800 btn 9"], store=adaptive)
    check(arp[2][21] == 2, "ARP note order skipped a named choice during a fast turn")
    lfo = run("lfo-shape", ["10 btn 8", "100 hold 5 1", "850 hold 5 0",
              "1000 enc 4 1", "1020 enc 4 1", "1800 btn 9"], store=adaptive)
    check(lfo[2][19] == 2, "LFO shape accelerated across enumerated choices")
    metro = run("metro-values", ["10 btn 8", "100 hold 11 1", "850 hold 11 0",
                "1000 enc 3 1", "1020 enc 3 1", "1040 enc 4 1", "1060 enc 4 1",
                "1800 btn 9"], store=adaptive)
    check(metro[2][3698:3700] == bytes((37, 6)), "Metronome level or beats used the wrong response class")

    # Audit the actual primary control path on every kind of sound page. A
    # disabled page must consume movement rather than accelerate a hidden macro.
    for label, enter, encoder, offset in (
        ("home", [], 2, 8), ("sound-edit", ["200 btn 6"], 3, 9),
        ("memory", ["200 btn 3"], 2, 15),
        ("lfo-rate", ["100 hold 5 1", "850 hold 5 0"], 2, 17),
        ("fx", ["100 hold 2 1", "850 hold 2 0"], 2, 9),
        ("voice", ["200 hold 6 1", "220 keys 4194304", "270 hold 6 0", "290 keys 0"], 5, 23),
    ):
        sample = run("page-" + label, ["10 btn 8"] + enter +
                     [f"1000 enc {encoder} 1", f"1020 enc {encoder} 1", "1800 btn 9"], store=adaptive)
        check(sample[2][offset] == 69, f"{label}: continuous primary control did not use the saved adaptive response")

    fm_factory = run("fm-factory", ["10 btn 8", sysex(100, 0x16, 1, (32,)), "1800 btn 9"],
                     store=adaptive)[2]
    fm = bytearray(fm_factory)
    fm[FM_AT + 1] = 64  # OP1 level.
    fm[FM_AT + 37] = 64  # Global feedback.
    fm[FM_AT + 39] = 64  # Pitch envelope time.
    fm = run("fm-normalized", ["10 btn 8", "1800 btn 9"], store=bytes(fm))[2]

    def label_page(key, start=200):
        return [f"{start} hold 6 1", f"{start + 20} keys {1 << key}",
                f"{start + 70} hold 6 0", f"{start + 90} keys 0"]

    operator = run("fm-operator", ["10 btn 8"] + label_page(1) +
                   ["1000 enc 3 1", "1020 enc 3 1", "1040 enc 2 1", "1060 enc 2 1", "1800 btn 9"],
                   store=fm)
    check(operator[2][FM_AT + 1] == 69 and operator[2][FM_AT] == fm[FM_AT] + 2,
          "OP level failed acceleration or the discrete frequency ratio skipped values")
    routing = run("fm-global", ["10 btn 8"] + label_page(17) +
                  ["1000 enc 2 1", "1020 enc 2 1", "1040 enc 4 1", "1060 enc 4 1", "1800 btn 9"],
                  store=fm)
    check(routing[2][FM_AT + 37] == 69 and routing[2][FM_AT + 36] == fm[FM_AT + 36] + 2,
          "FM feedback failed acceleration or routing skipped an algorithm")
    pitch = run("fm-pitch", ["10 btn 8"] + label_page(15) +
                ["1000 enc 3 1", "1020 enc 3 1", "1040 enc 4 1", "1060 enc 4 1", "1800 btn 9"],
                store=fm)
    check(pitch[2][FM_AT + 39] == 69 and pitch[2][3454] == fm[3454] + 2,
          "Pitch time failed acceleration or external bend range skipped semitones")
    focus = run("operator-away-back", ["10 btn 8"] + label_page(1) +
                ["500 enc 3 1", "520 enc 3 1", "530 hold 6 1", "540 keys 8", "545 hold 6 0",
                 "550 keys 0", "560 hold 6 1", "570 keys 2", "575 hold 6 0", "580 keys 0",
                 "590 enc 3 1", "1800 btn 9"], store=fm)
    check(focus[2][FM_AT + 1] == 70, "Operator focus away/back reused another operator's acceleration state")

    kit = run("kit-factory", ["10 btn 8", sysex(100, 0x16, 1, (30,)), "1800 btn 9"], store=adaptive)[2]
    disabled = run("kit-memory-unavailable", ["10 btn 8", "200 btn 3", "1000 enc 2 1",
                   "1020 enc 2 1", "1800 btn 9"], store=kit)
    check(disabled[2] == kit and not disabled[1], "Unavailable KIT memory page edited hidden sound or MIDI")

    # Smooth Fire knobs send absolute track macros, and generic keyboards send
    # standard MIDI CCs. Neither goes through the physical FM-1 speed helper.
    external = ["10 btn 8", "150 midi 0x90 60 100", "300 midi 0xB0 20 65",
                "320 midi 0xB0 20 66", sysex(340, 0x1E, 1, (2, 1, 65)),
                sysex(360, 0x1E, 2, (2, 1, 66)), "500 midi 0x80 60 0", "8000 btn 9"]
    usb_linear = run("external-linear", external, store=linear, end=8500)
    usb_adaptive = run("external-adaptive", external, store=adaptive, end=8500)
    check(usb_linear[:2] == usb_adaptive[:2], "Panel acceleration altered external-controller PCM or MIDI")
    left, right = bytearray(usb_linear[2]), bytearray(usb_adaptive[2])
    right[RESPONSE_AT] = left[RESPONSE_AT]
    check(left == right and right[8] == 66, "External absolute values were reshaped or skipped")
    check(replies(usb_adaptive)[2][3] == 66, "Fire track macro readback did not retain the exact absolute value")

    old = bytearray(adaptive[:3700])
    struct.pack_into("<II", old, 0, 3696, 10)
    migrated = run("schema10-migrate", ["10 btn 8", "1800 btn 9"], store=bytes(old))[2]
    check(migrated[RESPONSE_AT] == 0, "Schema-10 reserved byte was misread as a user acceleration preference")
    check(migrated[8:RESPONSE_AT] == adaptive[8:RESPONSE_AT] and
          migrated[RESPONSE_AT + 1:3697] == adaptive[RESPONSE_AT + 1:3697],
          "Schema-10 migration disturbed sounds, controllers, patterns or FM fields")
    bad = bytearray(adaptive)
    bad[RESPONSE_AT] = 2
    fallback = run("invalid-feel", ["10 btn 8", "1800 btn 9"], store=bytes(bad))[2]
    check(fallback == factory, "Invalid saved control preference did not use safe factory defaults")

    report = {"checks": checks, "hardware_qualified": False,
              "preview": str(folder / "control-feel-preview.png"),
              "save_size": 3700, "schema": 12,
              "covered": ["real menu selection", "saved preference reload", "fine/fast response",
                          "reversal", "separate knobs", "page/preset reset", "endpoints/bounded extrema",
                          "discrete exclusions", "external MIDI/Fire neutrality", "schema10 migration"]}
    (folder / "encoder-response-report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"encoder actual-main: {checks} checks passed; physical feel remains unqualified")


if __name__ == "__main__":
    main()
