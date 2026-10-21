#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise 16/32/64-step loops, BASS, long gates and 0.8 save migration.

Run after building the host app:
    py tests/check_long_tracks.py build/host/terkinstein.exe build/host/long-tracks
"""
from __future__ import annotations

import argparse
from pathlib import Path
import struct
import tempfile

from PIL import Image

import check_three_track as c
from check_remote import sysex


def step(blob: bytearray, track: int, position: int,
         notes: list[tuple[int, int, int]]) -> None:
    at = 24 + (track * c.STEPS + position) * c.STEP_BYTES
    data = [len(notes)]
    for pitch, velocity, gate in notes:
        data.extend((pitch, velocity, gate))
    data.extend([0, 0, 0] * (4 - len(notes)))
    blob[at:at + c.STEP_BYTES] = bytes(data)


def scenario(exe: Path, prefix: Path, directory: Path, label: str,
             blob: bytes, text: str, fps: int = 0):
    store = directory / f"{label}.bin"
    store.write_bytes(blob)
    script = directory / f"{label}.txt"
    script.write_text(text, encoding="ascii")
    out = prefix.with_name(prefix.name + "-" + label)
    events, log = c.run_app(exe, out, script, store, fps)
    c.check_note_pairs(events)
    return events, log, store, out


def playback(exe: Path, prefix: Path, directory: Path, base: bytearray) -> None:
    blob = bytearray(base)
    blob[c.LENGTH_AT:c.LENGTH_AT + 4] = bytes((16, 32, 64, 32))
    step(blob, 0, 0, [(48, 100, 0)])
    step(blob, 0, 4, [(50, 100, 0)])
    step(blob, 1, 20, [(60, 100, 0)])
    step(blob, 2, 0, [(72, 100, 255)])  # full 64 steps, not 63.75
    step(blob, 3, 0, [(36, 100, 32)])
    step(blob, 3, 24, [(40, 100, 0)])
    text = "100 btn 11\n100 btn 12\n16500 btn 12\n17000 btn 9\n25000 end\n"
    events, log, store, out = scenario(exe, prefix, directory, "mixed", blob, text, 1)
    expected = {"drum": (48, [100, 2100, 4100, 6100, 8100]),
                "melody": (60, [2600, 6600, 10600, 14600]),
                "atmos": (72, [100, 8100, 16100]),
                "bass": (36, [100, 4100, 8100, 12100, 16100])}
    for track, (pitch, wanted) in expected.items():
        onsets = [ms for ms, status, note, velocity in events
                  if status == c.NOTE_ON[track] and note == pitch and velocity]
        assert onsets[:len(wanted)] == wanted, (track, onsets, wanted)
    held = c.generated_intervals(events, c.NOTE_ON["atmos"], 72)
    assert held[:2] == [(100, 8100), (8100, 16100)], held
    assert 10 < c.active_peak(out, 2200, 2500) < 32767, "long ATMOS became silent or clipped"
    saved = store.read_bytes()
    assert saved == blob and len(c.SAVED.findall(log)) == 1, "four-track SAVE changed the song"

    # ATMOS's sixteen-step display page advances; DRUM's remains P1/1.
    with Image.open(Path(f"{out}-00007.ppm")) as screen:
        screen.resize((480, 480), Image.Resampling.NEAREST).save(out.with_suffix(".png"))
    with Image.open(Path(f"{out}-00001.ppm")) as first, Image.open(Path(f"{out}-00007.ppm")) as later:
        assert first.crop((132, 130, 164, 146)).tobytes() != later.crop((132, 130, 164, 146)).tobytes()
        assert first.crop((132, 70, 164, 86)).tobytes() == later.crop((132, 70, 164, 86)).tobytes()
    print("Four tracks: DRUM 16, MELODY 32, ATMOS 64 and BASS 32 repeat independently")
    print("ATMOS sustains for all 64 steps; display-page changes preserve note gates")

    mute_text = "100 btn 11\n100 btn 12\n6000 enc 0 2\n6100 btn 3\n10100 btn 3\n15000 btn 12\n16000 end\n"
    muted, _, _, _ = scenario(exe, prefix, directory, "bass-mute", blob, mute_text)
    assert not c.note_ons(muted, c.NOTE_ON["bass"], 6100, 10100)
    assert c.note_ons(muted, c.NOTE_ON["drum"], 6100, 10100)
    assert c.note_ons(muted, c.NOTE_ON["melody"], 6100, 10100)
    assert c.note_ons(muted, c.NOTE_ON["atmos"], 6100, 10100)
    assert c.note_ons(muted, c.NOTE_ON["bass"], 10100, 15000)
    print("BASS uses MIDI channel 4 and SEL mutes only that track")

    saturated = bytearray(base)
    for track, pitch in enumerate((48, 60, 72, 36)):
        step(saturated, track, 0, [(pitch, 100, 32 if track else 0)])
    dense, _, _, dense_out = scenario(exe, prefix, directory, "four-with-live", saturated,
        "10 keys 0x880\n100 btn 11\n100 btn 12\n1300 keys 0\n1500 btn 12\n5000 end\n")
    first = {(status, pitch) for ms, status, pitch, velocity in dense
             if ms == 100 and status in c.NOTE_ON.values() and velocity}
    assert first == {(0x99, 48), (0x91, 60), (0x92, 72), (0x93, 36)}, first
    assert all(ms == 1300 for ms, status, _, _ in dense if status == 0x80)
    assert 10 < c.active_peak(dense_out, 200, 500) < 32767
    print("Two live keys and one note from each of four tracks fit the shared voice pool")


def recordings(exe: Path, prefix: Path, directory: Path, base: bytearray) -> None:
    # At 120 BPM, step 2 starts at 350 ms and step 62 at 7850 ms.
    text = ("50 enc 6 -11\n100 btn 11\n100 btn 12\n180 enc 0 1\n200 enc 3 2\n300 btn 13\n"
            "400 midi 0x90 72 100\n600 enc 3 -2\n4500 midi 0x90 74 100\n"
            "4850 midi 0x80 74 0\n7900 midi 0x90 76 100\n8350 midi 0x80 76 0\n"
            "9000 midi 0x80 72 0\n9200 btn 13\n10000 btn 9\n20000 end\n")
    _, log, store, _ = scenario(exe, prefix, directory, "capture64", base, text)
    assert len(c.SAVED.findall(log)) == 1
    saved = store.read_bytes()
    assert saved[c.LENGTH_AT + 2] == 64, "REC did not lock the selected length"
    for position, pitch in ((2, 72), (35, 74), (62, 76)):
        at = 24 + (2 * c.STEPS + position) * c.STEP_BYTES
        assert saved[at:at + 3] == bytes((1, pitch, 100)), (position, saved[at:at + 4])
    at = 24 + (2 * c.STEPS + 2) * c.STEP_BYTES
    assert saved[at + 3] == 255, "64-step hold was truncated to the former 16-step cap"
    replay = "100 btn 11\n100 btn 12\n17000 btn 12\n18000 end\n"
    events, _, _, _ = scenario(exe, prefix, directory, "capture64-reload", saved, replay)
    held = c.generated_intervals(events, c.NOTE_ON["atmos"], 72)
    assert held[:2] == [(350, 8350), (8350, 16350)], held
    crossing = c.generated_intervals(events, c.NOTE_ON["atmos"], 76)
    assert crossing and crossing[0][0] < 8100 < crossing[0][1], crossing

    text32 = ("100 btn 11\n100 btn 12\n200 enc 3 1\n300 btn 13\n2700 midi 0x90 60 100\n"
              "3100 midi 0x80 60 0\n4000 midi 0x90 64 100\n4700 midi 0x80 64 0\n"
              "5000 btn 13\n5500 btn 9\n14000 end\n")
    _, _, store32, _ = scenario(exe, prefix, directory, "capture32", base, text32)
    saved32 = store32.read_bytes()
    assert saved32[c.LENGTH_AT + 1] == 32
    for position, pitch in ((20, 60), (31, 64)):
        at = 24 + (c.STEPS + position) * c.STEP_BYTES
        assert saved32[at:at + 3] == bytes((1, pitch, 100)), (position, saved32[at:at + 4])
    events32, _, _, _ = scenario(exe, prefix, directory, "capture32-reload", saved32,
                                 "100 btn 11\n100 btn 12\n8500 btn 12\n9000 end\n")
    crossing32 = c.generated_intervals(events32, c.NOTE_ON["melody"], 64)
    assert crossing32[0][0] < 4100 < crossing32[0][1], crossing32

    # Track navigation now preserves the auditioned sound. Choose the bass
    # preset explicitly before legacy REC captures that live patch.
    bass = (sysex(50, 0x16, 77, (18,)) + "\n100 btn 11\n100 btn 12\n180 enc 0 2\n200 enc 3 1\n300 btn 13\n"
            "400 midi 0x90 36 100\n900 midi 0x80 36 0\n1000 btn 13\n"
            "1500 btn 9\n10000 end\n")
    _, _, bass_store, _ = scenario(exe, prefix, directory, "bass-capture", base, bass)
    bass_saved = bass_store.read_bytes()
    assert bass_saved[c.LENGTH_AT + 3] == 32
    assert bass_saved[c.PATCH_AT + 3 * 14 + 4] == 18, "BASS did not capture the chosen TOMB CURRENT"
    bass_events, _, _, _ = scenario(exe, prefix, directory, "bass-reload", bass_saved,
                                     "100 btn 11\n100 btn 12\n5500 btn 12\n6000 end\n")
    bass_holds = c.generated_intervals(bass_events, c.NOTE_ON["bass"], 36)
    assert bass_holds == [(350, 850), (4350, 4850)], bass_holds
    print("Live REC: 32/64-step late notes, full-loop holds and wrap-crossing gates survive reload")
    print("BASS live capture preserves its patch, length and note durations")

    tempo = ("100 btn 11\n100 btn 12\n150 enc 0 1\n160 enc 3 2\n180 btn 13\n"
             "200 midi 0x90 72 100\n2200 enc 2 -60\n6200 midi 0x80 72 0\n"
             "6500 btn 13\n7000 btn 9\n16000 end\n")
    _, _, tempo_store, _ = scenario(exe, prefix, directory, "long-tempo", base, tempo)
    tempo_saved = tempo_store.read_bytes()
    assert tempo_saved[24 + 2 * c.STEPS * c.STEP_BYTES + 3] == 128
    tempo_events, _, _, _ = scenario(exe, prefix, directory, "long-tempo-reload", tempo_saved,
        "100 btn 11\n100 btn 12\n10000 btn 12\n12000 end\n")
    assert c.generated_intervals(tempo_events, c.NOTE_ON["atmos"], 72) == [(100, 8100)]
    print("Long REC gates retain musical duration across a tempo change")


def migration_and_resize(exe: Path, prefix: Path, directory: Path,
                         recorded: Path, base: bytearray) -> None:
    old = bytearray(c.legacy_schema5_record(recorded))
    struct.pack_into("<I", old, 4, 6)
    old[-1] = 64
    events, _, store, _ = scenario(exe, prefix, directory, "schema6", old,
                                    "100 btn 11\n100 btn 12\n3500 btn 9\n12000 end\n")
    saved = store.read_bytes()
    assert saved[c.MASK_AT] == 64, "0.8 Ghost Mask depth was lost in migration"
    assert saved[c.LENGTH_AT:c.LENGTH_AT + 4] == bytes((16, 16, 16, 16))
    projected_path = directory / "projected.bin"
    projected_path.write_bytes(saved)
    projected = c.legacy_schema5_record(projected_path)
    assert projected[8:-1] == c.normalized_legacy_kit(old)[8:-1], (
        "0.8 patterns or patches changed beyond formerly ignored KIT macros")
    c.check_tracks(events, 100, 3100, "0.8 migration")

    hidden = bytearray(base)
    step(hidden, 3, 60, [(36, 100, 16)])
    text = ("100 btn 11\n100 btn 12\n200 enc 0 2\n300 enc 3 1\n1200 enc 3 1\n"
            "2200 enc 3 -1\n3200 enc 3 -1\n4200 enc 3 2\n5000 btn 9\n13000 end\n")
    _, _, resized_store, out = scenario(exe, prefix, directory, "resize", hidden, text, 1)
    resized = resized_store.read_bytes()
    assert resized[c.LENGTH_AT:c.LENGTH_AT + 4] == bytes((16, 16, 16, 64))
    assert resized[24:c.PATCH_AT] == hidden[24:c.PATCH_AT], "shrinking erased hidden steps"
    assert resized[8:12] == hidden[8:12], (
        "SEQ navigation or length editing changed the auditioned sound macros")
    with Image.open(Path(f"{out}-00004.ppm")) as screen:
        screen.resize((480, 480), Image.Resampling.NEAREST).save(out.with_suffix(".png"))
    # Step editing reaches the fourth display page, including the last step.
    edit = ("100 btn 11\n100 btn 12\n150 enc 0 2\n200 enc 3 2\n300 btn 6\n400 enc 4 63\n"
            "600 keys 0x80\n650 keys 0\n1500 btn 9\n9000 end\n")
    _, _, edit_store, edit_out = scenario(exe, prefix, directory, "edit63", base, edit, 1)
    edit_saved = edit_store.read_bytes()
    at = 24 + (3 * c.STEPS + 63) * c.STEP_BYTES
    assert edit_saved[at] == 1, "step entry could not reach step 64"
    with Image.open(Path(f"{edit_out}-00001.ppm")) as screen:
        screen.resize((480, 480), Image.Resampling.NEAREST).save(edit_out.with_suffix(".png"))
    clamp = ("100 btn 11\n100 btn 12\n150 enc 0 2\n200 enc 3 2\n300 btn 6\n400 enc 4 -1\n"
             "500 btn 6\n600 enc 0 -2\n700 btn 6\n800 keys 0x80\n850 keys 0\n"
             "1200 btn 9\n9000 end\n")
    _, _, clamp_store, _ = scenario(exe, prefix, directory, "edit-clamp", base, clamp)
    clamp_saved = clamp_store.read_bytes()
    assert clamp_saved[24 + (c.STEPS + 15) * c.STEP_BYTES] == 1
    assert clamp_saved[24 + (c.STEPS + 63) * c.STEP_BYTES] == 0
    print("0.8 migration retains three loops and Ghost Mask, with empty BASS and 16-step defaults")
    print("Length knob works both ways, preserves hidden steps and leaves DRIFT alone")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("prefix", type=Path)
    args = parser.parse_args()
    exe, prefix = args.executable.resolve(), args.prefix.resolve()
    prefix.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="tk-long-", dir=prefix.parent) as tmp:
        directory = Path(tmp)
        recorded = directory / "recorded.bin"
        c.run_app(exe, prefix.with_name(prefix.name + "-seed"), c.RECORD, recorded, 0)
        base = bytearray(recorded.read_bytes())
        assert len(base) == c.CURRENT_SIZE + 4
        base[16] = 120  # BPM
        base[c.PATCH_AT + 2 * 14 + 8] = 32  # short release after the full ATMOS hold
        base[24:c.PATCH_AT] = bytes(c.TRACKS * c.STEPS * c.STEP_BYTES)
        base[c.MASK_AT - 1] = base[c.MASK_AT] = 0
        playback(exe, prefix, directory, base)
        recordings(exe, prefix, directory, base)
        migration_and_resize(exe, prefix, directory, recorded, base)
    for path in prefix.parent.glob(prefix.name + "-*.ppm"):
        path.unlink()
    print("PASS: long tracks and BASS integration")


if __name__ == "__main__":
    main()
