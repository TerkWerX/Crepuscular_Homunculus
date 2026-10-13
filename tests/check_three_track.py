#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise three-track recording, Ghost Mask and save migrations in the host app.

Build with tools/preview.py first, then run:
    python tests/check_three_track.py build/host/terkinstein.exe build/host/three-track

The first process records DRUM, MELODY and ATMOS from panel keys, selects and
mutes DRUM, and saves. Fresh processes verify schema migration, held-note
durations through bar wrap and re-recording, and playback of all three loops.
MIDI status bytes verify source ownership; screen strips verify the selected
and muted track. Host timing/audio is not a hardware qualification.
"""
from __future__ import annotations

import array
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import wave

from PIL import Image


ROOT = Path(__file__).resolve().parent.parent
RECORD = ROOT / "tests/scripts/three-track-recorder.txt"
RELOAD = ROOT / "tests/scripts/three-track-reload.txt"
MIGRATE = ROOT / "tests/scripts/three-track-migration.txt"
MIGRATE_SCHEMA4 = ROOT / "tests/scripts/three-track-schema4-migration.txt"
MIGRATE_SCHEMA5 = ROOT / "tests/scripts/three-track-schema5-migration.txt"
GHOST_MASK = ROOT / "tests/scripts/three-track-ghost-mask.txt"
GHOST_MASK_RELOAD = ROOT / "tests/scripts/three-track-ghost-mask-reload.txt"
DENSE = ROOT / "tests/scripts/three-track-dense.txt"
HELD_RECORD = ROOT / "tests/scripts/three-track-held-record.txt"
HELD_RELOAD = ROOT / "tests/scripts/three-track-held-reload.txt"
HELD_RERECORD = ROOT / "tests/scripts/three-track-held-rerecord.txt"
HELD_MUTE = ROOT / "tests/scripts/three-track-held-mute.txt"
HELD_DRUM = ROOT / "tests/scripts/three-track-held-drum.txt"
HELD_LAYERING = ROOT / "tests/scripts/three-track-held-layering.txt"
GENERATED_PREEMPT = ROOT / "tests/scripts/three-track-generated-preempt.txt"
LIVE_PREEMPT = ROOT / "tests/scripts/three-track-live-preempt.txt"
TEMPO_RETIME = ROOT / "tests/scripts/three-track-tempo-retime.txt"
TEMPO_RECORD = ROOT / "tests/scripts/three-track-record-tempo-change.txt"
TEMPO_RECORD_RELOAD = ROOT / "tests/scripts/three-track-record-tempo-reload.txt"
MIDI = re.compile(r"^\s*(\d+) ms  MIDI out ([0-9A-F]{2}) (\d+) (\d+)$")
SAVED = re.compile(r"^\s*(\d+) ms  settings saved \((\d+) B\)$", re.MULTILINE)
LCD = re.compile(r"^\s*(\d+) ms  LCD pixels (\d+)$", re.MULTILINE)
NOTE_ON = {"drum": 0x99, "melody": 0x91, "atmos": 0x92, "bass": 0x93}
NOTE_OFF = {"drum": 0x89, "melody": 0x81, "atmos": 0x82, "bass": 0x83}
PITCHES = {"drum": {48, 50}, "melody": {60, 64}, "atmos": {60, 64}, "bass": set()}
SCHEMA = 11
SCHEMA4_SIZE = 496
SCHEMA5_SIZE = 688
CURRENT_SIZE = 3700
STEPS = 64
TRACKS = 4
STEP_BYTES = 13
PATCH_AT = 4 + 20 + TRACKS * STEPS * STEP_BYTES
MASK_AT = PATCH_AT + TRACKS * 14 + 1
LENGTH_AT = MASK_AT + 1
SCHEMA4_STEP_BYTES = 9
SCHEMA5_STEP_BYTES = 13


def run_app(executable: Path, prefix: Path, script: Path, store: Path,
            fps: int) -> tuple[list[tuple[int, int, int, int]], str]:
    env = dict(os.environ, TK_HOST_SETTINGS_PATH=str(store), TK_TRACE_LCD="1")
    run = subprocess.run([str(executable), str(prefix), str(script), str(fps)],
                         cwd=ROOT, env=env, capture_output=True, text=True,
                         check=True)
    prefix.with_suffix(".log").write_text(run.stdout, encoding="utf-8")
    events = [tuple(int(v, 16) if i == 1 else int(v)
                    for i, v in enumerate(match.groups()))
              for line in run.stdout.splitlines() if (match := MIDI.match(line))]
    return events, run.stdout


def note_ons(events: list[tuple[int, int, int, int]], status: int,
             start: int, end: int) -> list[int]:
    return [pitch for ms, code, pitch, velocity in events
            if start <= ms < end and code == status and velocity > 0]


def check_tracks(events: list[tuple[int, int, int, int]],
                 start: int, end: int, label: str) -> None:
    for track, status in NOTE_ON.items():
        pitches = set(note_ons(events, status, start, end))
        assert pitches == PITCHES[track], (
            f"{label}: {track} expected {PITCHES[track]}, got {pitches}")


def check_note_pairs(events: list[tuple[int, int, int, int]]) -> None:
    active: dict[tuple[int, int], bool] = {}
    for ms, status, pitch, velocity in events:
        if status in NOTE_ON.values() and velocity:
            key = (status, pitch)
            assert not active.get(key), f"repeated generated note-on without note-off: {key} at {ms} ms"
            active[key] = True
        elif status in NOTE_OFF.values():
            key = (status | 0x10, pitch)
            assert active.get(key), f"stray generated note-off: {key} at {ms} ms"
            active[key] = False
    assert not any(active.values()), f"generated MIDI notes stuck after transport stop: {active}"


def generated_intervals(events: list[tuple[int, int, int, int]], on_status: int,
                        pitch: int) -> list[tuple[int, int]]:
    """Return completed generated MIDI note intervals for one track/pitch."""
    start: int | None = None
    pairs = []
    for ms, status, note, velocity in events:
        if note != pitch:
            continue
        if status == on_status and velocity:
            assert start is None, f"pitch {pitch} retriggered while held at {ms} ms"
            start = ms
        elif status == (on_status & ~0x10):
            assert start is not None, f"pitch {pitch} had a stray release at {ms} ms"
            pairs.append((start, ms))
            start = None
    assert start is None, f"pitch {pitch} was left on at the end of the host run"
    return pairs


def check_held_notes(executable: Path, prefix: Path, directory: Path) -> None:
    """Recorded key-up must control release, including after a bar wrap."""
    store = directory / "held.bin"
    captured = prefix.with_name(prefix.name + "-held-capture")
    _, output = run_app(executable, captured, HELD_RECORD, store, 0)
    assert len(SAVED.findall(output)) == 1, "held-note pattern did not save"

    replay = prefix.with_name(prefix.name + "-held-replay")
    events, _ = run_app(executable, replay, HELD_RELOAD, store, 0)
    check_note_pairs(events)
    for pitch in (48, 60, 64):
        intervals = generated_intervals(events, NOTE_ON["melody"], pitch)
        normal = [(on, off) for on, off in intervals if on < 5100 and off < 6100]
        assert len(normal) >= 2, f"held pitch {pitch} did not repeat across two bars: {intervals}"
        for on, off in normal:
            minimum = 450 if pitch == 48 else 600
            assert minimum <= off - on <= 1200, (
                f"held pitch {pitch} lost its recorded duration: {on}..{off} ms")
    assert active_peak(replay, 500, 800) > 10, (
        "the long held melody has no audible output after the former short gate")
    e_intervals = generated_intervals(events, NOTE_ON["melody"], 64)
    assert any(on < 2766 < off for on, off in e_intervals), (
        f"late-bar held note did not sustain across the 16-step wrap: {e_intervals}")
    check_schema5_migration(executable, prefix, store, directory / "schema5.bin")

    rerecord = prefix.with_name(prefix.name + "-held-rerecord")
    replaced, _ = run_app(executable, rerecord, HELD_RERECORD, store, 0)
    check_note_pairs(replaced)
    old = generated_intervals(replaced, NOTE_ON["melody"], 60)
    assert old and old[0][0] <= 102 and 498 <= old[0][1] <= 502, (
        f"REC did not release the previous held melody immediately: {old}")
    assert 60 not in note_ons(replaced, NOTE_ON["melody"], 501, 4400), (
        "re-recording replayed the old melody after clearing it")
    assert 67 in note_ons(replaced, NOTE_ON["melody"], 1700, 4400), (
        "re-recorded held melody never entered playback")

    muted = prefix.with_name(prefix.name + "-held-mute")
    stopped, _ = run_app(executable, muted, HELD_MUTE, store, 0)
    check_note_pairs(stopped)
    c_notes = generated_intervals(stopped, NOTE_ON["melody"], 60)
    assert len(c_notes) >= 2 and 498 <= c_notes[0][1] <= 502, (
        f"muting did not stop the held generated melody: {c_notes}")
    assert 2798 <= c_notes[1][1] <= 2802, (
        f"STOP did not stop the restarted held melody: {c_notes}")

    drum = prefix.with_name(prefix.name + "-held-drum")
    drum_events, _ = run_app(executable, drum, HELD_DRUM, directory / "drum.bin", 0)
    check_note_pairs(drum_events)
    kick = generated_intervals(drum_events, NOTE_ON["drum"], 48)
    assert kick and 80 <= kick[0][1] - kick[0][0] <= 180, (
        f"a held physical drum key changed its one-shot gate: {kick}")
    struck = active_peak(drum, kick[0][0] + 20, kick[0][0] + 220)
    decayed = active_peak(drum, kick[0][0] + 700, kick[0][0] + 950)
    assert struck > 10 and decayed < struck // 8, (
        f"held drum stopped sounding like a short one-shot: {struck}, {decayed}")


def check_seq_clock_and_lcd(events: list[tuple[int, int, int, int]], output: str) -> None:
    onsets = sorted(ms for ms, status, _, velocity in events
                    if 3200 <= ms < 6200 and status in NOTE_ON.values() and velocity)
    assert len(onsets) >= 6, f"too few combined loop onsets: {onsets}"
    for first, second in zip(onsets, onsets[1:]):
        gap = second - first
        assert min(abs(gap - 166 * steps) for steps in range(1, 17)) <= 2, (
            f"SEQ onset gap {gap} ms is not a 90 BPM sixteenth-note multiple")
    pixels = {int(ms): int(count) for ms, count in LCD.findall(output)}
    for second in (5000, 8000, 11000):
        count = pixels.get(second)
        assert count is not None and 0 < count < 12000, (
            f"SEQ playhead at {second} ms redrew too much LCD: {count} pixels")


def row_selected_count(prefix: Path, second: int, row: int) -> int:
    with Image.open(Path(f"{prefix}-{second:05d}.ppm")) as image:
        assert image.size == (240, 240)
        label = image.convert("RGB").crop((8, 70 + 30 * row, 80, 86 + 30 * row))
        pixels = label.tobytes()
        return sum(g > 180 and g > r * 5 // 4 and g > b * 5 // 4 for r, g, b in
                   zip(pixels[0::3], pixels[1::3], pixels[2::3]))


def muted_pixels(prefix: Path, second: int) -> int:
    with Image.open(Path(f"{prefix}-{second:05d}.ppm")) as image:
        strip = image.convert("RGB").crop((173, 70, 240, 86))
        pixels = strip.tobytes()
        return sum(r != 0 or g != 0 or b != 0 for r, g, b in
                   zip(pixels[0::3], pixels[1::3], pixels[2::3]))


def check_panel(prefix: Path) -> None:
    for second, selected in ((1, 0), (2, 1), (3, 2), (7, 0)):
        counts = [row_selected_count(prefix, second, row) for row in range(4)]
        assert counts[selected] > 10 and all(
            value == 0 for row, value in enumerate(counts) if row != selected
        ), f"SEQ track selection wrong at {second}s: selected green pixels {counts}"
    unmuted = muted_pixels(prefix, 5)
    muted = muted_pixels(prefix, 7)
    resumed = muted_pixels(prefix, 10)
    assert unmuted == 0 and muted > 10 and resumed == 0, (
        f"SEL did not toggle the DRUM mute label: {unmuted}, {muted}, {resumed}")


def active_peak(prefix: Path, start: int, end: int) -> int:
    with wave.open(str(prefix) + ".wav", "rb") as wav:
        assert wav.getnchannels() == 2 and wav.getsampwidth() == 2
        assert wav.getframerate() == 44100
        wav.setpos(start * 44100 // 1000)
        samples = array.array("h", wav.readframes((end - start) * 44100 // 1000))
    if sys.byteorder != "little":
        samples.byteswap()
    return max(map(abs, samples), default=0)


def legacy_schema3_record() -> bytes:
    """Build the shipped 164-byte schema-3 layout with a custom melody."""
    old = bytearray(164)
    struct.pack_into("<I", old, 0, 3)
    old[4:8] = bytes((70, 96, 18, 48))  # pad macros
    old[8:20] = bytes((255, 2, 1, 48, 90, 64, 0, 0, 0, 0, 64, 64))
    for step, pitch in ((0, 72), (4, 76)):
        offset = 20 + step * 9  # count + four (pitch, velocity) pairs
        old[offset:offset + 3] = bytes((1, pitch, 100))
    return struct.pack("<I", len(old)) + old


def check_legacy_migration(executable: Path, prefix: Path, store: Path) -> None:
    store.write_bytes(legacy_schema3_record())
    migrated = prefix.with_name(prefix.name + "-legacy")
    events, output = run_app(executable, migrated, MIGRATE, store, 0)
    assert set(note_ons(events, NOTE_ON["melody"], 100, 3100)) == {72, 76}, (
        "schema-3 melody was not moved to the MELODY track")
    assert not note_ons(events, NOTE_ON["drum"], 100, 3100)
    assert not note_ons(events, NOTE_ON["atmos"], 100, 3100)
    assert len(SAVED.findall(output)) == 1, "migrated settings did not save as schema 9"
    blob = store.read_bytes()
    assert len(blob) == 4 + CURRENT_SIZE and struct.unpack_from("<I", blob, 4)[0] == SCHEMA, (
        "migrated record was not upgraded to schema 9")
    assert blob[8:12] == bytes((70, 96, 18, 48)) and blob[12:16] == bytes((255, 2, 1, 48)), (
        "migration changed the legacy macros, octave, preset, FX or memory")
    check_note_pairs(events)

    replay = prefix.with_name(prefix.name + "-legacy-reload")
    loaded, _ = run_app(executable, replay, RELOAD, store, 0)
    assert set(note_ons(loaded, NOTE_ON["melody"], 100, 3400)) == {72, 76}, (
        "upgraded melody was lost after a fresh-process reload")
    assert not note_ons(loaded, NOTE_ON["drum"], 100, 3400)
    assert not note_ons(loaded, NOTE_ON["atmos"], 100, 3400)
    check_note_pairs(loaded)


def legacy_schema4_record(saved: Path) -> bytes:
    """Project a saved duration-capable pattern into the schema-4 layout."""
    modern = saved.read_bytes()
    assert struct.unpack_from("<I", modern)[0] == CURRENT_SIZE
    assert struct.unpack_from("<I", modern, 4)[0] == SCHEMA
    current = memoryview(modern)[4:]
    old = bytearray(SCHEMA4_SIZE)
    old[:20] = current[:20]
    struct.pack_into("<I", old, 0, 4)
    for index in range(3 * 16):
        old_at = 20 + index * SCHEMA4_STEP_BYTES
        new_at = 20 + ((index // 16) * STEPS + index % 16) * STEP_BYTES
        old[old_at] = current[new_at]
        for note in range(4):
            old[old_at + 1 + note * 2:old_at + 3 + note * 2] = (
                current[new_at + 1 + note * 3:new_at + 3 + note * 3])
    old[452:494] = current[PATCH_AT - 4:PATCH_AT - 4 + 42]
    old[494] = current[MASK_AT - 5]
    return struct.pack("<I", len(old)) + old


def check_schema4_migration(executable: Path, prefix: Path, saved: Path,
                            store: Path) -> None:
    store.write_bytes(legacy_schema4_record(saved))
    migrated = prefix.with_name(prefix.name + "-schema4")
    events, output = run_app(executable, migrated, MIGRATE_SCHEMA4, store, 0)
    check_tracks(events, 100, 3100, "schema-4 migration")
    check_note_pairs(events)
    assert len(SAVED.findall(output)) == 1, "schema-4 migration did not save"
    blob = store.read_bytes()
    assert len(blob) == 4 + CURRENT_SIZE and struct.unpack_from("<I", blob, 4)[0] == SCHEMA, (
        "schema-4 migration did not write schema 9")
    old_gate = generated_intervals(events, NOTE_ON["melody"], 60)
    assert old_gate and all(90 <= off - on <= 150 for on, off in old_gate), (
        f"schema-4 notes did not preserve their legacy 75% gate: {old_gate}")
    replay = prefix.with_name(prefix.name + "-schema4-reload")
    loaded, _ = run_app(executable, replay, RELOAD, store, 0)
    check_tracks(loaded, 100, 3400, "upgraded schema-4 reload")
    check_note_pairs(loaded)


def legacy_schema5_record(saved: Path) -> bytes:
    """Turn a saved held-note record into 0.7's exact schema-5 layout."""
    modern = saved.read_bytes()
    assert len(modern) == 4 + CURRENT_SIZE
    old = bytearray(4 + SCHEMA5_SIZE)
    struct.pack_into("<I", old, 0, SCHEMA5_SIZE)
    old[4:24] = modern[4:24]
    for track in range(3):
        old_at = 24 + track * 16 * SCHEMA5_STEP_BYTES
        new_at = 24 + track * STEPS * STEP_BYTES
        old[old_at:old_at + 16 * SCHEMA5_STEP_BYTES] = modern[new_at:new_at + 16 * STEP_BYTES]
    old[648:690] = modern[PATCH_AT:PATCH_AT + 42]
    old[690] = modern[MASK_AT - 1]
    struct.pack_into("<I", old, 4, 5)
    # The last byte was padding in schema 5. A nonzero value catches a
    # migration that accidentally interprets the padding as mask depth.
    old[-1] = 0x5A
    return bytes(old)


