#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check the host 64-slot preset bank, MIDI selection and rendered sounds.

First run tools/preview.py --cc cl --script tests/scripts/preset-bank.txt
--fps 1 --out build/host/preset-bank. Pass its executable and preview prefix:

    python tests/check_preset_bank.py build/host/preset-bank build/host/terkinstein.exe

The title-strip check asserts 64 visually distinct slots and wraparound. The
audio check applies every sound to the same C-minor chord, with strike windows
for every kit. USB and rear Program Change select all factory indices and
reject indices64..127. This is a signal-level regression, not a substitute for
listening on the FM-1 or a proof that nobody has ever authored a similar sound.
"""
from __future__ import annotations

import argparse
import array
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import struct
import tempfile
import wave

from PIL import Image
from check_external_midi import Panel

CATALOG = json.loads((Path(__file__).resolve().parents[1] /
                      "assets/presets/terkinstein-presets.json").read_text(encoding="utf-8"))
PRESET_NAMES = tuple(preset["name"] for preset in CATALOG["presets"])
PRESET_INSTRUMENTS = tuple(preset["instrument"] for preset in CATALOG["presets"])
KIT = 2


BANK_SIZE = 64


def title_signature(path: Path) -> bytes:
    with Image.open(path) as image:
        # The preview doubles the FM-1 pixels. Only the title band is examined;
        # animated status, knobs and the unsaved-change star cannot affect it.
        scale = image.width // 240
        if image.size != (240 * scale, 240 * scale):
            raise AssertionError(f"unexpected preview size: {path}: {image.size}")
        box = (8 * scale, 45 * scale, 220 * scale, 64 * scale)
        strip = image.convert("RGB").crop(box)
        if not strip.getbbox():
            raise AssertionError(f"empty preset title: {path}")
        return hashlib.sha256(strip.tobytes()).digest()


def check_navigation(prefix: Path) -> None:
    frames = {n: title_signature(Path(f"{prefix}-{n:05d}.png"))
              for n in range(2, BANK_SIZE + 5)}
    titles = [frames[n] for n in range(2, BANK_SIZE + 2)]
    assert len(set(titles)) == BANK_SIZE, \
        f"encoder traversal did not show {BANK_SIZE} distinct titles"
    assert frames[BANK_SIZE + 2] == frames[2], \
        "clockwise wrap did not return to slot 01"
    assert frames[BANK_SIZE + 3] == frames[BANK_SIZE + 1], \
        f"counterclockwise wrap did not return to slot {BANK_SIZE:02d}"
    assert frames[BANK_SIZE + 4] == frames[2], \
        "final clockwise step did not return to slot 01"
    print(f"Preset navigation: {BANK_SIZE} distinct titles and both wrap directions passed")


def render_audio(executable: Path, output_dir: Path) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    hashes: set[bytes] = set()
    for slot in range(1, BANK_SIZE + 1):
        name = PRESET_NAMES[slot - 1]
        script = output_dir / f"preset-{slot:02d}.txt"
        prefix = output_dir / f"preset-{slot:02d}"
        script.write_text(
            f"100 enc 6 {slot - 1}\n250 keys 0x4480\n6250 keys 0\n7500 end\n",
            encoding="ascii",
        )
        subprocess.run([str(executable), str(prefix), str(script)],
                       check=True, stdout=subprocess.DEVNULL)
        with wave.open(str(prefix) + ".wav", "rb") as wav:
            assert wav.getnchannels() == 2 and wav.getsampwidth() == 2
            assert wav.getframerate() == 44100
            samples = array.array("h", wav.readframes(wav.getnframes()))
        if sys.byteorder != "little":
            samples.byteswap()
        # The kit is a one-shot strike; held-pad windows miss its entire tail.
        active = samples[2 * int(.25 * 44100): 2 * int(.50 * 44100)] if PRESET_INSTRUMENTS[slot - 1] == KIT \
            else samples[2 * 44100: 12 * 44100]
        peak = max(map(abs, active), default=0)
        rms = (sum(v * v for v in active) / len(active)) ** 0.5
        assert peak > 0 and rms > 1.0, f"{name}: inaudible render (peak {peak}, RMS {rms:.2f})"
        assert max(map(abs, samples), default=0) < 32767, f"{name}: clipped sample"
        digest = hashlib.sha256(samples.tobytes()).digest()
        assert digest not in hashes, f"{name}: byte-identical to another preset"
        hashes.add(digest)
        print(f"{slot:02d} {name:14s} peak={peak:5d} RMS={rms:7.2f}")
    print("Preset audio: all 64 slots non-silent, unclipped, and byte-distinct")


def check_program_changes(executable: Path, output_dir: Path) -> None:
    """Drive the actual app input loop; compare USB/rear selections to encoders."""
    output_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="tk-64-midi-") as directory:
        panel = Panel(executable, output_dir / "program", Path(directory))
        for slot in range(BANK_SIZE):
            expected = panel.run(f"encoder-{slot}",
                f"10 btn 8\n30 enc 6 {slot}\n400 btn 9\n1000 end\n")[2]
            assert expected is not None and struct.unpack_from("<II", expected) == (3700, 11)
            assert panel.run(f"reload-{slot}", "10 btn 8\n400 btn 9\n1000 end\n",
                             store=expected)[2] == expected, (slot, "schema11 factory round trip")
            for port in ("midi", "din"):
                # Invalid values after the legal change cannot replace it.
                events = "".join(f"{100 + invalid} {port} 0xC0 {invalid} 0\n"
                                 for invalid in range(64, 128))
                actual = panel.run(f"{port}-{slot}",
                    f"10 btn 8\n30 {port} 0xC0 {slot} 0\n" + events +
                    "400 btn 9\n1000 end\n")
                assert actual[2] == expected, (port, slot, "factory/save mismatch")
                assert not actual[1], (port, slot, "Program Change echoed musical MIDI")
        # Captured edits belong to SAVE, not to the new catalog. Deliberately
        # nonfactory FM parameters in all six banks must survive boot unchanged.
        edited = bytearray(expected)
        for bank in range(6):
            edited[3456 + bank * 40 + 1] = 11 + bank * 9
            edited[3456 + bank * 40 + 30] = 21 + bank
        # A manual edit with GRAFT=0 bakes the live patch into the GRAFT base.
        # Model that coherent record; mismatched raw fields are recomputed at
        # boot by design, which does not mean factory metadata replaced them.
        edited[3656:3696] = edited[3456:3496]
        assert panel.run("edited-fm-roundtrip", "10 btn 8\n400 btn 9\n1000 end\n",
                         store=bytes(edited))[2] == bytes(edited), "saved FM edits were replaced by factories"
    print("MIDI Program Change: USB/rear indices 0–63 match encoder factories; 64–127 ignored")
    print("SAVE: all64 schema11 factory records and nonfactory six-bank FM fields round trip unchanged")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("prefix", type=Path, help="preview output prefix")
    parser.add_argument("executable", type=Path, help="compiled host app")
    args = parser.parse_args()
    check_navigation(args.prefix)
    render_audio(args.executable.resolve(), args.prefix.parent / "preset-bank-audio")
    check_program_changes(args.executable.resolve(), args.prefix.parent / "preset-bank-midi")


if __name__ == "__main__":
    main()
