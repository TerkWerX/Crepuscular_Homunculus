#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual-main tap/hold menus without unintended effects or transport changes.

Host checks do not qualify physical debounce, LCD latency or audio deadlines.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import re

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
    panel = Panel(args.executable.resolve(), folder / "buttons", folder)
    checks = 0

    def check(condition, message):
        nonlocal checks
        checks += 1
        assert condition, message

    def run(label, events, *, end=2500, fps=0):
        saved = folder / f"{label}.bin"
        if saved.exists():
            saved.unlink()
        return panel.run(label, ordered(*[line + "\n" for line in events], f"{end} end\n"), fps=fps)

    def replies(result):
        answers = {}
        for match in re.finditer(r"\d+ ms\s+REMOTE out ([0-9A-F]+)", result[3].with_suffix(".log").read_text()):
            command, rid, body = decode(bytes.fromhex(match[1]))
            check(body and body[0] == 0, f"Query {command}/{rid} failed")
            answers[rid] = body
        return answers

    quiet = ["10 btn 8", sysex(50, 0x02, 1), "2000 btn 9"]
    baseline = run("quiet-reference", quiet)
    initial = replies(baseline)[1]
    check(baseline[2] is not None, "Quiet reference did not finish SAVE")
    for button, page, name in ((5, 4, "lfo"), (10, 5, "arp"), (11, 14, "seq")):
        for duration in (699, 700, 701):
            case = run(f"{name}-release-{duration}", quiet + [f"100 hold {button} 1",
                       sysex(300, 0x02, 2), f"{100 + duration} hold {button} 0", sysex(900, 0x02, 3)], fps=2)
            before, after = replies(case)[2], replies(case)[3]
            check(before[72] == 0 and before[2:30] == initial[2:30],
                  f"{name} press acted before release or hold threshold")
            if duration < 700:
                if button == 5:
                    check(after[72] == 0 and case[2][20] == 1 - baseline[2][20],
                          "LFO short release did not toggle only enable while staying on HOME")
                    changed = bytearray(baseline[2]); changed[20] ^= 1
                    # With zero GRAFT, the existing capture maintenance keeps
                    # its effective base enable flag consistent with live.
                    changed[3423] ^= 1
                    check(case[2] == bytes(changed), "LFO short tap changed unrelated live/captured fields")
                elif button == 10:
                    check(after[72] == 0 and after[2] == 1 and after[3] & 1,
                          "ARP short release did not toggle playback while retaining HOME")
                    check(case[2] == baseline[2], "ARP short tap changed saved sound or sequence fields")
                else:
                    check(after[72] == 6 and after[2] == 0 and not after[3] & 3,
                          "SEQ short release navigated incorrectly or started playback/REC")
                    check(case[2] == baseline[2], "SEQ short tap changed persistent fields")
            else:
                check(after[72] == page and after[2:30] == initial[2:30],
                      f"{name} hold menu changed enable, transport or musical fields")
                check(case[:3] == baseline[:3], f"{name} long release changed PCM, MIDI or SAVE")
                frames = sorted(case[3].parent.glob(case[3].name + "-*.ppm"))
                with Image.open(frames[3]) as image:  # 1500 ms, before SAVE returns HOME.
                    image.resize((480, 480), Image.Resampling.NEAREST).save(case[3].with_suffix(".png"))

        # A poll while still held must navigate once; its later release must
        # not turn into a short tap. A following short tap remains usable.
        held = run(f"{name}-held-open-once", quiet + [f"100 hold {button} 1", sysex(850, 0x02, 2),
                   sysex(1200, 0x02, 3), f"1400 hold {button} 0", sysex(1500, 0x02, 4)])
        reply = replies(held)
        check(reply[2][72] == reply[3][72] == reply[4][72] == page,
              f"{name} held menu did not open once and stay visible")
        check(held[:3] == baseline[:3], f"{name} post-hold release triggered a musical short action")

    # Toggle LFO or ARP without navigating away from the sequencer. This is
    # the essential distinction between activation and an edit-menu hold.
    for button, name in ((5, "lfo"), (10, "arp")):
        case = run(f"{name}-tap-retains-seq", ["10 btn 8", "50 btn 11", f"100 hold {button} 1",
                   "150 keys 0x4480", sysex(250, 0x02, 1), f"300 hold {button} 0",
                   sysex(350, 0x02, 2), f"500 btn {button}", "600 keys 0", "11000 btn 9"], end=12000)
        reply = replies(case)
        check(reply[1][72] == reply[2][72] == 6, f"{name} short tap forced its edit page")
        check(reply[1][2] == 0 and (reply[2][2] == 1 if button == 10 else reply[2][2] == 0),
              f"{name} short tap changed an unrelated transport mode")
        check(case[2] is not None and case[2][20] == baseline[2][20],
              f"Two {name} taps did not return LFO to its original enable setting")

    # Independent gesture instances must not share pending/release state.
    simultaneous = run("independent-held-buttons", quiet + ["100 hold 5 1", "120 hold 10 1",
                       "850 hold 5 0", "900 hold 10 0", sysex(950, 0x02, 2)])
    check(replies(simultaneous)[2][72] == 5 and simultaneous[:3] == baseline[:3],
          "Overlapping LFO and ARP holds shared gesture state or toggled playback/effects")

    # Menu navigation remains neutral while live notes, captured playback or
    # an arpeggiator are sounding. Held keys keep their original MIDI owner.
    for mode, start in (("live", []), ("seq", ["400 btn 12"]), ("arp", ["400 btn 10"])):
        common = ["10 btn 8", "300 keys 0x4480", "2400 keys 0", "11000 btn 9"] + start
        plain = run(f"{mode}-navigation-reference", common, end=12000)
        for button, name in ((5, "lfo"), (10, "arp"), (11, "seq")):
            case = run(f"{mode}-{name}-menu-neutral", common + [f"600 hold {button} 1",
                       f"1500 hold {button} 0", "1800 btn 8"], end=12000)
            check(case[:3] == plain[:3], f"Opening {name} edit during {mode} changed PCM, notes or saved state")

    # ENV already has useful direct navigation and a distinct HOLD parameter;
    # pressing it must not create a new implicit pedal or toggle the engine.
    env = run("env-direct-navigation", quiet + ["100 btn 4", sysex(300, 0x02, 2), "500 btn 4"])
    check(replies(env)[2][72] == 3 and env[:3] == baseline[:3],
          "ENV direct navigation changed sustain, audio or persistent settings")

    # An empty sequencer can still have an active click outside tk_quiet()'s
    # ordinary voices. Actual SAVE must wait until audio acknowledges STOP's
    # cancellation, rather than immediately treating the empty song as quiet.
    click_save = run("active-click-defers-save", ["10 btn 8"] +
                     [sysex(30 + track * 25, 0x23, 10 + track, (track, 0)) for track in range(4)] +
                     [sysex(150, 0x2B, 14, (1, 127, 4, 4)), "200 btn 12", "210 btn 9",
                      sysex(300, 0x02, 15)], end=1000)
    writes = re.findall(r"(\d+) ms\s+settings saved \((\d+) B\)", click_save[3].with_suffix(".log").read_text())
    check(len(writes) == 1 and int(writes[0][0]) > 210 and int(writes[0][1]) == 3700,
          "SAVE wrote an empty sequence before an active metronome cancellation reached audio")
    check(peak(click_save[0], 200, 210) > 0 and peak(click_save[0], 300, 900) == 0 and not click_save[1],
          "Active-click SAVE fixture lacked audible click, generated MIDI, or stayed noisy after STOP")
    click_state = replies(click_save)[15]
    check(not click_state[3] & 9 and click_save[2] is not None and click_save[2][3697:3701] == bytes((1, 127, 4, 4)),
          "Deferred SAVE failed to clear dirty/playback flags or preserve the chosen metronome settings")
    print(f"PASS actual-main edit gestures: {checks} threshold, enable/transport, page, overlapping-button, PCM/MIDI and SAVE checks; host only")


if __name__ == "__main__":
    main()
