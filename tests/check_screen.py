#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual app LCD mirror: exact framebuffer and portable audio/note neutrality."""
from pathlib import Path
import json
import math
import os
import re
import subprocess
import sys
from check_remote import decode, run, sysex


def unpack7(data):
    out = bytearray()
    at = 0
    while at < len(data):
        mask = data[at]
        at += 1
        for bit in range(min(7, len(data) - at)):
            out.append(data[at] | ((mask >> bit & 1) << 7))
            at += 1
    return out


def pixels_from_log(log):
    frame = bytearray(240 * 240 * 2)
    assembly = {}
    complete = set()
    for match in re.finditer(r"(\d+) ms\s+REMOTE out ([0-9A-F]+)", log):
        command, _, payload = decode(bytes.fromhex(match[2]))
        if command != 0x0B:
            continue
        assert payload[0] == 0 and payload[1] == 1 and len(payload) <= 80
        x, y = payload[2:4]
        serial = payload[4] | payload[5] << 7
        encoding = payload[6]
        total = payload[7] | payload[8] << 7
        offset = payload[9] | payload[10] << 7
        assert x < 15 and y < 15 and encoding <= 2 and 0 < total <= 512
        chunk = unpack7(payload[11:])
        assert 0 < len(chunk) <= 60
        key = x, y
        if offset == 0:
            assembly[key] = serial, bytearray()
        current_serial, current = assembly[key]
        assert current_serial == serial and len(current) == offset
        current.extend(chunk)
        assert len(current) <= total
        if len(current) < total:
            continue
        if encoding == 1:
            assert total % 3 == 0
            raw = bytearray()
            for at in range(0, total, 3):
                assert current[at] > 0
                raw.extend(current[at + 1:at + 3] * current[at])
        elif encoding == 2:
            colors = current[0]
            assert 1 <= colors <= 16
            bits = 1 if colors <= 2 else 2 if colors <= 4 else 4
            base = 1 + 2 * colors
            assert total == base + 32 * bits
            palette = [current[1 + i * 2:3 + i * 2] for i in range(colors)]
            raw = bytearray()
            for pixel in range(256):
                index = current[base + (pixel * bits >> 3)] >> (pixel * bits & 7) & ((1 << bits) - 1)
                assert index < colors
                raw.extend(palette[index])
        else:
            raw = current
        assert len(raw) == 512
        for row in range(16):
            at = ((y * 16 + row) * 240 + x * 16) * 2
            frame[at:at + 32] = raw[row * 32:row * 32 + 32]
        complete.add(key)
    return frame, complete


