#!/usr/bin/env python3
"""Actual app integration through its real USB SysEx collector and host backend."""
from pathlib import Path
import re
import subprocess
import sys


def frame(command, request_id, payload=()):
    body = bytes((0x7D, 0x54, 0x4B, 1, 0, command, request_id, len(payload), *payload))
    return bytes((0xF0,)) + body + bytes((-sum(body) & 127, 0xF7))


def sysex(time, command, request_id, payload=()):
    return f"{time} sysex {frame(command, request_id, payload).hex().upper()}"


def decode(wire):
    assert wire[0] == 0xF0 and wire[-1] == 0xF7
    data = wire[1:-1]
    assert data[:5] == bytes((0x7D, 0x54, 0x4B, 1, 1))
    assert all(x < 128 for x in data) and sum(data) % 128 == 0
    assert len(data) == data[7] + 9
    return data[5], data[6], data[8:-1]


def run(exe, folder, name, lines, end=3000):
    script = folder / f"{name}.txt"
    out = folder / name
    script.write_text("\n".join(lines + [f"{end} end"]) + "\n", encoding="ascii")
    result = subprocess.run([str(exe), str(out), str(script)], text=True, capture_output=True, check=True)
    responses = {}
    for match in re.finditer(r"(\d+) ms\s+REMOTE out ([0-9A-F]+)", result.stdout):
        command, request_id, payload = decode(bytes.fromhex(match[2]))
        responses.setdefault(request_id, []).append((int(match[1]), command, payload))
    return out, result.stdout, responses


def answer(responses, request_id, status=0):
    assert request_id in responses, f"No response for ID {request_id}"
    payload = responses[request_id][-1][2]
    assert payload[0] == status, (request_id, payload[0], status)
    return payload


