#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual-main splash visibility telemetry without screen/audio side effects."""
from __future__ import annotations

import argparse
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
    panel = Panel(args.executable.resolve(), folder / "ui", folder)
    checks = 0

    def check(condition, message):
        nonlocal checks
        checks += 1
        assert condition, message

    def run(label, events, *, end=1800):
        settings = folder / f"{label}.bin"
        if settings.exists():
            settings.unlink()
        return panel.run(label, ordered(*[line + "\n" for line in events], f"{end} end\n"))

    def responses(result):
        answers = {}
        for match in re.finditer(r"(\d+) ms\s+REMOTE out ([0-9A-F]+)", result[3].with_suffix(".log").read_text()):
            command, rid, payload = decode(bytes.fromhex(match[2]))
            check(payload and payload[0] == 0, f"UI query {command}/{rid} rejected")
            answers.setdefault(rid, []).append((int(match[1]), command, payload))
        return answers

    query = run("boot-visibility", [sysex(50, 0x01, 1), sysex(70, 0x0C, 2),
        "100 enc 2 1", sysex(140, 0x0C, 3), "200 midi 0x90 60 100",
        sysex(250, 0x0C, 4), "600 midi 0x80 60 0", "700 btn 8",
        sysex(780, 0x0C, 5), sysex(900, 0x02, 6), "1400 btn 9"])
    answers = responses(query)
    check(answers[1][-1][2][3] & 16, "HELLO did not advertise splash/page UI state")
    check(answers[2][-1][2] == bytes((0, 1, 1, 0)), "Boot splash was not reported visible")
    check(answers[3][-1][2] == bytes((0, 1, 1, 0)), "Encoder turn incorrectly dismissed startup artwork")
    check(answers[4][-1][2] == bytes((0, 1, 1, 0)), "Incoming MIDI incorrectly dismissed startup artwork")
    check(answers[5][-1][2] == bytes((0, 1, 0, 0)), "Physical HOME did not dismiss splash and report its page")
    check(len(answers[6][-1][2]) == 79 and answers[6][-1][2][72] == 0,
          "New UI query changed the established GET_STATE prefix/length")

    key = run("key-dismissal", [sysex(50, 0x0C, 1), "100 keys 0x80",
              sysex(150, 0x0C, 2), "400 keys 0"])
    key_answers = responses(key)
    check(key_answers[1][-1][2][2] == 1 and key_answers[2][-1][2] == bytes((0, 1, 0, 0)),
          "A physical key did not dismiss startup art")
    check(len(key[1]) == 2 and key[1][0] == (100, 0x90, 48, 100)
          and key[1][1] == (400, 0x80, 48, 0), "Dismissal swallowed or changed the playable first key")

    env = run("button-page", [sysex(50, 0x0C, 1), "100 btn 4", sysex(170, 0x0C, 2)])
    check(responses(env)[2][-1][2] == bytes((0, 1, 0, 3)),
          "Dismissal swallowed ENV navigation instead of reporting the envelope page")
    arp = run("button-transport", ["100 btn 10", sysex(170, 0x0C, 1), sysex(200, 0x02, 2)])
    arp_answers = responses(arp)
    check(arp_answers[1][-1][2] == bytes((0, 1, 0, 0))
          and arp_answers[2][-1][2][2] == 1 and arp_answers[2][-1][2][3] & 1,
          "Dismissal swallowed the first ARP button's normal transport action")

    replay = run("cached-ui-replay", [sysex(50, 0x0C, 1), "100 btn 8",
                 sysex(150, 0x0C, 1), sysex(200, 0x0C, 2)])
    replay_answers = responses(replay)
    check(len(replay_answers[1]) == 2 and replay_answers[1][0][2] == replay_answers[1][1][2],
          "Repeated request id did not return its safe immutable cached reply")
    check(replay_answers[2][-1][2] == bytes((0, 1, 0, 0)), "Fresh UI request did not observe subsequent dismissal")

    # Querying while sound is active must remain observational. The same
    # event stream with/without probes produces identical PCM, MIDI and SAVE.
    common = ["100 enc 2 1", "200 midi 0x90 60 100", "400 keys 0x80",
              "700 btn 4", "900 keys 0", "1000 midi 0x80 60 0", "6000 btn 9"]
    plain = run("observational-plain", common, end=6500)
    queried = run("observational-probed", common + [sysex(50, 0x0C, 1),
        sysex(150, 0x0C, 2), sysex(250, 0x0C, 3), sysex(450, 0x0C, 4),
        sysex(750, 0x0C, 5), sysex(1100, 0x0C, 6)], end=6500)
    check(queried[0] == plain[0], "UI observation changed rendered PCM")
    check(queried[1] == plain[1], "UI observation changed note ownership/timestamps")
    check(queried[2] == plain[2], "UI observation changed stored settings")
    print(f"PASS actual-main UI state: {checks} capability, splash, physical action, replay and PCM/SAVE checks; host only")


if __name__ == "__main__":
    main()
