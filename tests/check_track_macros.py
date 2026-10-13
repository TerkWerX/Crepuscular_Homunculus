#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Captured track controls through actual app and USB SysEx collector; offline."""
from pathlib import Path
import re
import sys

from check_remote import answer, run, sysex


def main():
    exe = Path(sys.argv[1]).resolve()
    folder = Path(sys.argv[2]).resolve()
    folder.mkdir(parents=True, exist_ok=True)

    # A full-loop ATMOS gate remains the same MIDI event while its captured
    # sound changes. Queries and a same-value write cannot alter PCM or notes.
    setup = [sysex(50, 0x17, 1, (2, 3)), sysex(100, 0x12, 2, (2, 2)),
             sysex(150, 0x10, 3, (2, 0, 1, 48, 110, 127, 1)),
             sysex(200, 0x13, 4, (1, 1)), sysex(250, 0x15, 5, (1,))]
    plain, plain_log, _ = run(exe, folder, "held-track-baseline", setup, 4500)
    queries = setup + [sysex(800, 0x06, 6, (2,)), sysex(850, 0x02, 7)]
    queried, queried_log, replies = run(exe, folder, "held-track-query", queries, 4500)
    before = answer(replies, 6)
    assert len(before) == 6 and before[1] == 2
    assert plain.with_suffix(".wav").read_bytes() == queried.with_suffix(".wav").read_bytes(), "GET_TRACK_MACROS changed sound"
    notes = re.compile(r"^.*MIDI out.*$", re.MULTILINE)
    assert notes.findall(plain_log) == notes.findall(queried_log), "GET_TRACK_MACROS changed held notes"
    same = queries + [sysex(1000, 0x1E, 8, (2, 1, before[3])), sysex(1100, 0x02, 9)]
    unchanged, unchanged_log, same_replies = run(exe, folder, "held-track-same-value", same, 4500)
    assert answer(same_replies, 8) == before
    assert plain.with_suffix(".wav").read_bytes() == unchanged.with_suffix(".wav").read_bytes(), "Same captured macro value changed PCM"
    assert answer(same_replies, 7)[30:32] == answer(same_replies, 9)[30:32], "Same macro value dirtied settings"

    changed_value = 127 if before[3] != 127 else 0
    edits = queries + [sysex(1000, 0x1E, 8, (2, 1, changed_value)),
                       sysex(1100, 0x06, 9, (2,)), sysex(1150, 0x02, 10)]
    edited, edited_log, replies = run(exe, folder, "held-track-edit", edits, 4500)
    accepted = answer(replies, 8)
    expected = bytearray(before)
    expected[3] = changed_value
    assert accepted == answer(replies, 9) == expected, "Captured macro write did not report accepted values"
    assert notes.findall(plain_log) == notes.findall(edited_log), "Captured macro edit retriggered or released a held note"
    assert plain.with_suffix(".wav").read_bytes() != edited.with_suffix(".wav").read_bytes(), "Held captured macro edit did not change actual app sound"
    initial, final = answer(replies, 7), answer(replies, 10)
    assert initial[10:30] == final[10:30], "Track macro edit changed global live sound or another persistent control"
    assert initial[30:32] != final[30:32] and initial[77:79] == final[77:79], "Captured edit invalidated unrelated note-cache revision"

    # Station REC keeps that track's patch even when the live sound is FM.
    # The DRUM case also proves it no longer forces the live kit preset.
    _, _, replies = run(exe, folder, "station-record-patch-ownership", [
        sysex(50, 0x16, 1, (32,)), sysex(100, 0x1B, 2, (0, 0, 31)),
        sysex(150, 0x17, 3, (0, 30)), sysex(200, 0x1E, 4, (0, 0, 11)),
        sysex(250, 0x06, 5, (0,)), sysex(300, 0x04, 6, (0,)),
        sysex(350, 0x02, 7), sysex(400, 0x1A, 8, (0, 1, 1)),
        sysex(450, 0x06, 9, (0,)), sysex(500, 0x04, 10, (0,)),
        sysex(550, 0x02, 11), sysex(600, 0x1E, 12, (0, 1, 41)),
        sysex(650, 0x1E, 13, (3, 1, 42)), sysex(700, 0x1A, 14, (0, 0)),
        sysex(750, 0x1A, 15, (0, 1, 2)), sysex(800, 0x02, 16),
    ], 1000)
    assert answer(replies, 5) == answer(replies, 9), "Station REC replaced captured macros"
    assert answer(replies, 6) == answer(replies, 10), "Station REC changed live FM operators"
    initial, recording = answer(replies, 7), answer(replies, 11)
    assert recording[3] & 2 and recording[4] == 0
    assert initial[10:30] == recording[10:30], "Preserved DRUM REC replaced a captured or live preset"
    answer(replies, 12, 3)
    other = answer(replies, 13)
    assert other[1] == 3 and other[3] == 42, "Other station's macro edit was blocked or misrouted during REC"
    answer(replies, 15, 2)
    assert not answer(replies, 16)[3] & 2, "Invalid preserve flag started recording"
    print("PASS actual app captured macros: query/same-value PCM, held gate continuity, track isolation, accepted values, revisions, preserved station REC and guards")


if __name__ == "__main__":
    main()
