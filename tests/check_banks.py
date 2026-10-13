#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual main-loop bank/song tests through the USB collector; offline only."""
from pathlib import Path
import re
import sys
from check_remote import answer, run, sysex


def main():
    exe = Path(sys.argv[1]).resolve()
    folder = Path(sys.argv[2]).resolve()
    folder.mkdir(parents=True, exist_ok=True)
    baseline = (Path(__file__).parent / "scripts/home-navigation.txt").read_text().splitlines()
    baseline = [line for line in baseline if line and not line.startswith("#") and " end" not in line]
    queries = baseline + [sysex(200, 7, 1), sysex(400, 8, 2, (1, 0, 0)),
                          sysex(1200, 9, 3, (2,)), sysex(6200, 7, 4)]
    queries.sort(key=lambda line: int(line.split()[0]))
    plain, plain_log, _ = run(exe, folder, "bank-query-baseline", baseline, 8500)
    queried, queried_log, replies = run(exe, folder, "bank-query-navigation", queries, 8500)
    assert plain.with_suffix(".wav").read_bytes() == queried.with_suffix(".wav").read_bytes(), "Read-only bank/full-patch queries changed PCM"
    notes = re.compile(r"^.*MIDI out.*$", re.MULTILINE)
    assert notes.findall(plain_log) == notes.findall(queried_log), "Read-only queries changed generated notes"
    for time in (1000, 4200, 8200):
        assert Path(f"{plain}-{time}.ppm").read_bytes() == Path(f"{queried}-{time}.ppm").read_bytes()
    assert len(answer(replies, 1)) == len(answer(replies, 4)) == 73
    assert len(answer(replies, 3)) == 56

    # A16->B64 switches at the DRUM boundary. The B17 clap is deliberately
    # different from its B1 snare so beginning at the wrong phase is audible.
    _, log, replies = run(exe, folder, "local-bank-boundary", [
        sysex(50, 0x23, 1, (1, 0)), sysex(100, 0x20, 2, (0, 0, 0, 1, 36, 100, 0, 0)),
        sysex(150, 0x20, 3, (0, 1, 0, 1, 38, 100, 0, 0)),
        sysex(200, 0x20, 4, (0, 1, 16, 1, 41, 100, 0, 0)),
        sysex(250, 0x21, 5, (0, 1, 2)), sysex(300, 0x15, 6, (1,)),
        sysex(700, 0x1F, 7, (0, 1)), sysex(800, 7, 8),
        sysex(3000, 7, 9), sysex(3150, 7, 10), sysex(3250, 0x15, 11, (0,)),
        sysex(3300, 8, 12, (0, 0, 0)), sysex(3350, 8, 13, (0, 1, 0)),
    ], 3500)
    pending = answer(replies, 8)
    assert pending[5] == 0 and pending[9] == 1, "Queued bank was announced active before loop boundary"
    committed = answer(replies, 10)
    assert committed[5] == 1 and committed[9] == 4, "Bank did not commit at its local boundary"
    drum_notes = [(int(t), int(pitch)) for t, pitch in re.findall(r"(\d+) ms\s+MIDI out 99 (\d+) \d+", log)]
    assert drum_notes[0][1] == 36 and drum_notes[1][1] == 38 and not any(pitch == 41 for _, pitch in drum_notes), "New B64 played step17 instead of step1"
    assert answer(replies, 12)[5] == 36 and answer(replies, 13)[5] == 38, "Switch lost outgoing/incoming stored notes"

    # Import isolation must include actual panel handlers and ordinary MIDI
    # transport, rather than only command helpers. Live keys remain playable.
    _, log, replies = run(exe, folder, "import-panel-and-midi-guards", [
        "50 btn 11", "100 btn 6", sysex(150, 2, 1), sysex(200, 8, 2, (1, 0, 0)),
        sysex(250, 0x24, 3), sysex(300, 0x25, 4, (1, 0, 0, 1, 70, 99, 4, 0)),
        "350 keys 1", "400 keys 0", "450 enc 0 15", "500 enc 1 1", "550 enc 4 2",
        "600 btn 13", "650 btn 12", "700 midi 0xFA 0 0", "750 midi 0xFB 0 0",
        sysex(800, 2, 5), sysex(850, 8, 6, (1, 0, 0)), sysex(900, 0x2A, 7),
        sysex(950, 8, 8, (1, 0, 0)), sysex(1000, 2, 9),
    ], 1200)
    before, during, after = answer(replies, 1), answer(replies, 5), answer(replies, 9)
    assert before[4] == during[4] == after[4] == 1, "Panel track navigation escaped import lock"
    assert before[7:19] == during[7:19] == after[7:19], "Panel tempo/length/patch changed during staged import"
    assert not during[3] & 3 and not after[3] & 3, "Panel/MIDI start or REC escaped import lock"
    assert answer(replies, 2) == answer(replies, 6) == answer(replies, 8), "Partial import or live-key step entry changed current pattern before ABORT"
    assert "MIDI out 90" in log, "Import lock silenced ordinary live panel playing"

    # A valid staged bank is published once, remains stopped, and a repeated
    # exact COMMIT request is replayed by the real USB protocol service.
    _, _, replies = run(exe, folder, "import-commit-and-retry", [
        sysex(50, 0x24, 1), sysex(100, 0x25, 2, (3, 2, 63, 1, 24, 120, 127, 1)),
        sysex(150, 0x26, 3, (3, 2, 2)), sysex(200, 0x28, 4, (2, 1, 0, 3, 0, 0, 0, 2, 127, 1)),
        sysex(250, 0x29, 5), sysex(300, 0x29, 5), sysex(350, 7, 6), sysex(400, 2, 7),
        sysex(450, 8, 8, (3, 2, 63)),
    ], 650)
    assert len(replies[5]) == 2 and replies[5][0][2] == replies[5][1][2]
    assert answer(replies, 6)[8] == 2 and not answer(replies, 6)[2]
    assert not answer(replies, 7)[3] & 3 and answer(replies, 7)[7:9] == bytes((2, 1))
    assert answer(replies, 8)[5:] == bytes((24, 120, 127, 1))
    print("PASS actual main-loop banks: read-only PCM/MIDI/screens, local16-to64 boundary, hidden notes, panel/MIDI/import guards, atomic stopped COMMIT and retry")


if __name__ == "__main__":
    main()
