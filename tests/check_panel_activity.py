#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual-main physical-panel telemetry and pixel/PCM neutrality checks.

The simulator covers physical edge routing, not real USB refresh or hardware
timing. Generic MIDI is deliberately excluded from the physical panel masks.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re

from check_external_midi import Panel, ordered
from check_remote import decode, sysex


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    folder = args.output.resolve()
    folder.mkdir(parents=True, exist_ok=True)
    panel = Panel(args.executable.resolve(), folder / "activity", folder)
    checks = 0

    def check(good, why):
        nonlocal checks
        checks += 1
        assert good, why

    def run(label, events, *, end=2500, store=None):
        old_store = folder / f"{label}.bin"
        if old_store.exists():
            old_store.unlink()
        return panel.run(label, ordered(*[line + "\n" for line in events], f"{end} end\n"), store=store)

    def responses(result):
        answers = {}
        for match in re.finditer(r"(\d+) ms\s+REMOTE out ([0-9A-F]+)", result[3].with_suffix(".log").read_text()):
            command, request_id, payload = decode(bytes.fromhex(match[2]))
            answers[request_id] = (int(match[1]), command, payload)
        return answers

    def activity(answer, buttons=0, notes=0, encoders=0, octave=1, held_buttons=0):
        payload = answer[2]
        check(answer[1] == 0x0E and len(payload) == 12 and payload[:2] == bytes((0, 2)),
              "GET_PANEL_ACTIVITY reply has status and eleven-byte version-two body")
        decoded_buttons = payload[2] | payload[3] << 7
        decoded_notes = sum(payload[4 + index] << (7 * index) for index in range(4))
        decoded_held = payload[10] | payload[11] << 7
        check((decoded_buttons, decoded_notes, payload[8], payload[9], decoded_held)
              == (buttons, notes, encoders, octave, held_buttons),
              f"Unexpected activity: {payload.hex()}, expected {buttons}/{notes}/{encoders}/{octave}/{held_buttons}")
        check(not decoded_held & ~decoded_buttons, "held button mask is a subset of visible activity")

    keys = 1 | 1 << 13 | 1 << 26
    result = run("physical-events", [sysex(50, 0x01, 1), sysex(70, 0x0E, 2),
        "100 hold 8 1", sysex(150, 0x0E, 3), sysex(350, 0x0E, 4),
        "400 hold 8 0", sysex(450, 0x0E, 5), "500 btn 4", sysex(550, 0x0E, 6),
        sysex(700, 0x0E, 7), f"750 keys {keys}", sysex(780, 0x0E, 8),
        "800 enc 0 1", sysex(850, 0x0E, 9), "900 enc 1 -1", sysex(950, 0x0E, 10),
        sysex(980, 0x0E, 11), "1000 keys 0", sysex(1080, 0x0E, 12),
        "1100 midi 0x90 60 100", sysex(1200, 0x0E, 13), "1250 midi 0x80 60 0",
        "1300 btn 1", sysex(1360, 0x0E, 14), sysex(1500, 0x0E, 15),
        sysex(1600, 0x0E, 16, (0,)), "2000 btn 9"])
    answer = responses(result)
    check(answer[1][2][3] & 64, "HELLO explicitly advertises physical panel activity")
    activity(answer[2])
    activity(answer[3], buttons=1 << 8, held_buttons=1 << 8)
    activity(answer[4], buttons=1 << 8, held_buttons=1 << 8)
    activity(answer[5])
    activity(answer[6], buttons=1 << 4)
    activity(answer[7])
    activity(answer[8], notes=keys)
    activity(answer[9], notes=keys, encoders=1)
    activity(answer[10], notes=keys, encoders=3)
    activity(answer[11], notes=keys, encoders=2)
    activity(answer[12])
    activity(answer[13])
    activity(answer[14], buttons=2, octave=2)
    activity(answer[15], octave=2)
    check(answer[16][2] == bytes((2,)), "GET_PANEL_ACTIVITY rejects malformed request body")

    # EDIT shortcut illumination follows the actual hold, never its lingering
    # press pulse. A malformed metadata query cannot consume that hold.
    edit = 1 << 6
    held = run("edit-held-and-tapped", ["100 hold 6 1", sysex(101, 0x0E, 1),
        sysex(150, 0x0E, 2, (0,)), sysex(200, 0x0E, 3), "250 hold 6 0",
        sysex(251, 0x0E, 4), sysex(400, 0x0E, 5), "500 hold 6 1",
        "501 hold 6 0", sysex(502, 0x0E, 6), sysex(660, 0x0E, 7),
        sysex(661, 0x0E, 8), "900 hold 6 1", sysex(1100, 0x0E, 9),
        "1200 hold 6 0", sysex(1201, 0x0E, 10), "1800 btn 9"])
    held_answers = responses(held)
    activity(held_answers[1], buttons=edit, held_buttons=edit)
    check(held_answers[2][2] == bytes((2,)), "malformed query rejects metadata while EDIT is held")
    activity(held_answers[3], buttons=edit, held_buttons=edit)
    activity(held_answers[4], buttons=edit)
    activity(held_answers[5])
    activity(held_answers[6], buttons=edit)
    activity(held_answers[7], buttons=edit)
    activity(held_answers[8])
    activity(held_answers[9], buttons=edit, held_buttons=edit)
    activity(held_answers[10])

    # Four primary encoder pulses remain visible even on pages where their
    # detents are consumed. They reflect physical input before acceleration.
    for encoder in range(7):
        trial = run(f"encoder-{encoder}", ["50 btn 4", f"300 enc {encoder} -1",
            sysex(340, 0x0E, 1), sysex(500, 0x0E, 2), "1000 btn 9"], end=1500)
        answers = responses(trial)
        activity(answers[1], encoders=1 << encoder)
        activity(answers[2])

    # A MIDI PLAY binding can generate a virtual panel action. It must never
    # masquerade as an actual physical button or key in this query.
    initial = run("silent-factory", ["10 btn 8", "100 btn 9"], end=500)
    check(initial[2] is not None, "factory settings were saved for a MIDI binding fixture")
    midi_bound = bytearray(initial[2])
    midi_bound[3430 + 7 * 3:3430 + 8 * 3] = bytes((80, 2, 16))
    trial = run("remote-midi-routing", ["50 btn 8", "300 midi 0xB0 80 127",
        sysex(350, 0x0E, 1), sysex(370, 0x02, 2), "700 midi 0xB0 80 0",
        "900 btn 12", "1500 btn 9"], store=bytes(midi_bound))
    answers = responses(trial)
    activity(answers[1])
    check(answers[2][2][2] == 2 and answers[2][2][3] & 1,
          "MIDI PLAY binding exercised a real remote button rather than an idle fixture")

    # Include held local notes, held EDIT with a shortcut key, detents, physical
    # SAVE and a real note stream. Query traffic must preserve audio, MIDI,
    # settings and exact framebuffer pixels.
    baseline = ["100 btn 8", "200 keys 0x91", "400 enc 2 1", "500 enc 3 1",
        "700 hold 6 1", "750 keys 0x93", "850 keys 0x91", "900 hold 6 0",
        "1300 keys 0", "1800 btn 9"]
    queries = [sysex(time, 0x0E, index + 1) for index, time in enumerate(range(50, 2300, 50))]
    original = run("pcm-pixel-baseline", baseline, end=8500)
    queried = run("pcm-pixel-queried", baseline + queries, end=8500)
    check(original[0] == queried[0], "telemetry traffic changed actual-main output PCM")
    check(original[1] == queried[1], "telemetry traffic changed outgoing musical MIDI")
    check(original[2] == queried[2] and original[2] is not None,
          "telemetry traffic changed saved settings or save completion")
    for time in (1000, 4200, 8200):
        check(Path(f"{original[3]}-{time}.ppm").read_bytes() == Path(f"{queried[3]}-{time}.ppm").read_bytes(),
              "telemetry traffic changed framebuffer pixels")
    answers = responses(queried)
    check(len(answers) == len(queries), "bounded repeated activity queries all completed")
    for _, command, payload in answers.values():
        check(command == 0x0E and len(payload) == 12 and payload[:2] == bytes((0, 2)),
              "repeated telemetry returned inconsistent framing")
    report = {"host_only": True, "hardware_qualified": False, "checks": checks,
              "physical_panel_masks": "pass", "exact_held_edit_and_quick_release": "pass",
              "activity_version": 2, "activity_body_bytes": 11, "malformed_query": "pass",
              "pcm_midi_saved_settings_pixel_neutrality": "pass"}
    (folder / "panel-activity-report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"PASS actual-main panel activity: {checks} checks")


if __name__ == "__main__":
    main()
