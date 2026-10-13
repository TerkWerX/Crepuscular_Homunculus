#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check the 0.5 ARP panel and generated MIDI against the host preview.

Build the host app with tools/preview.py, then run:

    python tests/check_arp_preview.py build/host/terkinstein.exe build/host/arp-controls

The checker drives the actual firmware app with a held triad. Image strips
verify Knob 3 RATE, Knob 4 GATE, and order transitions; channel-2 MIDI
timestamps verify timing, new order modes, and SEQ's fixed sixteenth clock.
"""
from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import re
import subprocess

from PIL import Image


ROOT = Path(__file__).resolve().parent.parent
SCRIPT = ROOT / "tests/scripts/arp-controls.txt"
RECLOCK_SCRIPT = ROOT / "tests/scripts/arp-reclock.txt"
FAST_SCRIPT = ROOT / "tests/scripts/arp-fast.txt"
MIDI = re.compile(r"^\s*(\d+) ms  MIDI out ([0-9A-F]{2}) (\d+) (\d+)$")
LCD = re.compile(r"^\s*(\d+) ms  LCD pixels (\d+)$")


def midi_events(output: str) -> list[tuple[int, int, int, int]]:
    events = []
    for line in output.splitlines():
        if match := MIDI.match(line):
            events.append(tuple(int(v, 16) if i == 1 else int(v)
                                for i, v in enumerate(match.groups())))
    return events


def lcd_pixels(output: str) -> dict[int, int]:
    return {int(match[1]): int(match[2]) for line in output.splitlines()
            if (match := LCD.match(line))}


def notes_in(events: list[tuple[int, int, int, int]], start: int, end: int,
             status: int = 0x91) -> list[tuple[int, int]]:
    return [(ms, pitch) for ms, code, pitch, velocity in events
            if start <= ms < end and code == status and velocity]


def note_durations(events: list[tuple[int, int, int, int]],
                   start: int, end: int) -> list[int]:
    pending: dict[int, int] = {}
    result = []
    for ms, code, pitch, velocity in events:
        if code == 0x91 and velocity:
            pending[pitch] = ms
        elif code == 0x81 and pitch in pending:
            onset = pending.pop(pitch)
            if start <= onset and ms < end:
                result.append(ms - onset)
    return result


def intervals(events: list[tuple[int, int, int, int]],
              start: int, end: int) -> list[int]:
    ons = notes_in(events, start, end)
    return [b[0] - a[0] for a, b in zip(ons, ons[1:])]


def near(values: list[int], expected: int, label: str, tolerance: int = 2) -> None:
    assert len(values) >= 2, f"{label}: too few pulses: {values}"
    assert all(abs(value - expected) <= tolerance for value in values), (
        f"{label}: expected about {expected} ms, got {values}")


def image_strip(prefix: Path, frame: int, box: tuple[int, int, int, int]) -> bytes:
    path = Path(f"{prefix}-{frame:05d}.ppm")
    with Image.open(path) as image:
        assert image.size == (240, 240), f"unexpected frame size: {path}"
        return hashlib.sha256(image.crop(box).tobytes()).digest()


def check_panels(prefix: Path) -> None:
    mode = [image_strip(prefix, n, (8, 68, 232, 100)) for n in range(1, 15)]
    rate = [image_strip(prefix, n, (168, 132, 232, 148)) for n in range(1, 15)]
    gate = [image_strip(prefix, n, (168, 155, 232, 171)) for n in range(1, 15)]
    assert len(set(mode[:3])) == 3, "UP, OUTSIDE IN and RANDOM screens did not differ"
    assert all(value == mode[2] for value in mode[3:]), "order changed while editing rate/gate"
    assert rate[:3] == [rate[0]] * 3, "order edits changed the initial 1/16 rate"
    assert len(set(rate[3:6])) == 3, "Knob 3 did not cycle through three rates"
    assert rate[5] == rate[0], "three Knob 3 detents did not wrap back to 1/16"
    assert all(value == rate[0] for value in rate[6:10]), "gate edit changed rate"
    assert rate[10] == rate[4] == rate[11], "reverse Knob 3 detent did not select 1/8"
    assert rate[12] == rate[13] == rate[0], "forward Knob 3 detent did not restore 1/16"
    assert gate[:6] == [gate[0]] * 6, "order/rate edits changed the initial 75% gate"
    assert len(set(gate[6:9])) == 3, "Knob 4 did not cycle through three other gates"
    assert gate[9] == gate[0] == gate[10], "four Knob 4 detents did not wrap back to 75%"
    assert gate[11] == gate[12] == gate[8], "reverse Knob 4 detent did not select 65%"
    assert gate[13] == gate[0], "forward Knob 4 detent did not restore 75%"
    assert image_strip(prefix, 15, (8, 48, 232, 100)) != image_strip(
        prefix, 14, (8, 48, 232, 100)), "SEQ did not open after ARP edits"
    print("ARP screen: orders, forward/reverse Knob 3 and 4 edits, and SEQ passed")


def check_midi(events: list[tuple[int, int, int, int]]) -> None:
    first = notes_in(events, 500, 1400)
    pitches = sorted({pitch for _, pitch in first})
    assert len(pitches) == 3, f"held triad missing pitches: {first}"
    # The next scheduled pulse follows the 1500 ms mode edit.
    outside = notes_in(events, 1500, 2450)
    assert len(outside) >= 3, f"OUTSIDE IN emitted too few notes: {outside}"
    assert [pitch for _, pitch in outside[:3]] == [pitches[0], pitches[2], pitches[1]], (
        f"OUTSIDE IN did not start low/high/middle: {outside[:3]}")
    random = notes_in(events, 2500, 3450)
    assert len(random) >= 3, f"RANDOM emitted too few notes: {random}"
    assert all(a[1] != b[1] for a, b in zip(random, random[1:])), (
        f"RANDOM immediately repeated a held pitch: {random}")
    assert {pitch for _, pitch in random} <= set(pitches), "RANDOM emitted an unheld pitch"

    near(intervals(events, 700, 1400), 166, "legacy 1/16")
    near(intervals(events, 3700, 4400), 83, "edited 1/32")
    near(intervals(events, 4500, 5500), 333, "edited 1/8")
    near(intervals(events, 5700, 6400), 166, "wrapped 1/16")
    near(note_durations(events, 5700, 6400), 124, "75% gate")
    near(note_durations(events, 6700, 7400), 41, "25% gate")
    near(note_durations(events, 7700, 8400), 83, "50% gate")
    near(note_durations(events, 8700, 9400), 107, "65% gate")
    near(note_durations(events, 9700, 10470), 124, "wrapped 75% gate")
    near(intervals(events, 10500, 11500), 333, "reverse Knob 3 to 1/8")
    near(note_durations(events, 11500, 12500), 216, "reverse Knob 4 to 65% at 1/8")
    near(intervals(events, 12500, 13500), 166, "forward Knob 3 to 1/16")
    near(note_durations(events, 13500, 14500), 124, "forward Knob 4 to 75%")

    seq = notes_in(events, 14500, 16200)
    assert len(seq) >= 4, f"seeded SEQ emitted too few notes: {seq}"
    # The seeded loop has notes on steps 0, 2, 4, 6 and 7. Even after editing
    # the ARP rate to 1/8 and back, SEQ retains its 1/16 step spacing.
    gaps = [b[0] - a[0] for a, b in zip(seq, seq[1:])]
    assert all(abs(gap - 333) <= 2 for gap in gaps[:3]), (
        f"SEQ's two-step note gaps changed: {gaps}")
    assert any(abs(gap - 166) <= 2 for gap in gaps[3:]), (
        f"SEQ's adjacent steps no longer use 1/16 timing: {gaps}")
    print("ARP MIDI: order, nonrepeating random, 1/8 to 1/32 rates, gate lengths and SEQ clock passed")


def check_macro_invariance(executable: Path, prefix: Path) -> None:
    """Turning RATE/GATE on ARP must leave the pad's DRIFT/BLOOM values alone."""
    script = prefix.with_suffix(".txt")
    script.write_text(
        "100 btn 8\n200 btn 6\n300 hold 10 1\n1000 hold 10 0\n1300 btn 10\n"
        "1500 enc 4 1\n1700 enc 5 1\n2400 btn 10\n2500 btn 8\n2600 btn 6\n3300 end\n", encoding="ascii")
    subprocess.run([str(executable), str(prefix), str(script), "1"],
                   cwd=ROOT, check=True, capture_output=True, text=True)
    # Rows 3 and 4 of SOUND EDIT hold DRIFT and BLOOM labels, bars and numbers.
    macro_box = (8, 116, 232, 152)
    assert image_strip(prefix, 1, macro_box) == image_strip(prefix, 3, macro_box), (
        "ARP Knob 3/4 edit altered DRIFT or BLOOM")
    for ppm in prefix.parent.glob(prefix.name + "-*.ppm"):
        ppm.unlink()
    print("ARP macros: Knob 3/4 leave DRIFT and BLOOM unchanged")


