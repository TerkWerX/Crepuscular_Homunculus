#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual-main contextual page knobs, saved ownership and visual previews.

Host checks do not qualify physical controls, LCD delays or target deadlines.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import re
import struct

from PIL import Image
from check_external_midi import Panel, ordered, peak
from check_remote import decode, sysex


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    folder = args.output.resolve()
    folder.mkdir(parents=True, exist_ok=True)
    panel = Panel(args.executable.resolve(), folder / "knobs", folder)
    checks = 0

    def check(condition, message):
        nonlocal checks
        checks += 1
        assert condition, message

    def run(label, events, *, end=1800, store=None, fps=0):
        settings = folder / f"{label}.bin"
        if store is None and settings.exists():
            settings.unlink()
        return panel.run(label, ordered(*[line + "\n" for line in events], f"{end} end\n"),
                         store=store, fps=fps)

    def state(result):
        responses = {}
        for match in re.finditer(r"\d+ ms\s+REMOTE out ([0-9A-F]+)",
                                result[3].with_suffix(".log").read_text()):
            command, rid, payload = decode(bytes.fromhex(match[1]))
            check(payload and payload[0] == 0, f"Protocol query {command}/{rid} failed")
            responses[rid] = payload
        return responses

    def preview(result):
        # SAVE returns HOME. Inspect the settled menu before that navigation,
        # rather than silently delivering a HOME screenshot as a page preview.
        frames = sorted(result[3].parent.glob(result[3].name + "-*.ppm"))
        saved = re.search(r"(\d+) ms\s+settings saved", result[3].with_suffix(".log").read_text())
        index = min(len(frames) - 1, (int(saved[1]) - 1) // 500) if saved else len(frames) - 1
        with Image.open(frames[index]) as image:
            image.resize((480, 480), Image.Resampling.NEAREST).save(result[3].with_suffix(".png"))

    # Actual encoder dispatch, not direct helper calls: each requested page
    # edits its named field while frozen sounds and note patterns stay intact.
    scenarios = [
        ("memory", 0, 3, (15, 8, 9, 11)),
        ("envelope-pad", 0, 4, (22, 23, 11)),
        ("envelope-fm", 32, 4, (22, 23, 11)),
        ("envelope-metal", 11, 4, (22, 11, 10)),
        ("envelope-kit", 30, 4, (10, 8, 9, 11)),
        ("lfo-pad", 0, 5, (17, 18, 19, 8)),
        ("lfo-metal", 11, 5, (17, 18, 19, 8)),
        ("lfo-fm", 32, 5, (17, 18, 19, 8)),
    ]
    for label, preset, button, offsets in scenarios:
        setup = ["100 btn 8", sysex(150, 0x16, 1, (preset,))]
        setup += ["200 hold 5 1", "950 hold 5 0"] if button == 5 else [f"200 btn {button}"]
        original = run(label + "-original", setup + ["1600 btn 9"])
        for knob, offset in enumerate(offsets):
            edited = run(f"{label}-k{knob + 1}", setup + [f"1000 enc {knob + 2} 7", "1600 btn 9"])
            check(edited[2] is not None, f"{label} did not complete silent SAVE")
            expected = (original[2][offset] + 7) % 3 if offset == 19 else min(127, original[2][offset] + 7)
            check(edited[2][offset] == expected, f"{label} Knob{knob + 1} missed its named field")
            for other in (8, 9, 10, 11, 15, 17, 18, 19, 22, 23):
                if other != offset:
                    check(edited[2][other] == original[2][other], f"{label} changed unrelated live field {other}")
            check(edited[2][24:3408] == original[2][24:3408], f"{label} changed patterns/captured sounds")
            check(edited[2][3496:3656] == original[2][3496:3656], f"{label} changed captured FM operators")
            check(struct.unpack_from("<II", edited[2]) == struct.unpack_from("<II", original[2]),
                  "Page knobs changed current save format")
        all_turns = [f"{1000 + knob * 35} enc {2 + knob} 7" for knob in range(len(offsets))]
        picture = run(label + "-page", setup + all_turns + ["1600 btn 9"], fps=2)
        preview(picture)
        reloaded = run(label + "-reload", ["100 btn 8", "1000 btn 9"], store=picture[2])
        check(reloaded[2] == picture[2], f"{label} failed fresh-process SAVE/reload")

    # Unavailable controls explicitly consume turns; they do not fall through
    # to hidden sound macros. Read-only diagnostics do the same for all sounds.
    for preset in (0, 11, 30, 32):
        cases = [("diagnostics", 7)]
        if preset:
            cases.append(("memory-unavailable", 3))
        if preset == 30:
            cases.append(("lfo-unavailable", 5))
        for label, button in cases:
            setup = ["100 btn 8", sysex(150, 0x16, 1, (preset,))]
            setup += ["200 hold 5 1", "950 hold 5 0"] if button == 5 else [f"200 btn {button}"]
            plain = run(f"{label}-{preset}-plain", setup + ["1600 btn 9"])
            turns = [f"{1000 + knob * 35} enc {2 + knob} 32767" for knob in range(4)]
            touched = run(f"{label}-{preset}-turned", setup + turns + ["1600 btn 9"], fps=2)
            check(touched[:3] == plain[:3], f"{label} silently changed PCM, MIDI or settings")
            preview(touched)

    # HOLD sends only two CC64 edges, at the actual transitions. Outward
    # saturation must not re-send either pedal value or dirty stored settings.
    hold_setup = ["100 btn 8", "200 btn 4", "900 btn 9"]
    plain = run("hold-plain", hold_setup)
    touched = run("hold-edges", hold_setup + ["300 enc 5 1", "350 enc 5 32767",
                  "500 enc 5 -1", "550 enc 5 -32767"])
    check(touched[1] == [(300, 0xB0, 64, 127), (500, 0xB0, 64, 0)],
          "HOLD did not send precisely its two panel sustain transitions")
    check(touched[2] == plain[2], "Transient HOLD rewrote persistent settings")

    # A pedal enabled on a pitched sound must remain releasable after choosing
    # the kit, whose fourth knob now edits BITE rather than panel HOLD.
    kit_hold = ["100 btn 8", "150 midi 0x90 60 100", "200 btn 4",
                "220 keys 0x80", "300 enc 5 1", "400 keys 0",
                sysex(500, 0x16, 1, (30,)), "1800 midi 0x80 60 0", "3000 btn 9"]
    released = run("kit-release-existing-hold", kit_hold + ["700 btn 6", "750 btn 6"], end=4000)
    external_only = run("kit-release-external-reference", [line for line in kit_hold if "keys" not in line], end=4000)
    check(released[1] == [(220, 0x90, 48, 100), (300, 0xB0, 64, 127),
                          (400, 0x80, 48, 0), (700, 0xB0, 64, 0)],
          "KIT EDIT did not release the existing panel pedal exactly once")
    check(released[2] is not None and released[2] == external_only[2],
          "KIT hold release changed saved drum settings or blocked the later silent SAVE")
    check(peak(released[0], 1500, 1700) > 0 and
          released[0][1500 * 44100 // 1000 * 4:1700 * 44100 // 1000 * 4] ==
          external_only[0][1500 * 44100 // 1000 * 4:1700 * 44100 // 1000 * 4],
          "KIT EDIT released an external held note or left an extra panel sustain voice")
    check(peak(released[0], 3000, 3900) == 0,
          "The old pitched panel note remained sustained after KIT EDIT released HOLD")

    arp_setup = ["100 btn 8", "200 keys 0x4480", "250 btn 10", "300 hold 10 1", "1050 hold 10 0",
                 "1400 keys 0", "1500 btn 10", "6500 btn 9"]
    baseline = run("arp-original", arp_setup, end=7000)
    all_arp = run("arp-four-knobs", arp_setup + ["1100 enc 2 7", "1140 enc 3 1",
                  "1180 enc 4 1", "1220 enc 5 1"], end=7000, fps=2)
    check(all_arp[2][16] == baseline[2][16] + 7, "ARP Knob1 did not edit BPM")
    check(all_arp[2][21] != baseline[2][21], "ARP order/rate/gate knobs did not update configuration")
    check(all_arp[2][8:16] == baseline[2][8:16], "ARP controls edited hidden live macros")
    check(all_arp[2][24:3408] == baseline[2][24:3408], "ARP controls changed captured loops")
    preview(all_arp)

    # Each track is reachable with SELECT, including BASS, while the same live
    # preset is kept for auditioning. Navigation never starts transport.
    seq_setup = ["100 btn 8", "200 btn 11", sysex(300, 0x02, 1)]
    nav = run("seq-track-navigation", seq_setup + ["400 enc 0 2", sysex(500, 0x02, 2), "1600 btn 9"], fps=2)
    before, after = state(nav)[1], state(nav)[2]
    check(after[4] == 3, "SEQ SELECT did not select BASS")
    check(after[10] == before[10] and after[19:30] == before[19:30], "SEQ track navigation changed the auditioned live sound")
    check(not after[3] & 1 and not nav[1] and peak(nav[0], 0, 1400) == 0,
          "SEQ track navigation started playback")
    preview(nav)
    seq = run("seq-four-knobs", seq_setup + ["400 enc 2 7", "440 enc 3 2",
              "480 enc 4 2", "520 enc 5 127", sysex(600, 0x02, 2), "1600 btn 9"], fps=2)
    old, new = state(seq)[1], state(seq)[2]
    check((new[7] | new[8] << 7) == (old[7] | old[8] << 7) + 7, "SEQ Knob1 did not edit BPM")
    check(new[4] == old[4] and new[11 + new[4]] == 2 and new[6] == old[6] and new[26] == 127,
          "SEQ length/MASK missed fields or Knob2/3 still navigated tracks")
    check(new[19:26] == old[19:26], "SEQ controls changed hidden live sound macros/GRAFT")
    preview(seq)
    step = run("seq-step-entry", ["100 btn 8", "200 btn 11", "250 btn 6",
               "400 enc 4 5", "440 enc 2 7", sysex(500, 0x02, 1), "1600 btn 9"], fps=2)
    check(state(step)[1][6] == 5 and not state(step)[1][3] & 1, "SEQ edit Knob3 missed cursor or started playback")
    check((state(step)[1][7] | state(step)[1][8] << 7) == (old[7] | old[8] << 7) + 7,
          "SEQ edit Knob1 lost BPM ownership")
    preview(step)

    # ALGORITHM owns banks, not tracks: stopped changes apply immediately,
    # playing changes cue at the selected loop boundary and can be cancelled.
    bank_setup = ["100 btn 8", "200 btn 11", sysex(220, 0x20, 10, (1, 1, 63, 1, 70, 90, 64, 0)),
                  sysex(260, 0x21, 11, (1, 1, 2)), sysex(300, 0x02, 1), sysex(350, 0x07, 2)]
    bank = run("seq-algorithm-bank", bank_setup + ["400 enc 1 1", sysex(500, 0x07, 3),
               sysex(550, 0x02, 4), sysex(600, 0x08, 5, (1, 1, 63)), "1600 btn 9"], fps=2)
    reply = state(bank)
    check(reply[2][6] == 0 and reply[3][6] == 1 and reply[3][10] == 4,
          "Stopped ALGORITHM bank selection did not confirm A-to-B without a queued bank")
    check(reply[4][4] == 1 and reply[4][12] == 2 and not reply[4][3] & 1,
          "ALGORITHM changed selected track, missed bank length or started playback")
    check(reply[4][10] == reply[1][10] and reply[4][15:30] == reply[1][15:30],
          "Bank selection changed auditioned preset, GRAFT or captured sound")
    check(reply[5][5:] == bytes((70, 90, 64, 0)), "ALGORITHM lost B64's hidden final note")
    check(not bank[1] and peak(bank[0], 0, 1800) == 0, "Stopped bank browsing emitted notes")
    preview(bank)
    cue = run("seq-algorithm-queue-cancel", bank_setup + ["400 btn 12", "500 enc 1 1",
              sysex(600, 0x07, 3), "650 enc 1 -1", sysex(700, 0x07, 4), "800 enc 1 1",
              sysex(850, 0x07, 5), sysex(3300, 0x07, 6), sysex(3350, 0x02, 7),
              "3400 btn 12", "7000 btn 9"], end=7500)
    reply = state(cue)
    check(reply[3][6] == 0 and reply[3][10] == 1, "ALGORITHM announced a queued bank active too early")
    check(reply[4][6] == 0 and reply[4][10] == 4, "Returning ALGORITHM to A failed to cancel pending B")
    check(reply[5][6] == 0 and reply[5][10] == 1 and reply[6][6] == 1 and reply[6][10] == 4,
          "Requeued B did not activate at the sixteen-step boundary")
    check(reply[7][4] == 1 and reply[7][3] & 1 and reply[7][15:30] == reply[1][15:30],
          "Bank activation stopped playback or replaced the auditioned/captured sound")

    # The page has no duplicate SELECT/ALGORITHM parameter aliases. Exercise
    # actual main dispatch with GRAFT active, including non-FM inactive pages.
    def label_shortcut(key):
        return ["200 hold 6 1", f"220 keys {1 << key}", "270 hold 6 0", "290 keys 0"]

    aliases = (("memory", ["200 btn 3"]), ("env", ["200 btn 4"]),
               ("lfo", ["200 hold 5 1", "950 hold 5 0"]),
               ("arp", ["200 hold 10 1", "950 hold 10 0"]),
               ("midi", ["200 btn 7", "250 btn 6"]),
               ("pitch", label_shortcut(15)), ("global", label_shortcut(17)),
               ("voice", label_shortcut(22)))
    for preset in (0, 11, 30, 32):
        for label, enter in aliases:
            # Set GRAFT on SOUND EDIT before entering a page. The kit has no
            # GRAFT; its encoder turns must nevertheless remain harmless.
            setup = ["10 btn 8", sysex(30, 0x16, 11, (preset,))]
            if preset != 30:
                setup += ["50 btn 6", "80 enc 0 64", "100 btn 8"]
            setup += enter + [sysex(1200, 0x02, 1)]
            plain = run(f"alias-{label}-{preset}-plain", setup + [sysex(1500, 0x02, 2), "1600 btn 9"])
            touched = run(f"alias-{label}-{preset}-turn", setup + ["1300 enc 0 32767", "1350 enc 0 -32767",
                          "1400 enc 1 32767", "1450 enc 1 -32767", sysex(1500, 0x02, 2), "1600 btn 9"])
            check(touched[:3] == plain[:3], f"{label}/{preset} kept a duplicate encoder duty or changed PCM/SAVE")
            before_state, after_state = state(touched)[1], state(touched)[2]
            check(before_state == after_state, f"{label}/{preset} no-op encoders dirtied or navigated state")

    # HOME's afterimage SELECT is meaningful only for PAD. Other engines
    # consume it rather than silently baking GRAFT or editing unused memory.
    for preset in (11, 30, 32):
        setup = ["100 btn 8", sysex(150, 0x16, 11, (preset,)), sysex(200, 0x02, 1)]
        plain = run(f"home-memory-inactive-{preset}-plain", setup + ["1000 btn 9"])
        touched = run(f"home-memory-inactive-{preset}-turn", setup + ["300 enc 0 32767",
                      "350 enc 0 -32767", sysex(400, 0x02, 2), "1000 btn 9"])
        check(touched[:3] == plain[:3] and state(touched)[1] == state(touched)[2],
              f"HOME SELECT changed inactive afterimage or dirty state on preset {preset}")

    # REC and import consume track/bank/length/cursor gestures. This catches
    # a main-loop fallback that direct helper-only coverage would miss.
    rec_setup = ["100 btn 8", "200 btn 11", "250 btn 13", sysex(300, 0x02, 1),
                 sysex(320, 0x07, 2), "1200 btn 13", "1400 btn 12", "7000 btn 9"]
    rec_plain = run("seq-rec-lock-plain", rec_setup, end=7500)
    rec_turn = run("seq-rec-lock-turn", rec_setup + ["400 enc 0 1", "450 enc 1 1", "500 enc 3 2",
                   "550 enc 4 5", sysex(600, 0x02, 3), sysex(650, 0x07, 4)], end=7500)
    check(rec_turn[:3] == rec_plain[:3], "REC track/bank/length/cursor turns changed playback or captured RAM")
    rec_reply = state(rec_turn)
    check(rec_reply[1][4] == rec_reply[3][4] and rec_reply[1][6:30] == rec_reply[3][6:30]
          and rec_reply[2][5:13] == rec_reply[4][5:13], "REC ownership locks escaped main dispatch")
    imported = run("seq-import-knob-lock", ["100 btn 8", "200 btn 11", "250 btn 6",
                   sysex(300, 0x02, 1), sysex(320, 0x07, 2), sysex(350, 0x24, 10)] +
                   [f"{400 + encoder * 35} enc {encoder} 32767" for encoder in range(6)] +
                   [sysex(700, 0x02, 3), sysex(720, 0x07, 4), sysex(800, 0x2A, 11), "1000 btn 9"])
    reply = state(imported)
    check(reply[1][2:30] == reply[3][2:30] and reply[2][5:13] == reply[4][5:13],
          "Staged import failed to consume SEQ's complete six-encoder surface")

    # SEQ BPM still controls a running arpeggiator's period correctly. Query
    # navigation is quiet; only the requested K1 turn changes pulse timing.
    arp_seq_common = ["100 btn 8", "200 keys 0x4480", "250 btn 10", "300 btn 11",
                      sysex(350, 0x02, 1), "1400 keys 0", "1500 btn 10", "6500 btn 9"]
    arp_seq = run("seq-bpm-under-arp", arp_seq_common + ["500 enc 2 40", sysex(550, 0x02, 2)], end=7000)
    reply = state(arp_seq)
    check(reply[1][2] == reply[2][2] == 1 and reply[2][3] & 1,
          "SEQ K1 BPM switched or stopped an active ARP")
    check((reply[2][7] | reply[2][8] << 7) == (reply[1][7] | reply[1][8] << 7) + 40,
          "SEQ K1 did not change tempo under ARP")
    pulses = [ms for ms, status, _, velocity in arp_seq[1] if status == 0x91 and velocity and 550 <= ms < 1400]
    period = 15000 // (reply[2][7] | reply[2][8] << 7)
    check(len(pulses) >= 4 and all(b - a in (period, period + 1) for a, b in zip(pulses, pulses[1:])),
          "SEQ BPM used wrong period math for the running arpeggiator")

    # MIDI routing exposes all four knobs and never modifies live tone.
    setup = ["100 btn 8", "200 btn 7", "250 btn 6"]
    plain = run("midi-plain", setup + ["1600 btn 9"])
    routing = run("midi-four-knobs", setup + ["400 enc 2 1", "440 enc 3 1",
                  "480 enc 4 -1", "520 enc 5 -1", "1600 btn 9"], fps=2)
    check(routing[2][3433:3436] == bytes((22, 1, 15)), "MIDI target/CC/port/channel knobs missed selected binding")
    check(routing[2][8:3430] == plain[2][8:3430], "MIDI knobs changed hidden live sound or sequencer fields")
    preview(routing)
    bend = run("midi-bend-target", setup + ["400 enc 2 32767", "440 enc 3 32767",
               "480 enc 4 -32767", "520 enc 5 -32767", "1600 btn 9"], fps=2)
    check(bend[2][3454] == 24, "MIDI Knob2 missed BEND RANGE target")
    check(bend[2][3430:3454] == plain[2][3430:3454], "Inactive bend route knobs changed CC mappings")
    preview(bend)
    print(f"PASS actual-main page knobs: {checks} field, ownership, sustain, transport, SAVE/reload and preview checks; host only")


if __name__ == "__main__":
    main()