def rgb_bytes(frame):
    out = bytearray()
    for at in range(0, len(frame), 2):
        c = frame[at] | frame[at + 1] << 8
        out.extend(((c >> 11) * 255 // 31, ((c >> 5) & 63) * 255 // 63, (c & 31) * 255 // 31))
    return out


def home_snapshot_budget(exe, folder):
    """Compare lossless codec budgets on actual native HOME framebuffer frames.

    Millisecond figures model sending limits; this is not USB/CPU measurement.
    The host itself advances one millisecond per main loop and cannot model
    the target's admitted three-packet burst within the same millisecond.
    """
    from PIL import Image
    prefix = folder / "native-home-budget"
    script = Path(__file__).parent / "scripts/home-panel.txt"
    subprocess.run([str(exe), str(prefix), str(script.resolve()), "10"],
                   text=True, capture_output=True, check=True)
    previous = None
    frames = []
    for path in sorted(folder.glob("native-home-budget-*.ppm")):
        time = int(path.stem.rsplit("-", 1)[1]) * 100
        with Image.open(path) as image:
            rgb = image.tobytes()
        pixels = [((rgb[i] >> 3) << 11) | ((rgb[i + 1] >> 2) << 5) | (rgb[i + 2] >> 3)
                  for i in range(0, len(rgb), 3)]
        if 1200 <= time <= 5200:
            result = dict(time_ms=time, changed_tiles=0, legacy_bytes=0, advanced_bytes=0,
                          legacy_chunks=0, advanced_chunks=0, palette_tiles=0)
            for y in range(15):
                for x in range(15):
                    positions = [((y * 16 + row) * 240 + x * 16 + col)
                                 for row in range(16) for col in range(16)]
                    tile = [pixels[at] for at in positions]
                    if previous is not None and all(previous[at] == pixels[at] for at in positions):
                        continue
                    runs, at = 0, 0
                    while at < 256:
                        run = 1
                        while run < 255 and at + run < 256 and tile[at + run] == tile[at]:
                            run += 1
                        runs += 1
                        at += run
                    legacy = min(512, runs * 3)
                    colors = len(set(tile))
                    bits = 1 if colors <= 2 else 2 if colors <= 4 else 4
                    advanced = min(legacy, 1 + 2 * colors + 32 * bits) if colors <= 16 else legacy
                    assert advanced <= legacy
                    result["changed_tiles"] += 1
                    result["legacy_bytes"] += legacy
                    result["advanced_bytes"] += advanced
                    result["legacy_chunks"] += math.ceil(legacy / 60)
                    result["advanced_chunks"] += math.ceil(advanced / 60)
                    result["palette_tiles"] += advanced < legacy
            frames.append(result)
        previous = pixels
    summary = {key: sum(frame[key] for frame in frames) for key in frames[0] if key != "time_ms"}
    summary.update(snapshot_count=len(frames), source="actual host HOME LCD snapshots",
        legacy_delivery_budget_ms=sum(f["legacy_chunks"] * 4 for f in frames) / len(frames),
        advanced_best_case_delivery_budget_ms=sum(math.ceil(f["advanced_chunks"] / 3) for f in frames) / len(frames),
        hardware_throughput_measured=False)
    assert summary["palette_tiles"] > 0 and summary["advanced_chunks"] < summary["legacy_chunks"]
    report = folder / "home-mirror-bandwidth.json"
    report.write_text(json.dumps({"summary": summary, "snapshots": frames}, indent=2) + "\n")
    print("HOME lossless wire budget:", json.dumps(summary))


def main():
    exe = Path(sys.argv[1]).resolve()
    folder = Path(sys.argv[2]).resolve()
    folder.mkdir(parents=True, exist_ok=True)
    os.environ["TK_SCREEN_VALIDATE"] = "1"
    # The startup image itself comes from real lcd_blit strips and includes
    # dense binary colors. A complete wire reconstruction must match its PPM.
    heartbeat = [sysex(t, 0x0A, i + 1, (1,)) for i, t in enumerate(range(50, 9000, 2000))]
    quiet, _, _ = run(exe, folder, "mirror-splash-off", [], 8500)
    streamed, log, _ = run(exe, folder, "mirror-splash-on", heartbeat, 8500)
    frame, complete = pixels_from_log(log)
    assert len(complete) == 225, "Initial stream failed to refresh every tile"
    assert rgb_bytes(frame) == Path(f"{streamed}-8200.ppm").read_bytes().split(b"\n", 1)[1], "Wire image differs from real LCD splash"
    assert quiet.with_suffix(".wav").read_bytes() == streamed.with_suffix(".wav").read_bytes()
    advanced_heartbeat = [sysex(t, 0x0A, i + 1, (3 if i == 0 else 4,))
                          for i, t in enumerate(range(50, 9000, 2000))]
    advanced, advanced_log, _ = run(exe, folder, "mirror-splash-advanced", advanced_heartbeat, 8500)
    advanced_frame, complete = pixels_from_log(advanced_log)
    assert len(complete) == 225
    assert rgb_bytes(advanced_frame) == Path(f"{advanced}-8200.ppm").read_bytes().split(b"\n", 1)[1]
    assert quiet.with_suffix(".wav").read_bytes() == advanced.with_suffix(".wav").read_bytes()

    # Enter all fifteen pages, including labeled operator/pitch/global/voice
    # pages, and play through HOME/SEQ/ARP navigation. The host checks exact
    # capture versus its retained LCD framebuffer on every main-loop poll.
    pages = ["100 btn 8", "200 keys 0x4480", "1500 keys 0", "1800 btn 6", "2300 btn 6",
             "3000 btn 3", "3500 btn 7", "4000 btn 4", "4500 hold 5 1", "5250 hold 5 0",
             "5300 hold 10 1", "6050 hold 10 0", "6300 btn 11", "6500 btn 8", "6600 btn 6",
             "7000 btn 7", "7100 btn 6"]
    for time, key in ((7500, 1), (8000, 15), (8500, 17), (9000, 20)):
        pages.extend((f"{time} hold 6 1", f"{time + 10} keys {1 << key}",
                      f"{time + 20} keys 0", f"{time + 30} hold 6 0"))
    pages.extend(("10000 hold 2 1", "10750 hold 2 0", "11000 hold 11 1",
                  "11750 hold 11 0", "12200 btn 8"))
    end = 18000
    heartbeats = [sysex(t, 0x0A, i + 1, (1,)) for i, t in enumerate(range(50, end, 2000))]
    augmented = sorted(pages + heartbeats, key=lambda line: int(line.split()[0]))
    plain, plain_log, _ = run(exe, folder, "mirror-pages-off", pages, end)
    queried, queried_log, _ = run(exe, folder, "mirror-pages-on", augmented, end)
    assert plain.with_suffix(".wav").read_bytes() == queried.with_suffix(".wav").read_bytes(), "Screen mirror changed PCM"
    notes = re.compile(r"^.*MIDI out.*$", re.MULTILINE)
    assert notes.findall(plain_log) == notes.findall(queried_log), "Screen mirror changed notes or their timestamps"
    for time in (1000, 4200, 8200):
        assert Path(f"{plain}-{time}.ppm").read_bytes() == Path(f"{queried}-{time}.ppm").read_bytes(), "Screen mirror changed actual screen"
    pixels_from_log(queried_log)  # all changing-page tile chunks stay valid
    advanced_lines = [sysex(t, 0x0A, i + 1, (3 if i == 0 else 4,))
                      for i, t in enumerate(range(50, end, 2000))]
    advanced, advanced_log, _ = run(exe, folder, "mirror-pages-advanced",
        sorted(pages + advanced_lines, key=lambda line: int(line.split()[0])), end)
    assert plain.with_suffix(".wav").read_bytes() == advanced.with_suffix(".wav").read_bytes()
    assert notes.findall(plain_log) == notes.findall(advanced_log)
    pixels_from_log(advanced_log)
    for time in (1000, 4200, 8200):
        assert Path(f"{plain}-{time}.ppm").read_bytes() == Path(f"{advanced}-{time}.ppm").read_bytes()
    home_snapshot_budget(exe, folder)
    print("PASS actual app screen mirror: exact legacy/advanced pixels, all-page capture, PCM/MIDI/timestamp/screens neutrality and actual HOME codec budgets")


if __name__ == "__main__":
    main()
