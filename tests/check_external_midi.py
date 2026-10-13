#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check external MIDI ownership/controllers and SEQ navigation in the host app.

    py tests/check_external_midi.py build/host/terkinstein.exe build/host/external-midi

The simulator feeds decoded USB and rear-input messages. It does not model a
UART, adapter wiring, USB enumeration, audio deadlines or a physical pedal.
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


ROOT = Path(__file__).resolve().parent.parent
MIDI = re.compile(r"^\s*(\d+) ms  MIDI out ([0-9A-F]{2}) (\d+) (\d+)$", re.MULTILINE)
BIND_AT = 3430                         # schema-9 host header + payload offset
BEND_AT = 3454
PATTERNS_AT, PATTERNS_END = 24, 3352


def ordered(*parts: str) -> str:
    return "\n".join(sorted("".join(parts).splitlines(),
                            key=lambda line: int(line.split()[0]))) + "\n"


class Panel:
    def __init__(self, exe: Path, prefix: Path, directory: Path):
        self.exe, self.prefix, self.directory = exe, prefix, directory

    def run(self, label: str, text: str, *, fps: int = 0, store: bytes | None = None):
        script = self.directory / f"{label}.txt"
        settings = self.directory / f"{label}.bin"
        script.write_text(text, encoding="ascii")
        if store is not None:
            settings.write_bytes(store)
        out = self.prefix.with_name(self.prefix.name + "-" + label)
        env = dict(os.environ, TK_HOST_SETTINGS_PATH=str(settings))
        result = subprocess.run([str(self.exe), str(out), str(script), str(fps)],
                                cwd=ROOT, env=env, check=True,
                                capture_output=True, text=True)
        out.with_suffix(".log").write_text(result.stdout, encoding="utf-8")
        midi = [tuple(int(x, 16) if index == 1 else int(x)
                      for index, x in enumerate(match))
                for match in MIDI.findall(result.stdout)]
        with wave.open(str(out) + ".wav", "rb") as wav:
            assert (wav.getnchannels(), wav.getsampwidth(), wav.getframerate()) == (2, 2, 44100)
            samples = wav.readframes(wav.getnframes())
        return samples, midi, settings.read_bytes() if settings.exists() else None, out


