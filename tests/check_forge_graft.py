#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise SCRAP FORGE and GRAFT through the unchanged desktop FM-1 panel.

    py tests/check_forge_graft.py build/host/terkinstein.exe build/host/forge-graft

Checks actual app controls, audio, frozen recording patches, settings reload and
schema-7 migration. Host audio and timing are not hardware qualification.
"""
from __future__ import annotations

import argparse
import array
import hashlib
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import wave

from PIL import Image


ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from preset_catalog import PRESET_INSTRUMENTS
SCHEMA = 12
SIZE = 3700
LEGACY_SIZE = 3412
PATCH_AT = 3352                         # host file includes the 4-byte length
LENGTH_AT = 3410
GRAFT_BASE_AT = 3414
TARGET_AT = 3428
AMOUNT_AT = 3429
KIT = 30
PAD = {slot for slot, engine in enumerate(PRESET_INSTRUMENTS) if engine == 0}
METAL = {slot for slot, engine in enumerate(PRESET_INSTRUMENTS) if engine == 1}
SAVED = re.compile(r"^\s*(\d+) ms  settings saved \((\d+) B\)$", re.MULTILINE)
MIDI = re.compile(r"^\s*(\d+) ms  MIDI out ([0-9A-F]{2}) (\d+) (\d+)$", re.MULTILINE)


def instrument(slot: int) -> int:
    return PRESET_INSTRUMENTS[slot]


def next_target(source: int) -> int:
    slots = PAD if source in PAD else METAL
    return next((source + offset) % len(PRESET_INSTRUMENTS)
                for offset in range(1, len(PRESET_INSTRUMENTS))
                if (source + offset) % len(PRESET_INSTRUMENTS) in slots)


def patch(blob: bytes) -> bytes:
    """Pack the effective live settings in the same order as a track patch."""
    return (blob[8:12] + bytes((blob[13], blob[14], blob[15], blob[22],
            blob[23], blob[20], blob[19], blob[17], blob[18],
            instrument(blob[13]))))


def read_settings(path: Path) -> bytes:
    blob = path.read_bytes()
    assert len(blob) == SIZE + 4 and struct.unpack_from("<II", blob) == (SIZE, SCHEMA), (
        f"wrong current record: {len(blob)}, {struct.unpack_from('<II', blob)}")
    return blob


def pcm(prefix: Path, start: int = 0, end: int | None = None) -> bytes:
    with wave.open(str(prefix) + ".wav", "rb") as wav:
        assert (wav.getnchannels(), wav.getsampwidth(), wav.getframerate()) == (2, 2, 44100)
        wav.setpos(start * 44100 // 1000)
        frames = wav.getnframes() - wav.tell() if end is None else (end - start) * 44100 // 1000
        return wav.readframes(frames)


def peak(prefix: Path, start: int = 0, end: int | None = None) -> int:
    samples = array.array("h", pcm(prefix, start, end))
    if sys.byteorder != "little":
        samples.byteswap()
    return max(map(abs, samples), default=0)


class Panel:
    def __init__(self, exe: Path, prefix: Path):
        self.exe = exe.resolve()
        self.prefix = prefix.resolve()
        self.directory = self.prefix.parent / (self.prefix.name + "-fixtures")
        self.directory.mkdir(parents=True, exist_ok=True)

    def run(self, label: str, text: str, initial: bytes | None = None,
            fps: int = 0, *, saved: bool = True):
        script = self.directory / (label + ".txt")
        script.write_text(text, encoding="ascii")
        store = self.directory / (label + ".bin")
        if initial is not None:
            store.write_bytes(initial)
        elif store.exists():
            store.unlink()
        out = self.prefix.with_name(self.prefix.name + "-" + label)
        env = dict(os.environ, TK_HOST_SETTINGS_PATH=str(store))
        result = subprocess.run([str(self.exe), str(out), str(script), str(fps)],
                                cwd=ROOT, env=env, capture_output=True,
                                text=True, check=True)
        out.with_suffix(".log").write_text(result.stdout, encoding="utf-8")
        if saved:
            writes = SAVED.findall(result.stdout)
            assert len(writes) == 1 and int(writes[0][1]) == SIZE, (label, writes)
            blob = read_settings(store)
        else:
            assert not SAVED.search(result.stdout), (label, "unexpected SAVE")
            blob = store.read_bytes() if store.exists() else b""
        return blob, out, MIDI.findall(result.stdout)

    def factory(self, slot: int) -> bytes:
        blob, _, _ = self.run(f"factory-{slot:02d}",
            f"10 btn 8\n20 enc 6 {slot}\n100 btn 9\n300 end\n")
        assert blob[13] == slot and blob[AMOUNT_AT] == 0
        assert blob[GRAFT_BASE_AT:GRAFT_BASE_AT + 14] == patch(blob)
        return blob


def amount(panel: Panel, initial: bytes, value: int, label: str,
           extra: str = "") -> bytes:
    blob, _, _ = panel.run(label,
        "10 btn 8\n20 btn 6\n" + extra +
        f"100 enc 0 {value}\n200 btn 9\n400 end\n", initial)
    return blob


def controls_and_endpoints(panel: Panel, factories: dict[int, bytes]) -> None:
    for source in (0, 4, 11, 31, 29):
        target = next_target(source)
        original = factories[source]
        assert original[TARGET_AT] == target, (source, original[TARGET_AT], target)
        full = amount(panel, original, 127, f"endpoint-{source:02d}")
        assert full[AMOUNT_AT] == 127 and full[TARGET_AT] == target
        # A morph remains named after its source; its effective sound reaches
        # the target exactly, including bypass and LFO configuration.
        reached, wanted = bytearray(patch(full)), bytearray(patch(factories[target]))
        reached[4] = wanted[4]
        assert reached == wanted, (source, list(reached), list(wanted))
        restored = amount(panel, full, -127, f"restored-{source:02d}")
        assert restored[AMOUNT_AT] == 0 and patch(restored) == patch(original)
    print("GRAFT: exact source/target settings in PAD and METAL, including FX and LFO endpoints")

    for source, slots in ((0, sorted(PAD)), (11, sorted(METAL))):
        base = factories[source]
        candidates = [slot for slot in slots if slot != source]
        first = candidates.index(base[TARGET_AT])
        for delta in (-1, 1, len(candidates), -len(candidates), 123, -123):
            changed, _, _ = panel.run(f"target-{source:02d}-{delta}",
                f"10 btn 8\n20 btn 6\n100 enc 1 {delta}\n200 btn 9\n400 end\n", base)
            assert changed[TARGET_AT] == candidates[(first + delta) % len(candidates)], (
                source, delta, changed[TARGET_AT])
            assert changed[AMOUNT_AT] == 0 and patch(changed) == patch(base)
    print("GRAFT: target navigation skips the source and KIT and wraps in both directions")

    off = factories[4]                 # CHAPEL GLASS: stored FRACTURE72, FX off
    middle = amount(panel, off, 64, "fx-middle")
    assert middle[14] == 1 and abs(middle[9] - 118 * 64 / 127) <= 1, list(patch(middle))
    assert middle[19] == off[19], "intermediate GRAFT changed source LFO shape"
    # The continuous fields follow the source/target line, rather than abruptly
    # enabling the source's dormant fracture or LFO depth.
    for at in (8, 10, 11, 15, 17, 22, 23):
        desired = off[at] + (factories[5][at] - off[at]) * 64 / 127
        assert abs(middle[at] - desired) <= 1, (at, middle[at], desired)
    assert abs(middle[18] - off[18] * 63 / 127) <= 1 and middle[20] == 1
    print("GRAFT: disabled FX/LFO contributions blend continuously instead of switching at halfway")


def continuity_and_editing(panel: Panel, factories: dict[int, bytes]) -> None:
    common = "10 btn 8\n20 btn 6\n500 keys 0x4880\n3500 keys 0\n4500 btn 9\n14000 end\n"
    baseline, plain, midi_plain = panel.run("zero-plain", common, factories[0])
    changed_text = common.replace("\n500 keys", "\n100 enc 1 2\n500 keys")
    zero, moved, midi_moved = panel.run("zero-target", changed_text, factories[0])
    assert pcm(plain) == pcm(moved) and midi_plain == midi_moved
    assert patch(baseline) == patch(zero)
    morph_text = common.replace("\n3500 keys", "\n1200 enc 0 32\n2000 enc 0 32\n2800 enc 0 63\n3500 keys")
    full, morphed, midi_morphed = panel.run("held-morph", morph_text, factories[0], 1)
    assert midi_plain == midi_morphed, "GRAFT retriggered or released held panel notes"
    assert pcm(plain, 1800, 3300) != pcm(morphed, 1800, 3300), "held chord did not change timbre"
    for start in range(600, 3300, 100):
        assert 10 < peak(morphed, start, start + 100) < 32767, ("held chord dropped/clipped", start)
    assert full[AMOUNT_AT] == 127
    with Image.open(Path(f"{morphed}-00003.ppm")) as image:
        image.resize((480, 480), Image.Resampling.NEAREST).save(morphed.with_suffix(".png"))
    print("GRAFT: amount zero preserves PCM; held chord morphs without extra MIDI events or audio gaps")

    initial = amount(panel, factories[0], 64, "edit-start")
    before = patch(initial)
    edited, _, _ = panel.run("manual-rebase",
        "10 btn 8\n20 btn 6\n100 enc 2 7\n200 btn 9\n400 end\n", initial)
    assert edited[AMOUNT_AT] == 0
    expected = bytearray(before)
    expected[0] += 7
    assert patch(edited) == expected
    assert edited[GRAFT_BASE_AT:GRAFT_BASE_AT + 14] == patch(edited)
    midi, _, _ = panel.run("midi-rebase",
        "10 btn 8\n100 midi 0xB0 22 9\n200 midi 0xB0 24 77\n300 btn 9\n500 end\n", initial)
    expected = bytearray(before)
    expected[2], expected[6] = 9, 77
    assert midi[AMOUNT_AT] == 0 and patch(midi) == expected
    assert midi[GRAFT_BASE_AT:GRAFT_BASE_AT + 14] == patch(midi)
    rebased, _, _ = panel.run("target-rebase",
        "10 btn 8\n20 btn 6\n100 enc 1 1\n200 btn 9\n400 end\n", initial)
    assert rebased[AMOUNT_AT] == 0 and patch(rebased) == before
    assert rebased[GRAFT_BASE_AT:GRAFT_BASE_AT + 14] == before
    assert rebased[TARGET_AT] == 2
    play = "10 btn 8\n20 btn 6\n500 keys 0x4880\n3500 keys 0\n7000 end\n"
    _, continuing, before_midi = panel.run("target-held-before", play, initial, saved=False)
    turn = play.replace("\n3500 keys", "\n1200 enc 1 1\n3500 keys")
    _, baked, after_midi = panel.run("target-held-after", turn, initial, saved=False)
    assert pcm(continuing) == pcm(baked) and before_midi == after_midi, (
        "choosing a new target at a nonzero mix changed the currently held sound")
    reloaded, _, _ = panel.run("saved-morph-reload",
        "10 btn 8\n100 btn 9\n300 end\n", initial)
    assert reloaded == initial, "GRAFT source/target/amount changed after fresh-process SAVE/reload"
    print("GRAFT: manual/MIDI edits and target changes bake the current sound; SAVE/reload retains the morph")


def forge_and_recording(panel: Panel, factories: dict[int, bytes]) -> None:
    kit = factories[KIT]
    assert kit[8:12] == bytes((64,) * 4), "factory SCRAP FORGE is not neutral"
    kit_changed, _, _ = panel.run("kit-no-graft",
        "10 btn 8\n20 btn 6\n100 enc 1 19\n200 btn 9\n400 end\n", kit)
    assert kit_changed[TARGET_AT] == KIT and kit_changed[AMOUNT_AT] == 0
    assert patch(kit_changed) == patch(kit)
    strikes = ("500 midi 0x90 48 100\n550 midi 0x80 48 0\n"
               "900 midi 0x90 50 100\n950 midi 0x80 50 0\n"
               "1300 midi 0x90 54 100\n1350 midi 0x80 54 0\n3000 end\n")
    for macro in range(4):
        hashes = []
        for value in (0, 127):
            _, out, _ = panel.run(f"forge-{macro}-{value}",
                f"10 btn 8\n100 midi 0xB0 {20 + macro} {value}\n" + strikes,
                kit, saved=False)
            assert 10 < peak(out) < 32767
            hashes.append(hashlib.sha256(pcm(out)).digest())
        assert hashes[0] != hashes[1], f"SCRAP FORGE macro {macro} did not affect the drum audio"
    print("SCRAP FORGE: BODY, WIRE, DECAY and BITE each affect the actual drum render")

    drum, forge_out, _ = panel.run("drum-record",
        "10 btn 8\n20 btn 6\n30 midi 0xB0 20 91\n40 midi 0xB0 21 21\n"
        "50 midi 0xB0 22 100\n60 midi 0xB0 23 103\n100 btn 11\n100 btn 12\n150 enc 0 -1\n"
        "200 btn 13\n400 keys 0x80\n450 keys 0\n600 keys 0x200\n650 keys 0\n"
        "800 btn 13\n3000 btn 9\n6000 end\n", kit, 1)
    assert drum[PATCH_AT:PATCH_AT + 4] == bytes((91, 21, 100, 103)), (
        "DRUM REC discarded sculpted kit controls", drum[PATCH_AT:PATCH_AT + 4])
    assert drum[PATCH_AT + 13] == 2
    with Image.open(Path(f"{forge_out}-00001.ppm")) as image:
        image.resize((480, 480), Image.Resampling.NEAREST).save(forge_out.with_suffix(".png"))

    morphed = amount(panel, factories[0], 64, "record-start")
    song, _, _ = panel.run("graft-record",
        "10 btn 8\n100 btn 11\n100 btn 12\n200 btn 13\n400 keys 0x80\n1000 keys 0\n"
        "1300 btn 13\n3000 btn 9\n13000 end\n", morphed)
    assert song[PATCH_AT + 14:PATCH_AT + 28] == patch(morphed), (
        "REC captured the unmorphed source instead of the effective patch")
    # HOME from SEQ now inspects the captured track; HOME again explicitly
    # returns to the independent live patch before testing live-only GRAFT.
    play = "100 btn 11\n100 btn 12\n150 btn 8\n180 btn 8\n200 btn 6\n3000 btn 12\n7000 end\n"
    _, plain, plain_midi = panel.run("frozen-plain", play, song, saved=False)
    changed = play.replace("3000 btn", "600 enc 0 63\n1600 enc 0 -127\n3000 btn")
    _, moved, moved_midi = panel.run("frozen-morphed", changed, song, saved=False)
    assert plain_midi == moved_midi and pcm(plain) == pcm(moved), (
        "live GRAFT altered the captured sequencer sound or note events")
    print("Recording: edited kit survives DRUM REC; a morphed melody records its frozen effective patch")


def legacy_record(blob: bytes) -> bytearray:
    old = bytearray(blob[:LEGACY_SIZE + 4])
    struct.pack_into("<II", old, 0, LEGACY_SIZE, 7)
    old[-2:] = b"\0\0"                 # restore schema-7 tail padding
    return old


def migration_and_rejection(panel: Panel, factories: dict[int, bytes]) -> None:
    for fx in (0, 1):
        old = legacy_record(factories[KIT])
        old[8:12] = bytes((48, 105, 23, 31))
        old[14] = fx
        old[PATCH_AT:PATCH_AT + 4] = bytes((48, 105, 23, 31))
        old[PATCH_AT + 5] = fx
        old[LENGTH_AT:LENGTH_AT + 4] = bytes((32, 16, 64, 32))
        old[24 + 13:24 + 17] = bytes((1, 50, 99, 0))
        upgraded, _, _ = panel.run(f"schema7-kit-fx{fx}",
            "10 btn 8\n100 btn 9\n300 end\n", old)
        assert upgraded[8:12] == bytes((64,) * 4)
        assert upgraded[PATCH_AT:PATCH_AT + 4] == bytes((64,) * 4)
        assert upgraded[24:PATCH_AT] == old[24:PATCH_AT], "schema-7 notes or gates changed"
        assert upgraded[LENGTH_AT:LENGTH_AT + 4] == old[LENGTH_AT:LENGTH_AT + 4]
        assert upgraded[PATCH_AT + 14:PATCH_AT + 56] == old[PATCH_AT + 14:PATCH_AT + 56]
        assert upgraded[AMOUNT_AT] == 0 and upgraded[TARGET_AT] == KIT
    old_pad = legacy_record(factories[3])
    upgraded, _, _ = panel.run("schema7-pad", "10 btn 8\n100 btn 9\n300 end\n", old_pad)
    assert patch(upgraded) == patch(old_pad), "schema-7 live pad was changed"
    assert upgraded[24:PATCH_AT] == old_pad[24:PATCH_AT]
    # Old DRUM macros were ignored, but all other captured sound fields remain.
    for at in range(PATCH_AT, PATCH_AT + 56):
        if at not in range(PATCH_AT, PATCH_AT + 4):
            assert upgraded[at] == old_pad[at], ("legacy track field changed", at)
    print("Migration: schema-7 notes, lengths and pitched patches survive; legacy KIT controls start neutral")

    for label, at in (("live-kit-range", 8), ("track-kit-range", PATCH_AT)):
        invalid = legacy_record(factories[KIT])
        invalid[at] = 128
        fresh, _, _ = panel.run("reject-schema7-" + label,
            "10 btn 8\n100 btn 9\n300 end\n", invalid)
        assert fresh[13] == 0, "legacy KIT normalization hid an invalid macro value"

    for label, at, value in (("wrong-engine", TARGET_AT, 11),
                             ("kit-target", TARGET_AT, KIT),
                             ("too-large", AMOUNT_AT, 255),
                             ("wrong-base-mode", GRAFT_BASE_AT + 13, 2)):
        invalid = bytearray(factories[3])
        invalid[at] = value
        fresh, _, _ = panel.run("reject-" + label,
            "10 btn 8\n100 btn 9\n300 end\n", invalid)
        assert fresh[13] == 0 and fresh[AMOUNT_AT] == 0, (label, fresh[13])
    print("Settings: invalid target/mode/amount falls back to complete factory state")


def controls_preview(panel: Panel) -> None:
    text = (ROOT / "tests/scripts/forge-graft-controls.txt").read_text(encoding="ascii")
    _, out, _ = panel.run("controls", text, fps=1)
    screens = {}
    for position in (1, 5, 6, 7, 8, 9, 11):
        with Image.open(Path(f"{out}-{position:05d}.ppm")) as image:
            screens[position] = image.copy()
    assert screens[1].crop((8, 8, 152, 25)).tobytes() != screens[6].crop((8, 8, 152, 25)).tobytes()
    assert screens[1].crop((8, 76, 120, 154)).tobytes() != screens[6].crop((8, 76, 120, 154)).tobytes()
    assert screens[1].crop((120, 76, 232, 154)).tobytes() != screens[5].crop((120, 76, 232, 154)).tobytes()
    graft_row = (8, 158, 232, 174)
    assert len({screens[n].crop(graft_row).tobytes() for n in (6, 7, 8, 9)}) == 4, (
        "GRAFT amount/target controls did not update the displayed row")
    for position, label in ((5, "forge-menu"), (7, "graft-middle"), (9, "graft-end"), (11, "home")):
        path = out.with_name(out.name + "-" + label).with_suffix(".png")
        screens[position].resize((480, 480), Image.Resampling.NEAREST).save(path)
    print("Controls preview: FORGE labels and knobs, GRAFT amount and target, and HOME return")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("prefix", type=Path)
    args = parser.parse_args()
    panel = Panel(args.executable, args.prefix)
    required = {0, 1, 2, 3, 4, 5, 11, 12, 29, 30, 31}
    required.update(next_target(source) for source in (0, 4, 11, 31, 29))
    factories = {slot: panel.factory(slot) for slot in required}
    controls_and_endpoints(panel, factories)
    continuity_and_editing(panel, factories)
    forge_and_recording(panel, factories)
    migration_and_rejection(panel, factories)
    controls_preview(panel)
    print("SCRAP FORGE/GRAFT integration checks passed (host only)")


if __name__ == "__main__":
    main()