def normalized_legacy_kit(old: bytes) -> bytes:
    """Expected schema-5/6 migration: formerly ignored KIT macros become neutral.

    All notes, gate bytes, pitched patches and other KIT fields stay unchanged.
    """
    expected = bytearray(old)
    if expected[13] == 30:  # live SCRAP ALTAR
        expected[8:12] = bytes((64,) * 4)
    for track in range(3):
        at = 648 + track * 14
        if expected[at + 13] == 2:
            expected[at:at + 4] = bytes((64,) * 4)
    return bytes(expected)


def check_schema5_migration(executable: Path, prefix: Path, saved: Path,
                            store: Path) -> None:
    """Upgrade 0.7 loops without losing their recorded held-note gates."""
    old = legacy_schema5_record(saved)
    store.write_bytes(old)
    migrated = prefix.with_name(prefix.name + "-schema5")
    events, output = run_app(executable, migrated, MIGRATE_SCHEMA5, store, 0)
    check_note_pairs(events)
    for pitch in (60, 64):
        intervals = generated_intervals(events, NOTE_ON["melody"], pitch)
        assert len(intervals) >= 2 and all(600 <= off - on <= 1200
                                          for on, off in intervals[:2]), (
            f"schema-5 migration shortened held note {pitch}: {intervals}")
    assert len(SAVED.findall(output)) == 1, "schema-5 migration did not save"
    modern = store.read_bytes()
    assert len(modern) == 4 + CURRENT_SIZE and struct.unpack_from("<I", modern, 4)[0] == SCHEMA, (
        "schema-5 migration did not write schema 9")
    projected = legacy_schema5_record(store)
    assert projected[8:-1] == normalized_legacy_kit(old)[8:-1], (
        "schema-5 migration changed a saved patch, pattern or held-note duration")
    assert modern[MASK_AT] == 0, "schema-5 padding was mistaken for Ghost Mask depth"
    assert modern[LENGTH_AT:LENGTH_AT + 4] == bytes((16, 16, 16, 16))
    assert not any(modern[24 + 3 * STEPS * STEP_BYTES:PATCH_AT]), "migration populated BASS"

    replay = prefix.with_name(prefix.name + "-schema5-reload")
    loaded, _ = run_app(executable, replay, HELD_RELOAD, store, 0)
    check_note_pairs(loaded)
    held = generated_intervals(loaded, NOTE_ON["melody"], 60)
    assert len(held) >= 2 and all(600 <= off - on <= 1200 for on, off in held[:2]), (
        f"upgraded schema-5 held notes were lost after reload: {held}")


