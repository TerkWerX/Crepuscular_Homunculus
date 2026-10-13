#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual-main LCD traffic and optional before/after PCM, MIDI and screen parity.

Host pixels quantify transferred payload, not physical SPI latency or deadlines.
Pass --baseline with a frozen pre-change host executable for historical parity.
"""
from pathlib import Path
import argparse
import json
import os
import re
import subprocess

from check_remote import sysex


def execute(exe, folder, name, events, end=3500):
    script = folder / f"{name}.txt"
    script.write_text("\n".join(sorted(events, key=lambda line: int(line.split()[0])) + [f"{end} end"]) + "\n", encoding="ascii")
    prefix = folder / name
    env = dict(os.environ, TK_TRACE_LCD="1")
    result = subprocess.run([str(exe), str(prefix), str(script), "10"],
                            check=True, text=True, capture_output=True, env=env)
    (folder / f"{name}.log").write_text(result.stdout, encoding="utf-8")
    traffic = {int(time): int(pixels) for time, pixels in
               re.findall(r"(\d+) ms\s+LCD pixels (\d+)", result.stdout)}
    notes = re.findall(r"^.*MIDI out.*$", result.stdout, re.MULTILINE)
    return prefix, traffic, notes


def ppm_pixels(path):
    header, pixels = path.read_bytes().split(b"\n", 1)
    assert header == b"P6 240 240 255" and len(pixels) == 240 * 240 * 3
    return pixels


def historical_parity(before, after):
    assert before[0].with_suffix(".wav").read_bytes() == after[0].with_suffix(".wav").read_bytes(), "Display optimization changed PCM"
    assert before[2] == after[2], "Display optimization changed MIDI timestamps or notes"
    old_frames = sorted(path for path in before[0].parent.glob(before[0].name + "-*.ppm")
                        if re.fullmatch(re.escape(before[0].name) + r"-\d+\.ppm", path.name))
    new_frames = sorted(path for path in after[0].parent.glob(after[0].name + "-*.ppm")
                        if re.fullmatch(re.escape(after[0].name) + r"-\d+\.ppm", path.name))
    assert len(old_frames) == len(new_frames)
    corrections = 0
    for old, new in zip(old_frames, new_frames):
        first, second = ppm_pixels(old), ppm_pixels(new)
        if first == second:
            continue
        for at in range(240 * 240):
            if first[at * 3:at * 3 + 3] != second[at * 3:at * 3 + 3]:
                x, y = at % 240, at // 240
                # A final build may display its newer version. The old partial
                # last-cell paint also erased one pixel of the lane scar tip;
                # incremental/full same-state C checks require its restoration.
                if 144 <= x < 232 and 8 <= y < 24:
                    continue
                assert x == 225 and y in (95, 125, 155, 185), (old.name, x, y)
                corrections += 1
    return len(new_frames), corrections


def scenarios():
    setup = ["100 btn 11", sysex(150, 0x14, 1, (52, 1))]
    for track in range(4):
        setup += [sysex(170 + track * 10, 0x12, 2 + track, (track, 2))]
    setup += [sysex(220, 0x15, 6, (1,))]
    macro = setup + [sysex(time, 0x1E, 7 + i, (2, 1, 30 + i))
                     for i, time in enumerate(range(600, 2500, 60))]
    changes = setup + [sysex(450, 0x13, 7, (0, 1)), sysex(600, 0x11, 8, (2,)),
        sysex(700, 0x12, 9, (1, 1)), sysex(800, 0x20, 10, (3, 1, 0, 1, 36, 90, 3, 0)),
        sysex(850, 0x1F, 11, (3, 1, 1)), "900 btn 6", "950 enc 0 4",
        "1000 keys 0x4480", "1100 keys 0", "1200 btn 6",
        sysex(1300, 0x12, 12, (2, 2)), "1450 btn 11", "1700 btn 11",
        "2000 btn 7", "2250 btn 11", sysex(2500, 0x15, 13, (0,)),
        sysex(2700, 0x15, 14, (1,))]
    return {
        "glo-idle": ["100 btn 7"],
        "glo-voices-revisit": ["100 btn 7", "600 keys 0x4480", "1600 keys 0", "2250 btn 8", "2500 btn 7"],
        "seq-held-macros": macro,
        "seq-controls-revisit": changes,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--baseline", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    report = {}
    for name, events in scenarios().items():
        updated = execute(args.executable.resolve(), args.output.resolve(), name, events)
        item = {"updated_pixels_per_second": updated[1]}
        if args.baseline:
            previous = execute(args.baseline.resolve(), args.output.resolve(), name + "-before", events)
            frames, scars = historical_parity(previous, updated)
            item.update(before_pixels_per_second=previous[1], parity_frames=frames, restored_scar_pixels=scars)
        if name == "glo-idle":
            assert updated[1][2000] == 0 and updated[1][3000] == 0, "Idle GLO retransmits static artwork or values"
        if name == "seq-held-macros":
            assert updated[1][2000] < 20000 and updated[1][3000] < 20000, "Timbre edits caused complete SEQ page traffic"
        report[name] = item
    (args.output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print("PASS actual-main display traffic" + ("; historical PCM/MIDI/screen parity" if args.baseline else "") + "; no device timing claim")


if __name__ == "__main__":
    main()