def peak(pcm: bytes, begin: int, end: int) -> int:
    values = array.array("h", pcm[begin * 44100 // 1000 * 4:end * 44100 // 1000 * 4])
    if sys.byteorder != "little":
        values.byteswap()
    return max(map(abs, values), default=0)


def seq_navigation(panel: Panel) -> None:
    idle = "10 btn 8\n100 btn 11\n400 btn 11\n700 btn 11\n2200 btn 9\n10000 end\n"
    audio, midi, _, out = panel.run("seq-idle", idle, fps=1)
    assert not midi and peak(audio, 0, 8000) == 0, "opening SEQ started audible/generated notes"
    with Image.open(Path(f"{out}-00001.ppm")) as image:
        image.resize((480, 480), Image.Resampling.NEAREST).save(out.with_suffix(".png"))

    explicit = "10 btn 8\n100 btn 11\n1100 btn 12\n2500 btn 12\n6000 end\n"
    _, generated, _, _ = panel.run("seq-explicit-play", explicit)
    onsets = [ms for ms, status, _, velocity in generated if status == 0x91 and velocity]
    assert onsets and min(onsets) == 1100 and all(ms >= 1100 for ms, *_ in generated), generated

    common = "10 btn 8\n100 btn 11\n100 btn 12\n150 btn 8\n3200 btn 12\n3800 btn 9\n12000 end\n"
    plain = panel.run("seq-playing-plain", common)
    visiting = panel.run("seq-playing-visit", ordered(common,
        "500 btn 11\n800 btn 11\n1200 btn 11\n1600 btn 8\n"))
    assert plain[:3] == visiting[:3], "SEQ page navigation changed playing PCM, MIDI or settings"

    common = "10 btn 8\n100 keys 0x4880\n110 btn 10\n3100 btn 10\n3500 keys 0\n4000 btn 9\n12000 end\n"
    plain = panel.run("arp-playing-plain", common)
    visiting = panel.run("arp-playing-visit", ordered(common,
        "500 btn 11\n800 btn 11\n1200 btn 11\n1600 btn 8\n"))
    assert plain[:3] == visiting[:3], "opening SEQ stopped or altered the running arpeggiator"

    common = ("10 btn 8\n100 btn 11\n110 btn 13\n150 btn 8\n300 keys 0x80\n"
              "850 keys 0\n1200 keys 0x400\n1800 keys 0\n2100 btn 13\n"
              "3100 btn 12\n4000 btn 9\n12000 end\n")
    plain = panel.run("rec-plain", common)
    visiting = panel.run("rec-visit", ordered(common,
        "500 btn 11\n700 btn 11\n1300 btn 11\n1600 btn 8\n"))
    assert plain[:3] == visiting[:3], "SEQ page navigation changed recorded notes, held gates or audio"

    switch = ("10 btn 8\n100 keys 0x4880\n110 btn 10\n500 btn 11\n"
              "1200 btn 12\n2000 keys 0\n2400 btn 12\n6000 end\n")
    _, midi, _, _ = panel.run("arp-to-seq-play", switch)
    assert any(ms < 1200 and status == 0x91 and velocity for ms, status, _, velocity in midi)
    assert any(ms == 1200 and status == 0x91 and note == 48 and velocity
               for ms, status, note, velocity in midi), "PLAY from SEQ did not explicitly start its pattern"
    _, _, _, arp_view = panel.run("seq-over-arp",
        "10 btn 8\n100 keys 0x4880\n110 btn 10\n500 btn 11\n"
        "2100 btn 10\n2200 keys 0\n6000 end\n", fps=1)
    with Image.open(Path(f"{arp_view}-00001.ppm")) as image:
        for row in range(4):
            strip = image.convert("RGB").crop((8, 86 + row * 30, 218, 88 + row * 30))
            rgb = strip.tobytes()
            assert not any(g > 180 and g > r * 5 // 4 and g > b * 5 // 4
                           for r, g, b in zip(rgb[0::3], rgb[1::3], rgb[2::3])), (
                               "SEQ displayed a false playhead while ARP ran")
        image.resize((480, 480), Image.Resampling.NEAREST).save(arp_view.with_suffix(".png"))
    print("SEQ: idle navigation is silent; PLAY starts explicitly; page visits preserve SEQ, ARP and live REC")


def port_ownership(panel: Panel) -> None:
    for live, other in (("midi", "din"), ("din", "midi")):
        common = (f"10 btn 8\n500 {live} 0x90 60 100\n"
                  f"3000 {live} 0x80 60 0\n7000 end\n")
        plain = panel.run(f"{live}-isolated-plain", common)
        unrelated = (f"700 {other} 0xB0 64 127\n800 {other} 0xE0 127 127\n"
                     f"900 {other} 0xB0 1 127\n1000 {other} 0xD0 127 0\n"
                     f"1100 {other} 0xA0 60 127\n1200 {other} 0xB0 7 0\n"
                     f"1300 {other} 0xB0 11 0\n1400 {other} 0xB0 66 127\n"
                     f"1500 {other} 0xB0 67 127\n1600 {other} 0x80 60 0\n"
                     f"1800 {other} 0xB0 123 0\n2000 {other} 0xB0 120 0\n"
                     f"2200 {other} 0xB0 121 0\n")
        touched = panel.run(f"{live}-other-port-controls", ordered(common, unrelated))
        assert plain[0] == touched[0], f"{other} channel 1 controllers/note-off touched {live} channel 1"
        assert not plain[1] and not touched[1], "external input was echoed to USB output"

        # Same physical port, different channels retain independent state too.
        separate_channel = unrelated.replace(other, live).replace("0xB0", "0xB1").replace(
            "0xE0", "0xE1").replace("0xD0", "0xD1").replace("0xA0", "0xA1").replace("0x80", "0x81")
        changed = panel.run(f"{live}-other-channel-controls", ordered(common, separate_channel))
        assert plain[0] == changed[0], f"{live} channel 2 controls touched channel 1"

    common = ("10 btn 8\n500 midi 0x90 60 100\n600 din 0x90 60 100\n"
              "900 midi 0x80 60 0\n1600 din 0x80 60 0\n7000 end\n")
    audio, midi, _, _ = panel.run("same-pitch-two-ports", common)
    usb_only = panel.run("same-pitch-usb-only", common.replace("600 din 0x90 60 100\n", ""))[0]
    assert peak(audio, 1200, 1500) > peak(usb_only, 1200, 1500), (
        "USB release extinguished the same-pitch rear-input voice")
    assert not midi
    print("MIDI ownership: USB/TRS and differing channels keep notes, wheels, pressure and pedals separate; no input echo")


def expressive_controls(panel: Panel) -> None:
    for port in ("midi", "din"):
        common = f"10 btn 8\n500 {port} 0x90 60 100\n3500 {port} 0x80 60 0\n7000 end\n"
        plain = panel.run(f"{port}-performance-plain", common)[0]
        for label, event in (("bend", "0xE0 127 127"), ("mod", "0xB0 1 127"),
                             ("channel-pressure", "0xD0 127 0"), ("poly-pressure", "0xA0 60 127"),
                             ("soft", "0xB0 67 127"), ("brightness", "0xB0 74 0"),
                             ("texture", "0xB0 71 127")):
            changed = panel.run(f"{port}-performance-{label}", ordered(common,
                f"1200 {port} {event}\n"))[0]
            assert plain[1200 * 44100 // 1000 * 4:3000 * 44100 // 1000 * 4] != changed[
                1200 * 44100 // 1000 * 4:3000 * 44100 // 1000 * 4], (
                f"{port} {label} did not affect a sounding note")
            assert 10 < peak(changed, 1800, 3000) < 32767
        for cc in (7, 11):
            muted = panel.run(f"{port}-performance-cc{cc}", ordered(common,
                f"1200 {port} 0xB0 {cc} 0\n"))[0]
            assert peak(muted, 2200, 3000) == 0, f"{port} CC{cc} zero did not mute its voice"
        reset = panel.run(f"{port}-performance-reset", ordered(common,
            f"1200 {port} 0xB0 11 0\n2200 {port} 0xB0 121 0\n"))[0]
        assert peak(reset, 2800, 3300) > 10, f"{port} reset controllers did not restore expression"
    print("Performance: both ports drive smoothed bend/modulation, channel/poly pressure, volume, expression and soft pedal")


def pedals_and_loss(panel: Panel) -> None:
    for port, number in (("midi", 0), ("din", 1)):
        expected = panel.run(f"{port}-pedal-held-reference",
            f"10 btn 8\n500 {port} 0x90 60 100\n2500 {port} 0x80 60 0\n7000 end\n")[0]
        for cc in (64, 66):
            held = panel.run(f"{port}-pedal-{cc}",
                f"10 btn 8\n500 {port} 0x90 60 100\n700 {port} 0xB0 {cc} 127\n"
                f"1000 {port} 0x80 60 0\n2500 {port} 0xB0 {cc} 0\n7000 end\n")[0]
            assert held == expected, f"{port} pedal CC{cc} failed to retain/release the existing note"
        live = "din" if port == "midi" else "midi"
        common = f"10 btn 8\n500 {live} 0x90 60 100\n3000 {live} 0x80 60 0\n7000 end\n"
        plain = panel.run(f"{port}-loss-other-reference", common)[0]
        lost = panel.run(f"{port}-loss-other", ordered(common, f"1200 drop {number}\n"))[0]
        assert plain == lost, "overflow on one MIDI port killed the other port's sound"
        gone = panel.run(f"{port}-loss-held",
            f"10 btn 8\n500 {port} 0x90 60 100\n900 drop {number}\n7000 end\n")[0]
        assert peak(gone, 2200, 3000) == 0, f"lost {port} message left a held note sounding"
        recovered = panel.run(f"{port}-loss-recover",
            f"10 btn 8\n500 {port} 0x90 60 100\n900 drop {number}\n"
            f"1800 {port} 0x90 64 100\n3000 {port} 0x80 64 0\n7000 end\n")[0]
        assert peak(recovered, 2400, 2800) > 10, "input could not play after recovering from queue loss"
    print("Pedals/loss: sustain and sostenuto retain existing notes; port-local panic recovers without harming the other input")


def bindings_and_settings(panel: Panel) -> None:
    _, _, initial, _ = panel.run("settings-factory", "10 btn 8\n100 btn 9\n400 end\n")
    assert initial is not None and len(initial) == 3704
    assert struct.unpack_from("<II", initial) == (3700, 11)
    expected = b"".join(bytes((cc, 2, 16)) for cc in (20, 21, 22, 23, 24, 255, 255, 255))
    assert initial[BIND_AT:BIND_AT + 24] == expected and initial[BEND_AT] == 2

    # Learning consumes its first movement, records exact input/channel and
    # then responds only to that learned input. Reserved performance controls
    # cannot be stolen by the mapping menu.
    _, _, learned, preview = panel.run("learn-rear-shade",
        "10 btn 8\n100 btn 7\n200 btn 6\n300 btn 6\n"
        "400 din 0xB3 28 5\n600 midi 0xB3 28 9\n700 din 0xB2 28 10\n"
        "1500 din 0xB3 28 101\n2200 btn 9\n3000 end\n", fps=1)
    assert learned is not None and learned[BIND_AT:BIND_AT + 3] == bytes((28, 1, 3))
    assert learned[8] == 101, "learned rear controller did not move SHADE"
    with Image.open(Path(f"{preview}-00001.ppm")) as image:
        image.resize((480, 480), Image.Resampling.NEAREST).save(preview.with_suffix(".png"))
    _, _, consumed, _ = panel.run("learn-consumes-first",
        "10 btn 8\n100 btn 7\n200 btn 6\n300 btn 6\n400 din 0xB3 28 5\n"
        "600 midi 0xB3 28 9\n700 din 0xB2 28 10\n900 btn 9\n1300 end\n")
    assert consumed is not None and consumed[8] == initial[8]
    _, _, reserved, _ = panel.run("learn-reserved-wheel",
        "10 btn 8\n100 btn 7\n200 btn 6\n300 btn 6\n400 din 0xB3 1 127\n"
        "500 din 0xB3 31 63\n900 btn 9\n1300 end\n")
    assert reserved is not None and reserved[BIND_AT:BIND_AT + 3] == bytes((31, 1, 3))

    _, _, reloaded, _ = panel.run("learn-reload-save", "10 btn 8\n100 btn 9\n400 end\n", store=learned)
    assert reloaded == learned, "learned surface binding changed on fresh-process SAVE/reload"
    _, _, cleared, _ = panel.run("learn-clear",
        "10 btn 8\n100 btn 7\n200 btn 6\n300 btn 3\n500 btn 9\n900 end\n", store=learned)
    assert cleared is not None and cleared[BIND_AT] == 255

    extra = bytearray(initial)
    extra[BIND_AT + 5 * 3:BIND_AT + 7 * 3] = bytes((80, 2, 16, 81, 2, 16))
    _, _, surface, _ = panel.run("surface-graft-tempo",
        "10 btn 8\n100 din 0xB7 80 127\n200 midi 0xB5 81 127\n"
        "400 btn 9\n800 end\n", store=bytes(extra))
    assert surface is not None and surface[3429] == 127 and surface[16] == 180
    stolen = bytearray(initial)
    stolen[BIND_AT:BIND_AT + 3] = bytes((1, 2, 16))
    _, _, reserved_control, _ = panel.run("reserved-wheel-binding",
        "10 btn 8\n100 midi 0xB0 1 0\n400 btn 9\n800 end\n", store=bytes(stolen))
    assert reserved_control is not None and reserved_control[8] == initial[8], (
        "a mapping stole the reserved modulation-wheel CC")

    song = bytearray(initial)
    song[PATTERNS_AT:PATTERNS_END] = bytes(PATTERNS_END - PATTERNS_AT)
    melody = PATTERNS_AT + 64 * 13
    song[melody:melody + 4] = bytes((1, 48, 100, 64))
    song[BIND_AT + 7 * 3:BIND_AT + 8 * 3] = bytes((80, 2, 16))
    _, transport, saved, _ = panel.run("surface-transport",
        "10 btn 8\n100 btn 11\n400 midi 0xB0 80 0\n500 midi 0xB0 80 127\n"
        "600 midi 0xB0 80 127\n700 midi 0xB0 80 64\n1800 midi 0xB0 80 0\n"
        "1900 midi 0xB0 80 127\n2500 btn 9\n6000 end\n", store=bytes(song))
    onsets = [ms for ms, status, _, velocity in transport if status == 0x91 and velocity]
    assert onsets and 500 <= min(onsets) <= 502 and all(ms < 1902 for ms in onsets), transport
    assert any(1900 <= ms <= 1902 and status == 0x81 for ms, status, *_ in transport), (
        "transport footswitch did not stop after a low rearmed it")
    assert saved is not None and saved[BIND_AT:BIND_AT + 24] == song[BIND_AT:BIND_AT + 24]

    # 0.11 keeps every existing loop/morph offset. Its two old padding bytes
    # do not become controller configuration when schema 8 is migrated.
    old = bytearray(initial[:3432])
    struct.pack_into("<II", old, 0, 3428, 8)
    old[-2:] = b"\0\0"
    _, _, upgraded, _ = panel.run("schema8-migrate", "10 btn 8\n100 btn 9\n500 end\n", store=bytes(old))
    assert upgraded == initial, "0.11 migration changed loops/GRAFT or MIDI defaults"
    _, _, morphed, _ = panel.run("schema8-morph-seed",
        "10 btn 8\n20 btn 6\n100 enc 0 64\n200 btn 9\n500 end\n")
    assert morphed is not None and morphed[3429] == 64
    customized = bytearray(morphed)
    customized[PATTERNS_AT + 63 * 13:PATTERNS_AT + 63 * 13 + 4] = bytes((1, 48, 113, 0))
    customized[3352:3356] = bytes((91, 0, 127, 108))  # frozen forged DRUM patch
    customized[3410:3414] = bytes((64, 32, 64, 16))
    old = bytearray(customized[:3432])
    struct.pack_into("<II", old, 0, 3428, 8)
    old[-2:] = b"\0\0"
    _, _, preserved, _ = panel.run("schema8-custom-migrate",
        "10 btn 8\n100 btn 9\n500 end\n", store=bytes(old))
    assert preserved == customized, "schema-8 migration lost nonzero GRAFT, forged drums or extended loops"
    for label, offset, value in (("port", BIND_AT + 1, 3), ("channel", BIND_AT + 2, 17),
                                 ("cc", BIND_AT, 128), ("bend", BEND_AT, 25)):
        invalid = bytearray(initial)
        invalid[13], invalid[offset] = 3, value
        _, _, fresh, _ = panel.run("reject-binding-" + label,
            "10 btn 8\n100 btn 9\n500 end\n", store=bytes(invalid))
        assert fresh == initial, f"invalid {label} binding was accepted"
    print("Controller surfaces: exact port/channel learn, reserved wheels, persisted mappings, transport switch, schema-8 migration")


def rpn_and_programs(panel: Panel) -> None:
    for port in ("midi", "din"):
        common = f"10 btn 8\n500 {port} 0x90 60 100\n3500 {port} 0x80 60 0\n7000 end\n"
        plain = panel.run(f"{port}-rpn-reference", common)[0]
        select = f"700 {port} 0xB0 101 0\n710 {port} 0xB0 100 0\n800 {port} 0xB0 6 0\n"
        bend = f"1200 {port} 0xE0 127 127\n"
        zero = panel.run(f"{port}-rpn-zero", ordered(common, select, bend))[0]
        assert zero == plain, "RPN 0 bend range zero still changed the pitch"
        wide = panel.run(f"{port}-rpn-octave", ordered(common,
            select.replace("0xB0 6 0", "0xB0 6 12"), bend))[0]
        assert wide != plain and peak(wide, 1600, 2500) > 10
        cents = panel.run(f"{port}-rpn-cents-ignored", ordered(common,
            select.replace("0xB0 6 0", "0xB0 6 12"), f"900 {port} 0xB0 38 99\n", bend))[0]
        assert cents == wide, "unsupported RPN cents altered semitone sensitivity"
        for label, cancel in (("null", f"900 {port} 0xB0 101 127\n910 {port} 0xB0 100 127\n"),
                              ("nrpn", f"900 {port} 0xB0 99 0\n"),
                              ("reset", f"900 {port} 0xB0 121 0\n")):
            changed = panel.run(f"{port}-rpn-{label}", ordered(common, select, cancel,
                f"1000 {port} 0xB0 6 12\n", bend))[0]
            assert changed == plain, f"{port} {label} failed to cancel RPN selection/retain range zero"
        _, _, program, _ = panel.run(f"{port}-program-change",
            f"10 btn 8\n100 {port} 0xC0 30 0\n400 btn 9\n1000 end\n")
        assert program is not None and program[13] == 30
        _, _, ignored, _ = panel.run(f"{port}-program-range",
            f"10 btn 8\n100 {port} 0xC0 64 0\n400 btn 9\n1000 end\n")
        assert ignored is not None and ignored[13] == 0
    print("RPN/program: per-input bend range, null/NRPN/reset cancellation, ignored cents and 64-slot program range")


def pedal_record_gates(panel: Panel) -> None:
    # REC begins explicitly at 100 ms; all physical releases occur at 900 ms.
    # Sustain and sostenuto retain the sounding gate until 1900 ms, and the
    # recorder must retain that musical duration without storing pedal CCs.
    for port in ("midi", "din"):
        for cc in (64, 66):
            _, _, saved, _ = panel.run(f"{port}-pedal-record-{cc}",
                f"10 btn 8\n100 btn 11\n100 btn 13\n400 {port} 0x90 60 100\n"
                f"600 {port} 0xB0 {cc} 127\n900 {port} 0x80 60 0\n"
                f"1900 {port} 0xB0 {cc} 0\n2200 btn 13\n2600 btn 9\n12000 end\n")
            _, _, expected, _ = panel.run(f"{port}-pedal-record-{cc}-reference",
                f"10 btn 8\n100 btn 11\n100 btn 13\n400 {port} 0x90 60 100\n"
                f"1900 {port} 0x80 60 0\n2200 btn 13\n2600 btn 9\n12000 end\n")
            assert saved is not None and expected is not None
            assert saved[PATTERNS_AT:PATTERNS_END] == expected[PATTERNS_AT:PATTERNS_END], (
                f"{port} CC{cc} recording ended the note at key-up instead of pedal release")
    print("Recording: sustain/sostenuto-held notes save their full gate duration on either input")


def realtime_transport(panel: Panel) -> None:
    for port in ("midi", "din"):
        _, midi, _, _ = panel.run(f"{port}-realtime-transport",
            f"10 btn 8\n100 btn 11\n1000 {port} 0xFA 0 0\n2100 {port} 0xFC 0 0\n"
            f"3000 {port} 0xFB 0 0\n4300 {port} 0xFC 0 0\n7000 end\n")
        first = [(ms, pitch) for ms, status, pitch, velocity in midi
                 if status == 0x91 and velocity and ms < 2100]
        resumed = [(ms, pitch) for ms, status, pitch, velocity in midi
                   if status == 0x91 and velocity and ms >= 3000]
        assert first and 1000 <= first[0][0] <= 1002 and first[0][1] == 48
        assert not any(2101 <= ms < 3000 and status == 0x91 and velocity
                       for ms, status, _, velocity in midi)
        assert resumed and 3000 <= resumed[0][0] <= 3002 and resumed[0][1] == 55, resumed
        assert all(ms < 4302 for ms, status, _, velocity in midi if status == 0x91 and velocity)
    print("MIDI transport: Start resets to step one, Stop releases, Continue resumes the next sequencer step")


def last_channel_and_modes(panel: Panel) -> None:
    for port in ("midi", "din"):
        common = f"10 btn 8\n500 {port} 0x9F 60 100\n3000 {port} 0x8F 60 0\n7000 end\n"
        plain = panel.run(f"{port}-channel16-reference", common)[0]
        muted = panel.run(f"{port}-channel16-expression", ordered(common,
            f"1200 {port} 0xBF 11 0\n"))[0]
        assert peak(plain, 2200, 2700) > 10 and peak(muted, 2200, 2700) == 0
        unrelated = panel.run(f"{port}-channel16-isolation", ordered(common,
            f"1200 {port} 0xB0 11 0\n1500 {port} 0x80 60 0\n"))[0]
        assert plain == unrelated, f"{port} channel 1 touched channel 16"

        # All Notes Off honors sustain; All Sound Off ignores it. Neither is
        # a global panic that kills another source or internal sequence lane.
        sustain = (f"10 btn 8\n500 {port} 0x9F 60 100\n700 {port} 0xBF 64 127\n"
                   f"900 {port} 0xBF 123 0\n2500 {port} 0xBF 64 0\n7000 end\n")
        held = panel.run(f"{port}-channel16-all-notes-off", sustain)[0]
        reference = panel.run(f"{port}-channel16-held-reference",
            f"10 btn 8\n500 {port} 0x9F 60 100\n2500 {port} 0x8F 60 0\n7000 end\n")[0]
        assert held == reference
        killed = panel.run(f"{port}-channel16-all-sound-off",
            sustain.replace("0xBF 123 0", "0xBF 120 0"))[0]
        assert peak(killed, 1800, 2300) == 0
    print("Channel 16: independent input ownership, expression and correct pedal-respecting channel-mode messages")


def internal_track_isolation(panel: Panel) -> None:
    common = "10 btn 8\n100 btn 11\n100 btn 12\n3200 btn 12\n4000 btn 9\n12000 end\n"
    plain = panel.run("track-controller-reference", common)
    external = ("500 din 0xB0 7 0\n600 din 0xB0 11 0\n700 din 0xB0 120 0\n"
                "800 midi 0xB1 7 0\n900 midi 0xB1 11 0\n1000 midi 0xB1 123 0\n"
                "1100 din 0xE0 127 127\n1200 midi 0xE1 127 127\n"
                "1300 din 0xB0 64 127\n1400 midi 0xB1 66 127\n")
    controlled = panel.run("track-controller-isolation", ordered(common, external))
    assert plain[0:2] == controlled[0:2], "external controllers altered internal frozen sequencer voices/MIDI"
    changed = panel.run("track-program-isolation", ordered(common,
        "500 din 0xC0 30 0\n1000 midi 0xC1 12 0\n"))
    assert plain[0:2] == changed[0:2], "Program Change reshaped or interrupted an already captured track"
    assert plain[2] is not None and changed[2] is not None
    assert plain[2][PATTERNS_AT:3414] == changed[2][PATTERNS_AT:3414], (
        "Program Change modified saved loops, captured patches or track lengths")
    print("Internal tracks: external wheels/pedals/channel modes and factory Program Change leave captured loops intact")


def mapping_editor_and_priority(panel: Panel) -> None:
    _, _, factory, _ = panel.run("editor-factory", "10 btn 8\n100 btn 9\n400 end\n")
    assert factory is not None
    for current, delta, expected in ((37, 1, 39), (39, -1, 37), (0, 1, 2),
                                     (2, -1, 0), (119, 1, 255), (255, -1, 119),
                                     (255, 1, 0), (0, -1, 255)):
        state = bytearray(factory)
        state[BIND_AT] = current
        _, _, edited, _ = panel.run(f"editor-{current}-{delta}",
            f"10 btn 8\n100 btn 7\n200 btn 6\n300 enc 3 {delta}\n"
            "500 btn 9\n900 end\n", store=bytes(state))
        assert edited is not None and edited[BIND_AT] == expected, (
            "reserved CC blocked normal detent/wrap", current, delta, edited[BIND_AT] if edited else None)
    _, _, bend, _ = panel.run("editor-bend-default",
        "10 btn 8\n100 btn 7\n200 btn 6\n300 enc 2 8\n400 enc 3 5\n"
        "600 btn 9\n1000 end\n")
    assert bend is not None and bend[BEND_AT] == 7
    _, _, centered, _ = panel.run("editor-bend-clear",
        "10 btn 8\n100 btn 7\n200 btn 6\n300 enc 2 8\n400 btn 3\n"
        "600 btn 9\n1000 end\n", store=bend)
    assert centered is not None and centered[BEND_AT] == 2

    _, _, learned, _ = panel.run("editor-exact-duplicate",
        "10 btn 8\n100 btn 7\n200 btn 6\n300 btn 6\n400 din 0xB3 28 1\n"
        "500 enc 2 1\n600 btn 6\n700 din 0xB3 28 2\n900 btn 9\n1300 end\n")
    assert learned is not None and learned[BIND_AT] == 255
    assert learned[BIND_AT + 3:BIND_AT + 6] == bytes((28, 1, 3)), (
        "learning an exact duplicate failed to transfer its binding")

    mappings = bytearray(factory)
    mappings[BIND_AT:BIND_AT + 12] = bytes((28, 2, 16, 28, 1, 16, 28, 2, 3, 28, 1, 3))
    _, _, specific, _ = panel.run("routing-most-specific",
        "10 btn 8\n100 din 0xB3 28 101\n300 btn 9\n700 end\n", store=bytes(mappings))
    assert specific is not None and specific[8:11] == factory[8:11] and specific[11] == 101
    assert specific[BIND_AT:BIND_AT + 12] == mappings[BIND_AT:BIND_AT + 12]
    _, _, channel, _ = panel.run("routing-channel-specific",
        "10 btn 8\n100 midi 0xB3 28 102\n300 btn 9\n700 end\n", store=bytes(mappings))
    assert channel is not None and channel[10] == 102
    assert channel[8:10] == factory[8:10] and channel[11] == factory[11]
    _, _, physical, _ = panel.run("routing-port-specific",
        "10 btn 8\n100 din 0xB4 28 103\n300 btn 9\n700 end\n", store=bytes(mappings))
    assert physical is not None and physical[9] == 103 and physical[8] == factory[8]
    assert physical[10:12] == factory[10:12]
    print("MIDI editor: reserved CCs skip/wrap; bend default saves; exact duplicate moves; most-specific route wins")


def transport_latch_port_isolation(panel: Panel) -> None:
    _, _, factory, _ = panel.run("latch-factory", "10 btn 8\n100 btn 9\n400 end\n")
    assert factory is not None
    for port, number, other in (("midi", 0, "din"), ("din", 1, "midi")):
        song = bytearray(factory)
        song[PATTERNS_AT:PATTERNS_END] = bytes(PATTERNS_END - PATTERNS_AT)
        melody = PATTERNS_AT + 64 * 13
        song[melody:melody + 4] = bytes((1, 48, 100, 64))
        song[BIND_AT + 7 * 3:BIND_AT + 8 * 3] = bytes((80, number, 16))
        resets = [("drop", f"800 drop {1 - number}\n")]
        if other == "din":  # Inherited USB forwarding excludes System Reset.
            resets.append(("reset", f"800 {other} 0xFF 0 0\n"))
        for label, reset in resets:
            script = (f"10 btn 8\n100 btn 11\n500 {port} 0xB0 80 127\n" + reset +
                      f"900 {port} 0xB0 80 127\n1500 {port} 0xB0 80 0\n"
                      f"1900 {port} 0xB0 80 127\n2500 btn 9\n6000 end\n")
            _, midi, _, _ = panel.run(f"{port}-transport-latch-other-{label}", script, store=bytes(song))
            onsets = [ms for ms, status, pitch, velocity in midi
                      if status == 0x91 and pitch == 48 and velocity]
            releases = [ms for ms, status, pitch, _ in midi if status == 0x81 and pitch == 48]
            assert len(onsets) == len(releases) == 1, (
                "other input reset rearmed an unrelated transport switch", port, label, midi)
            assert 500 <= onsets[0] <= 502 and 1900 <= releases[0] <= 1902, (
                "held-high footswitch toggled before its own low/high edge", port, label, midi)
    print("Transport latch: loss/reset on the other input cannot rearm a port-filtered held footswitch")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("prefix", type=Path)
    args = parser.parse_args()
    exe, prefix = args.executable.resolve(), args.prefix.resolve()
    prefix.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="tk-external-", dir=prefix.parent) as tmp:
        panel = Panel(exe, prefix, Path(tmp))
        seq_navigation(panel)
        port_ownership(panel)
        expressive_controls(panel)
        pedals_and_loss(panel)
        bindings_and_settings(panel)
        rpn_and_programs(panel)
        pedal_record_gates(panel)
        realtime_transport(panel)
        last_channel_and_modes(panel)
        internal_track_isolation(panel)
        mapping_editor_and_priority(panel)
        transport_latch_port_isolation(panel)
    for path in prefix.parent.glob(prefix.name + "-*.ppm"):
        path.unlink()
    print("PASS: external MIDI and explicit sequencer transport (host only)")


if __name__ == "__main__":
    main()