def seq_mask_pixels(prefix: Path, second: int) -> bytes:
    """Read only the SEQ parameter row, excluding its moving playhead."""
    with Image.open(Path(f"{prefix}-{second:05d}.ppm")) as screen:
        return screen.convert("RGB").crop((0, 191, 240, 215)).tobytes()


def check_ghost_mask_control(executable: Path, prefix: Path, store: Path) -> None:
    """SEQ Knob 4 reaches OFF and a depth, then SAVE persists the value."""
    edited = prefix.with_name(prefix.name + "-ghost-mask")
    events, output = run_app(executable, edited, GHOST_MASK, store, 1)
    check_note_pairs(events)
    off = seq_mask_pixels(edited, 1)
    on = seq_mask_pixels(edited, 2)
    assert on != off, "SEQ screen did not show the MASK depth change"
    assert seq_mask_pixels(edited, 3) == off, (
        "turning Knob 4 down to zero did not restore MASK OFF on the SEQ screen")
    assert seq_mask_pixels(edited, 4) == on, (
        "turning Knob 4 back up did not restore the previous MASK depth display")
    assert len(SAVED.findall(output)) == 1, "Ghost Mask depth did not save"
    blob = store.read_bytes()
    assert len(blob) == 4 + CURRENT_SIZE and struct.unpack_from("<I", blob, 4)[0] == SCHEMA
    assert blob[MASK_AT] == 64, f"SEQ Knob 4 did not save Ghost Mask depth 64: {blob[MASK_AT]}"

    # Keep one visible reference for manual UI inspection after the PPM cleanup.
    with Image.open(Path(f"{edited}-00002.ppm")) as screen:
        screen.resize((480, 480), Image.Resampling.NEAREST).save(
            edited.with_suffix(".png"))

    replay = prefix.with_name(prefix.name + "-ghost-mask-reload")
    loaded, _ = run_app(executable, replay, GHOST_MASK_RELOAD, store, 1)
    check_note_pairs(loaded)
    assert seq_mask_pixels(replay, 1) == on, (
        "reloaded Ghost Mask depth did not appear on the SEQ screen")


