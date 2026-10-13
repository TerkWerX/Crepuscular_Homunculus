#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise split pad/key routing through the actual host main loop and USB collector."""
from pathlib import Path
import sys

from check_remote import answer, run, sysex


def main():
    exe, folder = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
    folder.mkdir(parents=True, exist_ok=True)
    checks = 0

    def expect(condition, why):
        nonlocal checks
        checks += 1
        assert condition, why

    # A capability probe is read-only: settings, animation, outgoing MIDI and
    # audio all remain identical while normal channel16 still plays live.
    baseline = ["10 keys 1", "20 keys 0", "100 midi 159 60 90", "700 midi 143 60 0"]
    original, original_log, _ = run(exe, folder, "default-channel16", baseline, 1500)
    queried, queried_log, replies = run(exe, folder, "default-channel16-query",
        sorted(baseline + [sysex(50, 0x0F, 1)], key=lambda line: int(line.split()[0])), 1500)
    expect(answer(replies, 1) == bytes((0, 1, 0, 16, 0)), "default capability body changed")
    expect(original.with_suffix(".wav").read_bytes() == queried.with_suffix(".wav").read_bytes(), "GET_PAD_ROUTE changed PCM")
    expect(Path(f"{original}-1000.ppm").read_bytes() == Path(f"{queried}-1000.ppm").read_bytes(), "read probe changed LCD")
    expect([line for line in original_log.splitlines() if "MIDI out" in line] ==
           [line for line in queried_log.splitlines() if "MIDI out" in line], "read probe emitted musical MIDI")

    setup = ["10 keys 1", "20 keys 0", sysex(40, 0x16, 1, (36,)),
             sysex(60, 0x17, 2, (0, 54)), sysex(80, 0x0F, 3), sysex(100, 0x2C, 4, (1, 1))]
    # Physical REC retains the selected captured drum kit and the independent
    # live melodic sound. Keyboard/panel notes do not enter the drum bank.
    _, _, replies = run(exe, folder, "physical-drum-record", setup + [
        sysex(130, 0x11, 5, (0,)), sysex(150, 0x09, 6, (0,)), "200 btn 13",
        "210 midi 144 60 99", "215 keys 2", "220 midi 159 36 45", "230 midi 159 38 118",
        "240 midi 143 36 0", "250 midi 143 38 0", "260 keys 0", "270 midi 128 60 0",
        "280 btn 13", sysex(310, 3, 7, (0, 0)), sysex(330, 2, 8), sysex(350, 0x09, 9, (0,)),
        sysex(400, 0x15, 10, (0,))], 700)
    expect(answer(replies, 4) == bytes((0, 1, 1, 16, 0)), "split route failed explicit acknowledgment")
    expect(answer(replies, 7)[3] == 2, "DRUM capture mixed keyboard/panel with pads")
    expect(answer(replies, 7)[4:] == bytes((36, 45, 0, 0, 38, 118, 0, 0)), "actual pad notes/velocity changed")
    expect(answer(replies, 8)[10] == 36 and answer(replies, 8)[15] == 54, "DRUM REC replaced live sound or chosen kit")
    expect(answer(replies, 6) == answer(replies, 9), "physical DRUM REC changed captured sound fields")

    # Remote default REC takes the same protected path. Melody capture admits
    # a normal keyboard note while ignored pad notes still audition DRUM.
    _, _, replies = run(exe, folder, "melody-record", setup + [
        sysex(200, 0x1A, 5, (1, 1)), "210 midi 144 60 103", "220 midi 159 36 81",
        "270 midi 143 36 0", "600 midi 128 60 0", sysex(650, 0x1A, 6, (1, 0)),
        sysex(700, 3, 7, (1, 0)), sysex(740, 0x15, 8, (0,))], 900)
    expect(answer(replies, 7)[3] == 1 and answer(replies, 7)[4:6] == bytes((60, 103)), "pads contaminated MELODY REC")
    expect(answer(replies, 7)[6] > 1, "keyboard held duration was not retained")

    # Pad wheels/pedals/program/controller learn cannot change melody or kit,
    # unlike accepted source-local volume/expression and strike pressure.
    sounding = setup + ["200 midi 144 60 91", "200 midi 159 36 95", "950 midi 128 60 0", "950 midi 143 36 0"]
    original, _, _ = run(exe, folder, "isolated-sound", sounding, 1600)
    ignored = ["150 midi 191 1 127", "155 midi 191 64 127", "160 midi 191 20 127",
               "165 midi 191 101 0", "170 midi 191 100 0", "175 midi 191 6 24",
               "180 midi 239 127 127", "185 midi 207 63 0"]
    filtered, _, _ = run(exe, folder, "isolated-controls",
        sorted(sounding + ignored, key=lambda line: int(line.split()[0])), 1600)
    expect(original.with_suffix(".wav").read_bytes() == filtered.with_suffix(".wav").read_bytes(),
           "ignored pad controls/program/bend changed actual app PCM")

    # Disabled/expired/disconnected routes revert to ordinary channel16, and
    # require a fresh handshake. Keep cached SET retries distinct from a
    # heartbeat: only a new request execution renews the lease.
    _, _, replies = run(exe, folder, "lease-and-disconnect", [
        sysex(50, 0x2C, 1, (1, 1)), sysex(2900, 0x2C, 2, (1, 1)), sysex(4000, 0x0F, 3),
        sysex(5901, 0x0F, 4), sysex(6000, 0x2C, 5, (1, 1)), "6100 usbreset 0",
        sysex(6150, 0x0F, 6), sysex(6200, 0x2C, 7, (1, 1)), "6300 drop 0",
        sysex(6350, 0x0F, 8)], 6700)
    expect(answer(replies, 3)[2] == 1, "fresh SET did not renew live lease")
    expect(answer(replies, 4)[2] == 0, "lease did not expire after3000ms")
    expect(answer(replies, 6)[2] == 0, "USB generation reset retained stale pad assignment")
    expect(answer(replies, 8)[2] == 0, "USB queue loss retained stale pad assignment")

    # A new split cannot change an in-progress recording, while turning it
    # off remains available for cleanup. Neither operation touches SAVE.
    _, _, replies = run(exe, folder, "capture-route-guard", [
        sysex(50, 0x1A, 1, (1, 1)), sysex(100, 0x2C, 2, (1, 1)), sysex(150, 0x0F, 3),
        sysex(200, 0x2C, 4, (1, 0)), sysex(250, 0x1A, 5, (1, 0)), sysex(300, 0x15, 6, (0,))], 600)
    answer(replies, 2, 3)
    expect(answer(replies, 3)[2] == 0 and answer(replies, 4)[2] == 0, "REC guard enabled split or prevented OFF cleanup")
    print(f"PASS: {checks} actual-main pad/key routing checks (host only)")


if __name__ == "__main__":
    main()
