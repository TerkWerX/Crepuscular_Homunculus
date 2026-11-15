#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual-main startup artwork, cooperative cancellation and musical neutrality.

These desktop fixtures exercise the production app and LCD capture. They do
not measure device CPU/SPI/USB timing or qualify firmware on real hardware.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import json
import os
from pathlib import Path
import re
import subprocess
import wave

from PIL import Image

from check_external_midi import MIDI, peak
from check_remote import answer, decode, sysex


ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "assets/crepuscular-homunculus/bubbling-v1"
REMOTE = re.compile(r"(\d+) ms\s+REMOTE out ([0-9A-F]+)")
FPS = 20


def pixels565(image: Image.Image) -> bytes:
    rgb = image.convert("RGB").tobytes()
    return b"".join((((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)).to_bytes(2, "little")
                    for r, g, b in zip(rgb[0::3], rgb[1::3], rgb[2::3]))


@dataclass
class Result:
    out: Path
    pcm: bytes
    midi: list
    settings: bytes | None
    replies: dict
    screen_events: list


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--assets", type=Path, default=ASSETS)
    args = parser.parse_args()
    exe, folder = args.executable.resolve(), args.output.resolve()
    folder.mkdir(parents=True, exist_ok=True)
    checks = 0

    def check(ok, why):
        nonlocal checks
        checks += 1
        assert ok, why

    def run(name, events=(), *, end=2300, fps=0):
        out = folder / name
        settings, journal = out.with_suffix(".bin"), out.with_suffix(".nor")
        for path in (settings, journal):
            if path.is_file():
                path.unlink()
        script = out.with_suffix(".txt")
        lines = sorted(events, key=lambda line: int(line.split()[0]))
        script.write_text("\n".join(lines + [f"{end} end"]) + "\n", encoding="ascii")
        env = dict(os.environ, TK_HOST_SETTINGS_PATH=str(settings),
                   TK_HOST_JOURNAL_PATH=str(journal), TK_SCREEN_VALIDATE="1")
        result = subprocess.run([str(exe), str(out), str(script), str(fps)],
                                cwd=ROOT, env=env, check=True, capture_output=True, text=True)
        out.with_suffix(".log").write_text(result.stdout + result.stderr, encoding="utf-8")
        replies, screen_events = {}, []
        for match in REMOTE.finditer(result.stdout):
            command, rid, payload = decode(bytes.fromhex(match[2]))
            replies.setdefault(rid, []).append((int(match[1]), command, payload))
            if command == 0x0B:
                screen_events.append((int(match[1]), payload))
        midi = [tuple(int(x, 16) if index == 1 else int(x)
                      for index, x in enumerate(match)) for match in MIDI.findall(result.stdout)]
        with wave.open(str(out) + ".wav", "rb") as wav:
            pcm = wav.readframes(wav.getnframes())
        return Result(out, pcm, midi, settings.read_bytes() if settings.exists() else None,
                      replies, screen_events)

    def snapshot(result, time):
        with Image.open(Path(f"{result.out}-{time * FPS // 1000:05d}.ppm")) as image:
            return image.convert("RGB")

    def ui(result, rid):
        body = answer(result.replies, rid)
        check(len(body) == 4 and body[1] == 1, "UI-state version/layout changed")
        return body[2:]

    manifest = json.loads((args.assets / "firmware-manifest.json").read_text())
    crop = manifest["crop"]
    box = (crop["x"], crop["y"], crop["x"] + crop["width"], crop["y"] + crop["height"])
    expected = []
    for label in "ABC":
        with Image.open(args.assets / f"homunculus_{label}_firmware.png") as image:
            expected.append(pixels565(image.crop(box)))
    check(len(set(expected)) == 3, "All three animation frames must be distinct")

    probes = [sysex(time, 0x0C, rid) for rid, time in enumerate((1100, 1300, 1600, 1900, 2050, 2200), 1)]
    quiet = run("startup-idle", fps=FPS)
    observed = run("startup-observed", probes, fps=FPS)
    check(not quiet.midi and peak(quiet.pcm, 0, 2300) == 0,
          "Startup must not start notes or transport")
    check(quiet.settings is None, "Automatic startup must not save settings")
    check(quiet.pcm == observed.pcm and quiet.midi == observed.midi,
          "Startup telemetry changed audio or MIDI")
    for rid in range(1, 7):
        check(ui(observed, rid) == bytes((1, 0)), "Both startup stages must report splash visible")

    with Image.open(ROOT / "assets/splash/terkinstein-splash-240.png") as logo:
        check(pixels565(snapshot(quiet, 1100)) == pixels565(logo),
              "Original TERKINSTEIN logo did not stay intact during its display period")
    runs, seen, outside = [], [], None
    for time in range(1100, 2251, 50):
        original, queried = snapshot(quiet, time), snapshot(observed, time)
        check(original.tobytes() == queried.tobytes(), "Telemetry changed physical LCD pixels")
        raw = pixels565(original.crop(box))
        if raw not in expected:
            continue
        index = expected.index(raw)
        seen.append((time, index))
        if not runs or runs[-1][1] != index:
            runs.append((time, index))
            original.save(folder / f"rendered-{len(runs)}-{'ABC'[index]}.png")
        # Keep the product title and all margins static once a whole frame is
        # settled; this also catches left-over strips from the previous logo.
        original.paste((0, 0, 0), box)
        if outside is None:
            outside = original.tobytes()
        else:
            check(original.tobytes() == outside, "Title/margins changed between settled animation frames")
    check([index for _, index in runs] == [0, 1, 2, 1, 0],
          f"Actual settled LCD frames did not repeat A-B-C-B-A: {runs}")
    check(1250 <= runs[0][0] <= 1350, "First frame did not follow the 1200ms logo hold plus bounded painting")
    check(all(150 <= later[0] - earlier[0] <= 250 for earlier, later in zip(runs, runs[1:])),
          "Settled frame cadence is outside the 200ms period plus strip/snapshot allowance")
    check(seen[-1][0] >= 2200, "Intro dismissed itself after the first animation loop")
    snapshot(quiet, 1100).save(folder / "rendered-logo.png")
    snapshot(quiet, 2200).save(folder / "rendered-held-intro.png")
    title = snapshot(quiet, runs[0][0]).crop((0, 190, 240, 240))
    check(title.getbbox() is not None, "Firmware title is missing beneath the character")

    # This is the actual capture hook, checked every host poll, plus actual
    # emitted companion packets. The compact HOME codec must never be used
    # for either startup stage, including partially painted frame strips.
    streamed = run("startup-streamed", probes + [sysex(50, 0x0A, 70, (3, 1)),
                   sysex(2050, 0x0A, 71, (4, 1))], fps=FPS)
    startup_chunks = [body for time, body in streamed.screen_events]
    check(bool(startup_chunks), "Startup screen subscription emitted no data")
    check(all(body[6] < 3 for body in startup_chunks), "Startup artwork used the reduced HOME codec")
    check(streamed.pcm == quiet.pcm and streamed.midi == quiet.midi and streamed.settings is None,
          "Screen subscription changed startup audio, notes or stored settings")
    for time in range(1100, 2251, 50):
        check(snapshot(streamed, time).tobytes() == snapshot(quiet, time).tobytes(),
              "Screen subscription changed actual LCD artwork")

    late = run("startup-long-wait", [sysex(9000, 0x0C, 1),
               "10000 btn 4", sysex(10100, 0x0C, 2)], end=10300)
    check(ui(late, 1) == bytes((1, 0)), "Startup dismissed itself during a long wait")
    check(ui(late, 2) == bytes((0, 3)), "A late ENV press failed to dismiss and navigate")

    # Exercise cancellation while the first full-screen image is partly drawn
    # and while later crop strips are pending, not just between stable frames.
    for time in (1230, 1490, 1690, 1890):
        key = run(f"key-skip-{time}", [f"{time} keys 0x80", sysex(time + 80, 0x0C, 1),
                  f"{time + 200} keys 0", sysex(2250, 0x0C, 2)])
        check(ui(key, 1) == bytes((0, 0)) and ui(key, 2) == bytes((0, 0)),
              "A physical key failed to cancel pending intro drawing permanently")
        check(key.midi == [(time, 0x90, 48, 100), (time + 200, 0x80, 48, 0)],
              "Intro cancellation swallowed or delayed the first physical note/release")
        check(peak(key.pcm, time + 10, time + 190) > 0, "First physical note did not render audio")
        env = run(f"env-skip-{time}", [f"{time} btn 4", sysex(time + 80, 0x0C, 1),
                  sysex(2250, 0x0C, 2)], fps=FPS)
        check(ui(env, 1) == bytes((0, 3)) and ui(env, 2) == bytes((0, 3)),
              "The first ENV button did not both cancel the intro and navigate")
        check(snapshot(env, 2200).tobytes() == snapshot(env, 2250).tobytes(),
              "Canceled animation overwrote the selected static page")

    # Incoming performance and knobs keep working during the art, without
    # becoming an automatic dismissal. Compare the same musical event stream
    # with early HOME dismissal to detect any DSP/ownership/save side effects.
    music = ["1300 midi 0x90 60 100", "1350 enc 2 1", "1500 din 0x91 64 90",
             "1600 midi 0xB0 64 127", "1700 midi 0x80 60 0", "1900 din 0x81 64 0",
             "2100 midi 0xB0 64 0", "6000 btn 9"]
    during = run("music-during-intro", music + [sysex(1800, 0x0C, 1)], end=6500)
    skipped = run("music-intro-skipped", ["100 btn 8", *music], end=6500)
    check(ui(during, 1) == bytes((1, 0)), "MIDI or encoder activity dismissed the artwork")
    check(during.pcm == skipped.pcm, "Startup animation changed rendered musical PCM")
    check(during.midi == skipped.midi, "Startup animation changed MIDI ownership/timestamps")
    check(during.settings is not None and during.settings == skipped.settings,
          "Startup animation changed the explicit SAVE result")

    report = {"checks": checks, "host_only": True, "hardware_qualified": False,
              "settled_frame_sequence": [{"time_ms": time, "frame": "ABC"[index]} for time, index in runs],
              "startup_capture_and_lossless_encoding": "pass", "partial_draw_key_button_cancellation": "pass",
              "first_note_timestamps": "pass", "pcm_midi_save_neutrality": "pass"}
    report["waits_for_key_or_button"] = "pass"
    (folder / "intro-report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"PASS actual-main homunculus intro: {checks} checks; desktop evidence, not device timing")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