def main():
    exe = Path(sys.argv[1]).resolve()
    folder = Path(sys.argv[2]).resolve()
    folder.mkdir(parents=True, exist_ok=True)

    # Querying the instrument must not change PCM, USB note events or screens.
    baseline = (Path(__file__).parent / "scripts" / "home-navigation.txt").read_text().splitlines()
    baseline = [line for line in baseline if line and not line.startswith("#") and " end" not in line]
    augmented = baseline + [sysex(200, 1, 1), sysex(400, 2, 2), sysex(1200, 2, 3), sysex(2200, 3, 4, (1, 0)), sysex(6200, 2, 5)]
    augmented.sort(key=lambda line: int(line.split()[0]))
    original, original_log, _ = run(exe, folder, "query-baseline", baseline, 8500)
    queried, queried_log, replies = run(exe, folder, "query-navigation", augmented, 8500)
    assert original.with_suffix(".wav").read_bytes() == queried.with_suffix(".wav").read_bytes()
    note_line = re.compile(r"^.*MIDI out.*$", re.MULTILINE)
    assert note_line.findall(original_log) == note_line.findall(queried_log)
    for time in (1000, 4200, 8200):
        assert Path(f"{original}-{time}.ppm").read_bytes() == Path(f"{queried}-{time}.ppm").read_bytes()
    assert answer(replies, 1)[1] == 1
    before = answer(replies, 3)
    after = answer(replies, 5)
    assert len(before) == len(after) == 79
    assert before[30:32] != after[30:32], "Sound edit did not advance full settings revision"
    assert before[77:79] == after[77:79], "Sound edit invalidated unrelated pattern notes"
    _, _, load = run(exe, folder, "host-load", [sysex(50, 5, 1)], 500)
    assert answer(load, 1) == bytes(14), "Host timing was presented as hardware qualification"

    # Explicit transport plus cached START retry while the running step advances.
    _, log, replies = run(exe, folder, "explicit-transport", [
        sysex(50, 2, 1), sysex(100, 0x11, 2, (2,)), sysex(200, 2, 3),
        sysex(400, 0x15, 4, (1,)), sysex(500, 0x15, 4, (1,)), sysex(620, 2, 5),
        sysex(800, 0x15, 6, (0,)), sysex(900, 2, 7),
        sysex(1000, 0x15, 8, (2,)), sysex(1100, 2, 9), sysex(1300, 0x15, 10, (0,)),
    ], 2000)
    assert answer(replies, 1)[2] == 0 and answer(replies, 1)[3] & 1 == 0
    assert answer(replies, 3)[4] == 2 and answer(replies, 3)[3] & 1 == 0
    assert len(replies[4]) == 2 and replies[4][0][2] == replies[4][1][2]
    playing = answer(replies, 5)
    assert playing[2] == 2 and playing[3] & 1 and playing[5] >= 1
    assert not answer(replies, 7)[3] & 1 and answer(replies, 9)[3] & 1
    for match in re.finditer(r"(\d+) ms\s+MIDI out", log):
        assert int(match[1]) >= 400, "Navigation/query started notes"

    # Full 64-step chord/gate editing, REC guards, captured FM and MONO state.
    long_step = (2, 63, 2, 36, 100, 127, 1, 60, 90, 8, 0)
    invalid_step = (2, 63, 2, 36, 100, 127, 1, 20, 90, 8, 0)
    _, _, replies = run(exe, folder, "pattern-and-fm", [
        sysex(50, 0x12, 1, (2, 2)), sysex(100, 0x10, 2, long_step),
        sysex(150, 3, 3, (2, 63)), sysex(200, 2, 4),
        sysex(250, 0x10, 5, invalid_step), sysex(300, 3, 6, (2, 63)),
        sysex(350, 0x12, 7, (2, 0)), sysex(400, 3, 8, (2, 63)),
        sysex(450, 0x1A, 9, (1, 1)), sysex(500, 0x10, 10, (1, 0, 0)),
        sysex(550, 0x12, 11, (2, 1)), sysex(600, 0x1A, 12, (1, 0)),
        sysex(650, 0x15, 13, (0,)), sysex(700, 0x16, 14, (32,)),
        sysex(750, 0x1B, 15, (0, 0, 31)), sysex(800, 0x1C, 16, (2, 48)),
        sysex(850, 4, 17, (0,)), sysex(900, 0x18, 18, (2,)),
        sysex(950, 0x16, 19, (33,)), sysex(1000, 4, 20, (3,)),
        sysex(1050, 0x1D, 21, (1,)), sysex(1100, 2, 22),
        sysex(1150, 0x1B, 23, (0, 0, 32)), sysex(1200, 4, 24, (3,)),
        sysex(1250, 0x16, 25, (0,)), sysex(1300, 0x1B, 26, (0, 1, 100)),
    ], 2000)
    for request_id in (2, 3, 6, 8):
        step = answer(replies, request_id)
        assert step[1:4] == bytes((2, 63, 2)) and step[4:] == bytes(long_step[3:])
    answer(replies, 5, 2)
    state = answer(replies, 4)
    assert state[13] == 2 and state[61] == 1, "64-step occupancy wrong"
    answer(replies, 10, 3)
    answer(replies, 11, 3)
    live = answer(replies, 17)
    captured = answer(replies, 20)
    assert len(live) == 42 and live[4] == 48 and live[6] == 31
    assert captured[1] == 3 and captured[2:] == live[2:], "FM capture lost edited operators"
    assert answer(replies, 22)[76] == 1
    answer(replies, 23, 2)
    assert answer(replies, 24)[2:] == captured[2:]
    answer(replies, 26, 2)

    # A reply routed back into USB and a bad checksum cannot act as commands.
    malformed = bytearray(frame(0x15, 2, (1,)))
    malformed[-2] ^= 1
    reply = bytearray(frame(0x15, 3, (1,)))
    reply[5] = 1
    reply[-2] = -sum(reply[1:-2]) & 127
    _, log, replies = run(exe, folder, "invalid-and-reset", [
        f"50 sysex {malformed.hex()}", f"100 sysex {reply.hex()}", sysex(150, 2, 4),
        sysex(200, 0x15, 5, (1,)), sysex(250, 0x15, 5, (0,)), sysex(300, 2, 6),
        "350 usbreset 0", sysex(400, 0x15, 5, (0,)), sysex(450, 2, 7),
    ], 1000)
    answer(replies, 2, 1)
    assert 3 not in replies and not answer(replies, 4)[3] & 1
    assert replies[5][1][2][0] == 5 and answer(replies, 6)[3] & 1
    assert not answer(replies, 7)[3] & 1
    for match in re.finditer(r"(\d+) ms\s+MIDI out", log):
        assert int(match[1]) >= 200
    print("PASS remote app: query PCM/MIDI/screens; explicit transport/retries; atomic 64-step/gate edits; REC guards; FM capture/MONO; invalid/reset isolation")


if __name__ == "__main__":
    main()