def check_dense_allocation(executable: Path, prefix: Path, saved: Path,
                           dense_store: Path) -> None:
    # The duration-capable layout retains the 20-byte schema-3 controls. Each track's
    # 64-step pattern follows as 64 * 13 bytes. Fill step 0 with four sorted
    # notes per track to stress the scarce generated-voice budget.
    blob = bytearray(saved.read_bytes())
    size = struct.unpack_from("<I", blob)[0]
    assert len(blob) == 4 + size and size == CURRENT_SIZE
    for track, pitches in enumerate(((48, 50, 52, 55),
                                     (60, 62, 64, 67),
                                     (72, 74, 76, 79))):
        offset = 4 + 20 + track * STEPS * STEP_BYTES
        blob[offset:offset + SCHEMA5_STEP_BYTES] = bytes((
            4, pitches[0], 100, 0, pitches[1], 100, 0,
            pitches[2], 100, 0, pitches[3], 100, 0))
    dense_store.write_bytes(blob)
    dense = prefix.with_name(prefix.name + "-dense")
    events, _ = run_app(executable, dense, DENSE, dense_store, 0)
    live = [(pitch, velocity) for ms, status, pitch, velocity in events
            if 10 <= ms < 100 and status == 0x90 and velocity]
    assert len(live) == 3, f"the held live triad was not played: {live}"
    generated = [(status, pitch) for ms, status, pitch, velocity in events
                 if ms == 100 and status in NOTE_ON.values() and velocity]
    assert sorted(generated) == sorted(((0x99, 48), (0x91, 60), (0x92, 72))), (
        f"dense step did not share three available voices across tracks: {generated}")
    assert 10 < active_peak(dense, 100, 300) < 32767, (
        "dense three-track step was silent or clipped")
    check_note_pairs(events)


