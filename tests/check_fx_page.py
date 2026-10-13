#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual-main FX tap/hold, existing-field persistence and sound ownership.

The host does not model physical LCD delays or qualify hardware audio timing.
"""
import argparse
from pathlib import Path
import re
import struct

from check_external_midi import Panel, ordered
from check_remote import decode, sysex


FX_PAGE = 13
FM_AT, TRACK_FM_AT, GRAFT_FM_AT = 3456, 3496, 3656
CAPTURED_AT, CAPTURED_END = 3352, 3408


def replies(prefix):
    result = {}
    for found in re.finditer(r"\d+ ms\s+REMOTE out ([0-9A-F]+)", prefix.with_suffix(".log").read_text()):
        command, rid, body = decode(bytes.fromhex(found[1]))
        assert body and body[0] == 0, (command, rid, body)
        result[rid] = body
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    folder = args.output.resolve()
    folder.mkdir(parents=True, exist_ok=True)
    panel = Panel(args.executable.resolve(), folder / "fx", folder)
    checks = 0

    def check(condition, message):
        nonlocal checks
        checks += 1
        assert condition, message

    def run(label, events, end=4500, store=None, fps=0):
        # Fresh case names must not silently reuse a previous test run's SAVE.
        settings = folder / f"{label}.bin"
        if store is None and settings.exists():
            settings.unlink()
        return panel.run(label, ordered(*[line + "\n" for line in events], f"{end} end\n"),
                         store=store, fps=fps)

    quiet = [sysex(50, 0x02, 1), "200 btn 8", "2000 btn 9"]
    baseline = run("gesture-initial", quiet, end=2500)
    initial_fx = replies(baseline[3])[1][27]
    check(initial_fx in (0, 1), "Factory FX flag was malformed")
    for duration in (699, 700, 701):
        case = run(f"release-{duration}", ["100 hold 2 1", sysex(300, 0x02, 1),
            f"{100 + duration} hold 2 0", sysex(900, 0x02, 2), "2000 btn 9"], end=2500)
        before, after = replies(case[3])[1], replies(case[3])[2]
        check(before[27] == initial_fx, "FX toggled while a short press was still held")
        if duration < 700:
            check(after[27] == 1 - initial_fx, "Short FX press did not toggle on release")
            check(after[72] == 0, "Short FX press navigated to the FX page")
        else:
            check(after[27] == initial_fx, "Long FX release toggled the sound")
            check(after[72] == FX_PAGE, "Long FX release did not open FX CHAMBER")
            check(case[2] == baseline[2], "Long FX gesture altered saved fields")
    instantaneous = run("legacy-tap", ["100 btn 2", sysex(200, 0x02, 1), "2000 btn 9"], end=2500)
    check(replies(instantaneous[3])[1][27] == 1 - initial_fx, "Synthetic short FX tap stopped working")

    # Menu entry and HOME exit remain note/PCM-neutral in each transport mode.
    for mode, setup in (("live", []), ("seq", ["400 btn 12"]), ("arp", ["400 btn 10"])):
        common = ["200 btn 8", "300 keys 0x4480", "1800 btn 8", "2400 keys 0", "3500 btn 9"]
        plain = run(f"navigation-{mode}-plain", setup + common)
        entered = run(f"navigation-{mode}-fx", setup + common + ["600 hold 2 1", "1500 hold 2 0"])
        check(plain[0] == entered[0], f"Opening FX changed {mode} PCM")
        check(plain[1] == entered[1], f"Opening FX changed {mode} notes/timestamps")
        check(plain[2] == entered[2], f"Opening FX changed {mode} settings")
        check("REMOTE out" not in entered[3].with_suffix(".log").read_text(), "FX navigation introduced application USB commands")

    # Same existing schema fields, including saturation, motion enabling and
    # captured-track isolation. Child C tests check each independent knob too.
    for preset, instrument in ((0, "pad"), (11, "metal"), (32, "fm"), (30, "kit")):
        setup = [sysex(50, 0x16, 1, (preset,)), "100 hold 2 1", "850 hold 2 0"]
        original = run(f"{instrument}-original", setup + ["1100 btn 9"], end=1500)
        up_events = setup + [f"{900 + knob * 35} enc {2 + knob} 32767" for knob in range(4)]
        raised = run(f"{instrument}-up", up_events + ["1100 btn 9"], end=1500, fps=10)
        low_events = up_events + [f"{1200 + knob * 35} enc {2 + knob} -32767" for knob in range(4)]
        lowered = run(f"{instrument}-down", low_events + ["1400 btn 9"], end=1700)
        offsets = (11, 9, 10, 8) if instrument == "kit" else (
            9, 8, 15 if instrument == "pad" else 10 if instrument == "metal" else FM_AT + 37, 18)
        for offset in offsets:
            check(raised[2][offset] == 127, f"{instrument} FX knob did not reach its existing field's upper bound")
            check(lowered[2][offset] == 0, f"{instrument} FX knob did not reach its existing field's lower bound")
        for edited in (raised, lowered):
            check(struct.unpack_from("<II", edited[2]) == (3700, 11), "FX changed the current save payload/schema")
            check(edited[2][24:CAPTURED_END] == original[2][24:CAPTURED_END], "Live FX edit changed patterns or captured track patches")
            check(edited[2][TRACK_FM_AT:GRAFT_FM_AT] == original[2][TRACK_FM_AT:GRAFT_FM_AT], "Live FX edit changed captured FM")
            check(edited[2][14] == 1, "Edited corrosion/BITE failed to enable its existing bypass flag")
        if instrument != "kit":
            check(raised[2][20] == 1 and lowered[2][20] == 1, "Nonzero motion edit did not enable LFO or zero depth silently disabled it")
        else:
            ignored = run("kit-unused-motion", setup + ["900 enc 0 32767", "940 enc 1 -32767", "980 btn 6", "1100 btn 9"], end=1500)
            check(ignored[2] == original[2], "KIT FX motion controls changed unrelated state")
        reloaded = run(f"{instrument}-reload", ["100 btn 9"], end=600, store=raised[2])
        check(reloaded[2] == raised[2], "FX values failed a fresh-process SAVE/reload")

    # FX EDIT controls motion rather than inheriting OPERATOR exit behavior.
    motion = run("motion-edit", ["100 hold 2 1", "850 hold 2 0", "900 enc 5 127",
        "1000 btn 6", sysex(1050, 0x02, 1), "1100 enc 0 32767", "1150 enc 1 32767",
        "1200 enc 1 -32767", "1300 btn 9"], end=1600)
    check(replies(motion[3])[1][72] == FX_PAGE, "FX EDIT left its page or entered step editing")
    check(motion[2][20] == 0, "FX EDIT did not toggle motion off")
    check(motion[2][17] == 127, "FX SELECT did not edit LFO rate")
    check(motion[2][19] == baseline[2][19], "FX ALGORITHM shape failed forward/backward wrap")

    # Actual GRAFT path: bake its effective live sound before one FX field edit.
    morph = ["100 btn 8", "200 btn 6", "300 enc 0 64"]
    morphed = run("graft-effective", morph + ["2000 btn 9"], end=2500)
    baked = run("graft-fx-bake", morph + ["500 hold 2 1", "1250 hold 2 0", "1400 enc 2 1", "2000 btn 9"], end=2500)
    check(morphed[2][3429] == 64 and baked[2][3429] == 0, "FX edit did not bake active GRAFT")
    for offset in (8, 10, 11, 14, 15, 17, 18, 19, 20, 22, 23):
        check(baked[2][offset] == morphed[2][offset], "FX bake changed an unrelated effective live field")
    check(baked[2][9] == min(127, morphed[2][9] + 1), "FX bake did not change the requested effective corrosion value")

    # A held live note keeps MIDI ownership while the existing effect changes.
    chord = ["100 btn 8", "200 keys 0x4480", "500 hold 2 1", "1300 hold 2 0", "2200 keys 0", "3500 btn 9"]
    plain = run("held-corrosion-plain", chord)
    edited = run("held-corrosion-edit", chord + ["1500 enc 2 127"])
    check(plain[1] == edited[1], "FX knob retriggered or released held notes")
    check(plain[0] != edited[0], "Existing corrosion edit did not change held-note PCM")

    # Captured loops stay independent of changes to the live FX page.
    frozen = [sysex(50, 0x17, 1, (2, 0)), sysex(90, 0x10, 2, (2, 0, 1, 48, 100, 64, 0)),
              sysex(120, 0x13, 3, (1, 1)), sysex(200, 0x15, 4, (1,)),
              "300 hold 2 1", "1100 hold 2 0", "3500 btn 9"]
    plain = run("captured-fx-plain", frozen)
    edited = run("captured-fx-edit", frozen + ["1600 enc 2 127"])
    check(plain[0] == edited[0], "Live FX edit reshaped a frozen loop")
    check(plain[1] == edited[1], "Live FX edit changed captured-loop notes")
    print(f"PASS actual-main FX page: {checks} checks; release/hold, four engines, save ownership, GRAFT, held PCM and captured-loop isolation")


if __name__ == "__main__":
    main()
