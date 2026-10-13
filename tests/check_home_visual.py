#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check HOME's audio visual and EDIT menu against the actual host app.

    py tests/check_home_visual.py build/host/terkinstein.exe build/host/home-visual

Screens check the menu, preset navigation, purple/green audio response and a
held one-shot's visual decay. Matched runs check that UI navigation changes no
PCM, MIDI events or schema-9 settings, including during REC and fast ARP.
LCD pixel counts bound traffic; the host does not simulate SPI blocking time
and these tests do not qualify device timing or CPU headroom.
"""
from __future__ import annotations

import argparse
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

import check_preset_bank as presets


ROOT = Path(__file__).resolve().parent.parent
SCRIPTS = ROOT / "tests/scripts"
VIEW = (8, 70, 232, 214)
MACROS = (8, 78, 232, 166)
MIDI = re.compile(r"^\s*(\d+) ms  MIDI out ([0-9A-F]{2}) (\d+) (\d+)$", re.MULTILINE)
LCD = re.compile(r"^\s*(\d+) ms  LCD pixels (\d+)$", re.MULTILINE)
SAVED = re.compile(r"^\s*(\d+) ms  settings saved \((\d+) B\)$", re.MULTILINE)


def run(exe: Path, prefix: Path, script: Path, store: Path, fps: int = 0) -> str:
    env = dict(os.environ, TK_HOST_SETTINGS_PATH=str(store), TK_TRACE_LCD="1")
    result = subprocess.run([str(exe), str(prefix), str(script), str(fps)],
                            cwd=ROOT, env=env, check=True,
                            capture_output=True, text=True)
    prefix.with_suffix(".log").write_text(result.stdout, encoding="utf-8")
    return result.stdout


def frame(prefix: Path, position: int) -> Image.Image:
    with Image.open(Path(f"{prefix}-{position:05d}.ppm")) as image:
        assert image.size == (240, 240), image.size
        return image.convert("RGB")


def pixels(image: Image.Image):
    data = image.tobytes()
    return zip(data[0::3], data[1::3], data[2::3])


def numeric_mask(image: Image.Image) -> set[int]:
    """Read white/green numeric glyphs, excluding the dim purple geometry.

    Only the four numeric boxes are inspected, so HOME's central green breach
    and EDIT's green GRAFT row cannot stand in for parameter readouts.
    """
    result = set()
    for row in range(4):
        region = image.crop((200, 76 + row * 20, 232, 92 + row * 20))
        for i, (r, g, b) in enumerate(pixels(region)):
            if (r > 235 and g > 235 and b > 235) or (g > 180 and g > r * 5 // 4 and g > b * 5 // 4):
                result.add(row * 512 + i)
    return result


def differences(first: Image.Image, second: Image.Image) -> int:
    assert first.size == second.size
    return sum(a != b for a, b in zip(pixels(first), pixels(second)))


def pcm(prefix: Path) -> bytes:
    with wave.open(str(prefix) + ".wav", "rb") as wav:
        assert (wav.getnchannels(), wav.getsampwidth(), wav.getframerate()) == (2, 2, 44100)
        return wav.readframes(wav.getnframes())


def peak(prefix: Path, begin: int, end: int) -> int:
    with wave.open(str(prefix) + ".wav", "rb") as wav:
        wav.setpos(begin * 44100 // 1000)
        samples = array.array("h", wav.readframes((end - begin) * 44100 // 1000))
    if sys.byteorder != "little":
        samples.byteswap()
    return max(map(abs, samples), default=0)


def settings(path: Path) -> bytes:
    blob = path.read_bytes()
    assert len(blob) == 3704 and struct.unpack_from("<II", blob) == (3700, 11), (
        "HOME changed the current schema-11 settings layout")
    return blob


def lcd_budget(log: str, *, begin: int = 2000, end: int = 11000) -> None:
    # Twelve complete 224x144 viewports cost 387,072 pixels/sec. Dirty tiles
    # should cost less; allow small captions and boundary rounding as well.
    counts = {int(ms): int(n) for ms, n in LCD.findall(log)}
    for ms, count in counts.items():
        if begin <= ms <= end:
            assert count <= 420_000, f"HOME sent {count} LCD pixels by {ms} ms"


def check_menu(exe: Path, prefix: Path, directory: Path) -> None:
    store = directory / "menu.bin"
    log = run(exe, prefix, SCRIPTS / "home-navigation.txt", store, 1)
    screens = [frame(prefix, n) for n in range(1, 7)]
    # Parameter readouts live on EDIT. Compare their glyph locations rather
    # than requiring white text; 0.11 extends the purple/green palette here.
    reference = numeric_mask(screens[1])
    assert len(reference) > 50, "EDIT did not reveal numeric parameters"
    assert len(numeric_mask(screens[4])) > 50, "EDIT did not reopen numeric parameters"
    for n in (0, 2, 3, 5):
        assert len(numeric_mask(screens[n]) & reference) < len(reference) * .75, (
            f"HOME still exposes parameter numbers at frame {n + 1}")
    assert screens[1].crop(MACROS).tobytes() != screens[4].crop(MACROS).tobytes(), (
        "SHADE knob on HOME did not update the hidden sound parameters")
    assert len(SAVED.findall(log)) == 1
    blob = settings(store)
    assert blob[8] == 102, f"HOME SHADE knob did not preserve +12 detents: {blob[8]}"
    for n, name in ((0, "home"), (1, "edit"), (4, "edit-changed")):
        screens[n].resize((480, 480), Image.Resampling.NEAREST).save(
            prefix.with_name(prefix.name + "-" + name).with_suffix(".png"))
    print("HOME EDIT: parameters hidden, toggled both ways and HOME knobs still edit sound")


def ordered_script(common: str, additions: str) -> str:
    # Preserve source order for events sharing a timestamp. The scenarios
    # deliberately avoid additions at any common event time.
    lines = (common + additions).splitlines()
    return "\n".join(sorted(lines, key=lambda line: int(line.split()[0]))) + "\n"


def check_navigation_invariance(exe: Path, prefix: Path, directory: Path) -> None:
    scenarios = {
        "live": (
            "100 btn 8\n200 keys 0x4880\n2500 keys 0\n3000 btn 9\n8000 end\n",
            "700 btn 6\n1400 btn 6\n1800 btn 6\n2200 btn 8\n"),
        "seq": (
            "100 btn 11\n100 btn 12\n150 btn 8\n500 keys 0x80\n1800 keys 0\n"
            "3800 btn 12\n4200 btn 9\n8000 end\n",
            "700 btn 6\n1400 btn 6\n2000 btn 6\n2600 btn 8\n"),
        "rec": (
            "100 btn 11\n100 btn 12\n150 btn 13\n180 btn 8\n400 keys 0x80\n"
            "850 keys 0\n1100 keys 0x400\n1750 keys 0\n2000 btn 13\n"
            "3000 btn 12\n3500 btn 9\n8000 end\n",
            "600 btn 6\n1000 btn 6\n1400 btn 6\n1700 btn 8\n"),
        "arp": (
            "10 hold 10 1\n760 hold 10 0\n780 btn 10\n810 enc 2 90\n820 enc 4 1\n830 enc 5 1\n"
            "840 btn 8\n850 keys 0x4880\n2000 keys 0\n2300 btn 10\n"
            "3000 btn 9\n8000 end\n",
            "900 btn 6\n1100 btn 6\n1300 btn 6\n1700 btn 8\n"),
    }
    for name, (common, additions) in scenarios.items():
        results = []
        for variant, extra in (("plain", ""), ("edited", additions)):
            script = directory / f"{name}-{variant}.txt"
            script.write_text(ordered_script(common, extra), encoding="ascii")
            store = directory / f"{name}-{variant}.bin"
            out = prefix.with_name(prefix.name + f"-{name}-{variant}")
            log = run(exe, out, script, store)
            assert len(SAVED.findall(log)) == 1, f"{name}: SAVE never finished"
            results.append((pcm(out), MIDI.findall(log), settings(store)))
            lcd_budget(log, end=2500)
        assert results[0] == results[1], (
            f"HOME EDIT navigation changed {name} PCM, MIDI timing or saved settings")
        print(f"HOME EDIT: {name} PCM, MIDI timestamps and settings unchanged")


def check_audio_response(exe: Path, prefix: Path, directory: Path) -> None:
    outputs = {}
    for label in ("quiet", "panel", "midi", "kit", "kit-quiet", "seq"):
        out = prefix.with_name(prefix.name + "-" + label)
        fps = 10 if label.startswith("kit") else 2
        log = run(exe, out, SCRIPTS / f"home-{label}.txt", directory / f"{label}.bin", fps)
        lcd_budget(log)
        outputs[label] = out
    quiet = outputs["quiet"]
    for label in ("panel", "midi", "seq"):
        sound = outputs[label]
        assert peak(sound, 2000, 2200) > 10, f"{label}: response scenario has no audio"
        delta = differences(frame(sound, 4).crop(VIEW), frame(quiet, 4).crop(VIEW))
        assert delta > 250, f"{label}: rendered sound did not affect HOME ({delta} pixels)"
        visual = frame(sound, 4).crop(VIEW)
        purple = sum(r > 30 and b > 30 and r > g * 5 // 4 and b > g * 5 // 4
                     for r, g, b in pixels(visual))
        green = sum(g > 30 and g > r * 5 // 4 and g > b * 5 // 4
                    for r, g, b in pixels(visual))
        assert purple > 50 and green > 50, f"{label}: purple/green palette missing"
        assert peak(sound, 10500, 11500) == 0, f"{label}: note tail never became silent"
        calm = differences(frame(sound, 22).crop(VIEW), frame(quiet, 22).crop(VIEW))
        assert calm < max(100, delta // 3), f"{label}: HOME did not settle after silence ({calm})"
        frame(sound, 4).resize((480, 480), Image.Resampling.NEAREST).save(sound.with_suffix(".png"))
    kit, kit_quiet = outputs["kit"], outputs["kit-quiet"]
    assert peak(kit, 1200, 1320) > 10 and peak(kit, 7000, 8000) == 0
    onset = max(differences(frame(kit, n).crop(VIEW), frame(kit_quiet, n).crop(VIEW))
                for n in range(13, 17))
    assert onset > 100, f"kit onset did not animate HOME: {onset} pixels"
    settled = differences(frame(kit, 90).crop(VIEW), frame(kit_quiet, 90).crop(VIEW))
    assert settled < max(100, onset // 3), (
        f"a physically held silent drum key kept HOME animated: {settled} pixels")
    frame(kit, 13).resize((480, 480), Image.Resampling.NEAREST).save(kit.with_suffix(".png"))
    print("HOME visual: panel, MIDI and generated audio respond; released pads and held drums fade")
    print("HOME LCD: bounded pixel traffic; physical SPI timing remains a device check")


def check_presets(exe: Path, prefix: Path, directory: Path) -> None:
    run(exe, prefix, SCRIPTS / "preset-bank.txt", directory / "presets.bin", 1)
    signatures = {n: presets.title_signature(Path(f"{prefix}-{n:05d}.ppm"))
                  for n in range(2, presets.BANK_SIZE + 5)}
    assert len({signatures[n] for n in range(2, presets.BANK_SIZE + 2)}) == presets.BANK_SIZE
    assert signatures[presets.BANK_SIZE + 2] == signatures[2] == signatures[presets.BANK_SIZE + 4], \
        "HOME clockwise preset wrap failed"
    assert signatures[presets.BANK_SIZE + 3] == signatures[presets.BANK_SIZE + 1], \
        "HOME reverse preset wrap failed"
    print(f"HOME presets: {presets.BANK_SIZE} distinct titles and wraparound preserved")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("prefix", type=Path)
    args = parser.parse_args()
    exe, prefix = args.executable.resolve(), args.prefix.resolve()
    prefix.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="tk-home-", dir=prefix.parent) as tmp:
        directory = Path(tmp)
        check_menu(exe, prefix.with_name(prefix.name + "-menu"), directory)
        check_navigation_invariance(exe, prefix.with_name(prefix.name + "-navigation"), directory)
        check_audio_response(exe, prefix.with_name(prefix.name + "-response"), directory)
        check_presets(exe, prefix.with_name(prefix.name + "-presets"), directory)
    for ppm in prefix.parent.glob(prefix.name + "-*.ppm"):
        ppm.unlink()
    print("PASS: HOME sound visual and EDIT integration")


if __name__ == "__main__":
    main()