def check_sustained_layering(executable: Path, prefix: Path, saved: Path,
                             layer_store: Path) -> None:
    """A held ATMOS note keeps its voice while later steps share the rest."""
    blob = bytearray(saved.read_bytes())
    assert struct.unpack_from("<I", blob)[0] == CURRENT_SIZE
    pattern_at = 4 + 20
    pattern_bytes = TRACKS * STEPS * STEP_BYTES
    blob[pattern_at:pattern_at + pattern_bytes] = bytes(pattern_bytes)

    def step(track: int, position: int,
             notes: list[tuple[int, int, int]]) -> None:
        assert len(notes) <= 4
        offset = pattern_at + (track * STEPS + position) * STEP_BYTES
        data = [len(notes)]
        for pitch, velocity, duration in notes:
            data.extend((pitch, velocity, duration))
        data.extend((0, 0, 0) * (4 - len(notes)))
        blob[offset:offset + SCHEMA5_STEP_BYTES] = bytes(data)

    step(2, 0, [(72, 95, 24)])  # six steps of atmosphere sustain
    step(0, 1, [(48, 95, 0), (50, 95, 0), (52, 95, 0), (55, 95, 0)])
    step(1, 1, [(60, 95, 0), (62, 95, 0), (64, 95, 0), (67, 95, 0)])
    step(0, 3, [(50, 95, 0)])
    step(1, 3, [(64, 95, 0)])
    layer_store.write_bytes(blob)
    layered = prefix.with_name(prefix.name + "-held-layering")
    events, _ = run_app(executable, layered, HELD_LAYERING, layer_store, 0)
    check_note_pairs(events)
    live = [(pitch, velocity) for ms, status, pitch, velocity in events
            if 10 <= ms < 100 and status == 0x90 and velocity]
    assert len(live) == 3, f"live chord did not reserve three voices: {live}"
    atmosphere = generated_intervals(events, NOTE_ON["atmos"], 72)
    assert len(atmosphere) == 1 and atmosphere[0][0] == 100 and 1090 <= atmosphere[0][1] <= 1100, (
        f"ATMOS did not sustain across later drum/melody steps: {atmosphere}")
    dense = [(status, pitch) for ms, status, pitch, velocity in events
             if 264 <= ms <= 268 and status in NOTE_ON.values() and velocity]
    assert sorted(dense) == sorted(((NOTE_ON["drum"], 48),
                                    (NOTE_ON["melody"], 60))), (
        f"dense step did not split its two free voices across DRUM/MELODY: {dense}")
    later = [(status, pitch) for ms, status, pitch, velocity in events
             if 596 <= ms <= 600 and status in NOTE_ON.values() and velocity]
    assert sorted(later) == sorted(((NOTE_ON["drum"], 50),
                                    (NOTE_ON["melody"], 64))), (
        f"sustained ATMOS stopped later DRUM/MELODY steps: {later}")
    assert 10 < active_peak(layered, 600, 900) < 32767, (
        "live chord and three loop tracks were silent or clipped")