def check_reclock(executable: Path, prefix: Path) -> None:
    env = dict(os.environ, TK_TRACE_LCD="1")
    run = subprocess.run([str(executable), str(prefix), str(RECLOCK_SCRIPT), "0"],
                         cwd=ROOT, env=env, check=True, capture_output=True, text=True)
    prefix.with_suffix(".log").write_text(run.stdout, encoding="utf-8")
    events = midi_events(run.stdout)
    lcd = lcd_pixels(run.stdout)
    assert lcd[3000] < 5000, (
        f"steady ARP page sent {lcd[3000]} LCD pixels during a quiet second")
    slow = intervals(events, 1200, 3350)
    near(slow, 750, "slow 40 BPM 1/8")
    fast = notes_in(events, 3350, 4500)
    assert fast, "no notes after live rate change"
    assert 0 < fast[0][0] - 3350 <= 380, (
        f"1/8 to 1/16 edit waited longer than one new period: {fast[0][0] - 3350} ms")
    near(intervals(events, fast[0][0], 4500), 375, "new 40 BPM 1/16")
    print("ARP MIDI: live rate edit adopts the new period by the next pulse")
    print("ARP LCD: quiet playing does not cause full page redraws")


def check_fast(executable: Path, prefix: Path) -> None:
    env = dict(os.environ, TK_TRACE_LCD="1")
    run = subprocess.run([str(executable), str(prefix), str(FAST_SCRIPT), "0"],
                         cwd=ROOT, env=env, check=True, capture_output=True, text=True)
    prefix.with_suffix(".log").write_text(run.stdout, encoding="utf-8")
    events = midi_events(run.stdout)
    near(intervals(events, 900, 2500), 41, "180 BPM 1/32", tolerance=1)
    near(note_durations(events, 900, 2500), 10, "25% fast gate", tolerance=1)
    lcd = lcd_pixels(run.stdout)
    assert lcd[2000] < 5000, (
        f"fast ARP page sent {lcd[2000]} LCD pixels during a quiet second")
    print("ARP fast clock: 41 ms pulses, 10 ms gates and no per-pulse screen redraw")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path, help="compiled host app")
    parser.add_argument("prefix", type=Path, help="output prefix for WAV and frames")
    args = parser.parse_args()
    executable = args.executable.resolve()
    prefix = args.prefix.resolve()
    prefix.parent.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, TK_TRACE_LCD="1")
    run = subprocess.run([str(executable), str(prefix), str(SCRIPT), "1"],
                         cwd=ROOT, env=env, check=True, capture_output=True, text=True)
    prefix.with_suffix(".log").write_text(run.stdout, encoding="utf-8")
    events = midi_events(run.stdout)
    check_panels(prefix)
    check_midi(events)
    check_macro_invariance(executable, prefix.with_name(prefix.name + "-macros"))
    lcd = lcd_pixels(run.stdout)
    for second in range(2000, 14001, 1000):
        assert lcd[second] < 12000, (
            f"ARP control edit sent {lcd[second]} LCD pixels by {second} ms")
    print("ARP LCD: order, rate and gate edits use small updates")
    check_reclock(executable, prefix.with_name(prefix.name + "-reclock"))
    check_fast(executable, prefix.with_name(prefix.name + "-fast"))
    with Image.open(Path(f"{prefix}-00007.ppm")) as frame:
        frame.resize((480, 480), Image.Resampling.NEAREST).save(prefix.with_suffix(".png"))
    for ppm in prefix.parent.glob(prefix.name + "-*.ppm"):
        ppm.unlink()
    print(f"ARP preview: {prefix}.png")


if __name__ == "__main__":
    main()