def check_generated_preemption(executable: Path, prefix: Path, saved: Path,
                               directory: Path) -> None:
    """A later drum gets a voice from held generated notes, never live keys."""
    base = bytearray(saved.read_bytes())
    assert struct.unpack_from("<I", base)[0] == CURRENT_SIZE
    pattern_at = 4 + 20
    pattern_bytes = TRACKS * STEPS * STEP_BYTES

    def one_run(label: str, melody: tuple[int, ...], atmos: tuple[int, ...],
                script: Path, expected_on: set[tuple[int, int]],
                protect_live: bool) -> None:
        blob = bytearray(base)
        blob[pattern_at:pattern_at + pattern_bytes] = bytes(pattern_bytes)
        for track, step_number, pitches, duration in (
            (1, 0, melody, 48), (2, 0, atmos, 48),
            (0, 1, (48,), 0),
        ):
            at = pattern_at + (track * STEPS + step_number) * STEP_BYTES
            encoded = [len(pitches)]
            for pitch in pitches:
                encoded.extend((pitch, 100, duration))
            encoded.extend((0, 0, 0) * (4 - len(pitches)))
            blob[at:at + SCHEMA5_STEP_BYTES] = bytes(encoded)
        store = directory / f"{label}.bin"
        store.write_bytes(blob)
        scenario = prefix.with_name(prefix.name + "-" + label)
        events, _ = run_app(executable, scenario, script, store, 0)
        check_note_pairs(events)
        initial = {(status, pitch) for ms, status, pitch, velocity in events
                   if ms == 100 and status in NOTE_ON.values() and velocity}
        assert initial == expected_on, (
            f"{label}: expected saturated generated voices {expected_on}, got {initial}")
        preempted = [(status, pitch) for ms, status, pitch, _ in events
                     if 264 <= ms <= 268 and status in NOTE_OFF.values()]
        assert len(preempted) == 1 and (preempted[0][0] | 0x10, preempted[0][1]) in initial, (
            f"{label}: drum did not send a matching early off for a generated victim: {preempted}")
        drum = [(status, pitch) for ms, status, pitch, velocity in events
                if 264 <= ms <= 268 and status == NOTE_ON["drum"] and velocity]
        assert drum == [(NOTE_ON["drum"], 48)], (
            f"{label}: the later drum hit was starved: {drum}")
        if protect_live:
            live_ons = [(pitch, velocity) for ms, status, pitch, velocity in events
                        if 10 <= ms < 100 and status == 0x90 and velocity]
            live_offs = [(ms, pitch) for ms, status, pitch, _ in events
                         if status == 0x80]
            assert len(live_ons) == 3 and len(live_offs) == 3 and all(
                ms == 1300 for ms, _ in live_offs
            ), f"{label}: a held live key was displaced: {live_ons}, {live_offs}"

    one_run("generated-preempt", (60, 62, 64, 67), (72, 74),
            GENERATED_PREEMPT,
            {(NOTE_ON["melody"], pitch) for pitch in (60, 62, 64, 67)} |
            {(NOTE_ON["atmos"], pitch) for pitch in (72, 74)}, False)
    one_run("live-preempt", (60, 62), (72,), LIVE_PREEMPT,
            {(NOTE_ON["melody"], pitch) for pitch in (60, 62)} |
            {(NOTE_ON["atmos"], 72)}, True)

    # In a sorted step, note[0] may retrigger an already held pitch. That
    # retrigger must not consume the one preemption allowance for the track:
    # note[1] is its first *new* note and still needs a voice.
    blob = bytearray(base)
    blob[pattern_at:pattern_at + pattern_bytes] = bytes(pattern_bytes)
    for track, step_number, pitches in (
        (1, 0, (60, 62, 64, 67)),
        (2, 0, (72, 74)),
        (1, 1, (60, 65)),
    ):
        at = pattern_at + (track * STEPS + step_number) * STEP_BYTES
        encoded = [len(pitches)]
        for pitch in pitches:
            encoded.extend((pitch, 100, 48))
        encoded.extend((0, 0, 0) * (4 - len(pitches)))
        blob[at:at + SCHEMA5_STEP_BYTES] = bytes(encoded)
    store = directory / "duplicate-preempt.bin"
    store.write_bytes(blob)
    scenario = prefix.with_name(prefix.name + "-duplicate-preempt")
    events, _ = run_app(executable, scenario, GENERATED_PREEMPT, store, 0)
    check_note_pairs(events)
    at_step = [(status, pitch, velocity) for ms, status, pitch, velocity in events
               if 264 <= ms <= 268]
    assert at_step.count((NOTE_OFF["melody"], 60, 0)) == 1 and (
        NOTE_ON["melody"], 60, 100) in at_step, (
        f"duplicate held pitch was not retriggered normally: {at_step}")
    assert (NOTE_ON["melody"], 65, 100) in at_step, (
        f"the first new pitch after a duplicate was starved: {at_step}")
    victims = [(status, pitch) for status, pitch, _ in at_step
               if status in NOTE_OFF.values() and pitch != 60]
    initial = ({(NOTE_ON["melody"], pitch) for pitch in (60, 62, 64, 67)} |
               {(NOTE_ON["atmos"], pitch) for pitch in (72, 74)})
    assert len(victims) == 1 and (victims[0][0] | 0x10, victims[0][1]) in initial, (
        f"new pitch lacked a matching generated victim note-off: {at_step}")


def check_seq_tempo_retime(executable: Path, prefix: Path, saved: Path,
                           store: Path) -> None:
    """A BPM change scales both the pending onset and held-note release."""
    blob = bytearray(saved.read_bytes())
    assert struct.unpack_from("<I", blob)[0] == CURRENT_SIZE
    pattern_at = 4 + 20
    blob[pattern_at:pattern_at + TRACKS * STEPS * STEP_BYTES] = bytes(
        TRACKS * STEPS * STEP_BYTES)
    for position, pitch, duration in ((0, 60, 32), (3, 64, 0)):
        at = pattern_at + (1 * STEPS + position) * STEP_BYTES
        blob[at:at + SCHEMA5_STEP_BYTES] = bytes(
            (1, pitch, 100, duration) + (0, 0, 0) * 3)
    store.write_bytes(blob)
    retimed = prefix.with_name(prefix.name + "-tempo-retime")
    events, _ = run_app(executable, retimed, TEMPO_RETIME, store, 0)
    check_note_pairs(events)
    held = generated_intervals(events, NOTE_ON["melody"], 60)
    later = note_ons(events, NOTE_ON["melody"], 500, 600)
    later_on = [ms for ms, status, pitch, velocity in events
                if status == NOTE_ON["melody"] and pitch == 64 and velocity]
    assert held and held[0][0] == 100 and 945 <= held[0][1] <= 985, (
        f"SEQ tempo change did not scale the held gate's remaining time: {held}")
    assert later == [64] and len(later_on) == 1 and 540 <= later_on[0] <= 560, (
        f"SEQ tempo change did not retime the pending step: {later_on}")


def check_record_tempo_change(executable: Path, prefix: Path, store: Path) -> None:
    """REC integrates note length in step units across a live BPM change."""
    capture = prefix.with_name(prefix.name + "-tempo-capture")
    _, output = run_app(executable, capture, TEMPO_RECORD, store, 0)
    assert len(SAVED.findall(output)) == 1, "tempo-change recording did not save"
    blob = store.read_bytes()
    assert len(blob) == 4 + CURRENT_SIZE and struct.unpack_from("<I", blob, 4)[0] == SCHEMA
    melody_step0 = 4 + 20 + STEPS * STEP_BYTES
    count, pitch, velocity, duration_q4 = blob[melody_step0:melody_step0 + 4]
    assert (count, pitch, velocity) == (1, 60, 100), (
        f"tempo-change recording missed the held note: {count, pitch, velocity}")
    assert 25 <= duration_q4 <= 28, (
        f"REC used one tempo for the entire hold instead of accumulating both: {duration_q4} q4")

    replay = prefix.with_name(prefix.name + "-tempo-record-replay")
    events, _ = run_app(executable, replay, TEMPO_RECORD_RELOAD, store, 0)
    check_note_pairs(events)
    held = generated_intervals(events, NOTE_ON["melody"], 60)
    expected_ms = duration_q4 * (15000 // 180) // 4
    assert held and held[0][0] == 100 and abs((held[0][1] - held[0][0]) - expected_ms) <= 3, (
        f"reloaded tempo-change hold did not use its recorded length: {held}, {duration_q4} q4")


def main() -> None:
    import argparse

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path, help="compiled host app")
    parser.add_argument("prefix", type=Path, help="output prefix for log, WAV and screens")
    args = parser.parse_args()
    executable = args.executable.resolve()
    prefix = args.prefix.resolve()
    prefix.parent.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="tk-track-store-", dir=prefix.parent) as tmp:
        store = Path(tmp) / "settings.bin"
        events, output = run_app(executable, prefix, RECORD, store, 1)
        saved = SAVED.findall(output)
        assert len(saved) == 1, f"expected one completed flash save, got {saved}"
        assert int(saved[0][0]) >= 12000, "settings saved before the SAVE press"
        blob = store.read_bytes()
        size = struct.unpack_from("<I", blob)[0]
        assert size == CURRENT_SIZE and len(blob) == 4 + size, (
            "invalid saved host settings length")
        assert struct.unpack_from("<I", blob, 4)[0] == SCHEMA, (
            "saved record did not use schema 9")

        check_panel(prefix)
        check_tracks(events, 3200, 6200, "playing three recorded tracks")
        assert not note_ons(events, NOTE_ON["drum"], 6400, 9000), (
            "muted drum track still emitted notes")
        assert set(note_ons(events, NOTE_ON["melody"], 6400, 9000)) == PITCHES["melody"]
        assert set(note_ons(events, NOTE_ON["atmos"], 6400, 9000)) == PITCHES["atmos"]
        check_tracks(events, 9000, 12000, "after drum unmute")
        check_note_pairs(events)
        check_seq_clock_and_lcd(events, output)
        peak = active_peak(prefix, 3200, 6200)
        assert 10 < peak < 32767, f"three-track playback is silent or clipped (peak {peak})"
        peak = active_peak(prefix, 6400, 9000)
        assert 10 < peak < 32767, f"muting drums silenced or clipped the mix (peak {peak})"

        replay = prefix.with_name(prefix.name + "-reload")
        loaded_events, loaded_output = run_app(executable, replay, RELOAD, store, 0)
        assert not SAVED.search(loaded_output), "reload unexpectedly wrote settings"
        check_tracks(loaded_events, 100, 3400, "fresh-process settings reload")
        check_note_pairs(loaded_events)
        peak = active_peak(replay, 100, 3400)
        assert 10 < peak < 32767, f"reloaded loops are silent or clipped (peak {peak})"

        check_dense_allocation(executable, prefix, store, Path(tmp) / "dense.bin")
        check_sustained_layering(executable, prefix, store, Path(tmp) / "layered.bin")
        check_generated_preemption(executable, prefix, store, Path(tmp))
        check_seq_tempo_retime(executable, prefix, store, Path(tmp) / "tempo.bin")
        check_record_tempo_change(executable, prefix, Path(tmp) / "tempo-record.bin")
        check_legacy_migration(executable, prefix, Path(tmp) / "legacy.bin")
        check_schema4_migration(executable, prefix, store, Path(tmp) / "schema4.bin")
        check_held_notes(executable, prefix, Path(tmp))
        check_ghost_mask_control(executable, prefix, Path(tmp) / "ghost-mask.bin")

    # Keep one representative screen with the mute state visible for review.
    with Image.open(Path(f"{prefix}-00007.ppm")) as screen:
        screen.resize((480, 480), Image.Resampling.NEAREST).save(prefix.with_suffix(".png"))
    for path in prefix.parent.glob(prefix.name + "-*.ppm"):
        path.unlink()
    print("Three-track recording: distinct drum/melody/atmosphere MIDI channels and notes passed")
    print("SEQ panel: SELECT chooses tracks; SEL mute hides only the drum loop")
    print("Save/reload: schema-11 record and all three loops survive a fresh host process")
    print("Schema-5 held loops migrate with Ghost Mask OFF; SEQ Knob 4 depth survives SAVE/reload")
    print("Dense step: three held live notes leave one generated voice for each loop track")
    print("Layering: held ATMOS spans later steps while DRUM and MELODY split two free voices")
    print("Saturation: later drum and duplicate-then-new note preempt generated holds, preserving live keys")
    print("SEQ BPM: pending step and held gate rescale when tempo changes mid-note")
    print("REC BPM: held-note length integrates both tempos and survives save/reload")
    print("Schema migration: legacy schema-3 melody and schema-4 loops survive upgrade")
    print("Held notes: press-to-release, bar wrap, REC/mute/STOP release and drum one-shot pass")
    print(f"SEQ preview: {prefix}.png")


if __name__ == "__main__":
    main()
