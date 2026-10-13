#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Read FM-1 identity, advertised capabilities and optional developer telemetry.

Only the updater identity query and editor INFO / DIAG requests are sent. DIAG
is sent only when INFO advertises its documented capability. This tool does
not enable WATCH, play notes, change settings, operate transport, capture a
backup or install firmware. Audio enumeration does not open or record audio.

Examples:
  python tools/fm1_probe.py --deps .tooling/midi --out build/hardware/probe.json
  python tools/fm1_probe.py --samples 10 --interval 2
"""
import argparse
import datetime
import json
from pathlib import Path
import queue
import re
import sys
import time

from fm1_install import HS_QUERY, parse_identity

HEADER = b"\xf0\x7d\x46\x4c"
INFO, DIAG = 1, 75
# Generic "USB MIDI", "composite" and OTA ports could be unrelated equipment.
FM1_PORT = re.compile(r"felucca|fm[ -]?1(?:\b|_)", re.I)
TAG_LENGTHS = {0x55: 3, 0x4D: 4, 0x42: 3, 0x46: 4, 0x53: 3,
               0x50: 3, 0x4E: 3, 0x52: 3, 0x44: 3, 0x4C: 3}
DIAG_NAMES = {
    1: "uptime_ms", 2: "cpu_percent", 3: "battery_adc", 4: "playing",
    5: "active_voices", 6: "voices_shed", 7: "voices_given_up",
    8: "displayed_track", 9: "bpm", 10: "flash_support",
    11: "pending_midi_overflow", 12: "usb_resets", 13: "usb_packets_rx",
    14: "usb_packets_tx", 15: "usb_stalls", 16: "sample_slots_used",
    17: "sample_slots_total", 18: "encoded_sample_bytes_used",
    19: "encoded_sample_bytes_capacity", 20: "song_slots_used",
    21: "song_slots_total", 22: "user_patches_used", 23: "user_patches_total",
    24: "active_storage_slot_plus_one", 25: "pending_song_load",
    26: "autosave_error", 27: "rendered_quiet_seconds", 28: "usb_retries",
    29: "usb_suspends", 30: "main_ram_allocated_bytes", 31: "main_ram_capacity_bytes",
    32: "audio_display_pool_allocated_bytes", 33: "audio_display_pool_capacity_bytes",
    34: "firmware_image_bytes", 35: "firmware_image_capacity_bytes",
    36: "ram_code_bytes", 37: "ram_code_capacity_bytes", 38: "mapped_flash_span_bytes",
    39: "persistent_region_reserved_bytes", 40: "peak_audio_render_us",
    41: "audio_overruns", 42: "boots", 43: "din_drops",
}


class ProbeError(Exception):
    """Unavailable device or dependency."""


class ProtocolError(ValueError):
    """Incomplete, ambiguous or invalid protocol response."""


def utc_now():
    return datetime.datetime.now(datetime.timezone.utc).isoformat()


def read_query(command):
    """Construct only allowlisted zero-argument editor queries."""
    if command not in (INFO, DIAG):
        raise ValueError("only INFO and DIAG read queries are allowed")
    return HEADER + bytes([command, 0xF7])


def editor_payload(packet, command):
    packet = bytes(packet)
    if (len(packet) < 6 or not packet.startswith(HEADER + bytes([command]))
            or packet[-1] != 0xF7):
        raise ProtocolError("wrong editor reply framing or command")
    payload = packet[5:-1]
    if any(b > 127 for b in payload):
        raise ProtocolError("editor data contains a non-7-bit byte")
    return payload


def parse_info(packet):
    """Decode mandatory INFO fields; retain every optional trailing byte.

    Unknown extension layouts cannot safely be skipped. Parsing stops there,
    so an accidental diagnostic tag inside an unknown block never enables DIAG.
    Truncated known extensions are retained but do not advertise a capability.
    """
    payload = editor_payload(packet, INFO)
    position = 0

    def string():
        nonlocal position
        end = payload.find(b"\0", position)
        if end < 0:
            raise ProtocolError("truncated INFO string")
        result = payload[position:end].decode("ascii")
        position = end + 1
        return result

    version = string()
    if len(payload) - position < 5:
        raise ProtocolError("truncated INFO counts")
    nengine, pcount, gcount, nstep, pe0 = payload[position:position + 5]
    position += 5
    engines = [string() for _ in range(nengine)]
    trailing = payload[position:]
    capabilities = []
    extension_tail = b""
    cursor = min(2, len(trailing))  # v3 NTRK, v6 CHAIN_ROWS precede tagged blocks.
    while cursor < len(trailing):
        size = TAG_LENGTHS.get(trailing[cursor])
        if size is None or len(trailing) - cursor < size or trailing[cursor + 1] != 1:
            extension_tail = trailing[cursor:]
            break
        block = trailing[cursor:cursor + size]
        capabilities.append({"tag": block[0], "version": block[1],
                             "data": list(block[2:]), "raw_hex": block.hex()})
        cursor += size
    diag_blocks = [c for c in capabilities if c["tag"] == 0x44]
    # Duplicated advertisements are ambiguous, even if one is supported.
    supports_diag = len(diag_blocks) == 1 and diag_blocks[0]["data"] == [1]
    return {"version": version, "nengines": nengine, "pcount": pcount,
            "gcount": gcount, "nstep": nstep, "engine_parameter_start": pe0,
            "engine_names": engines, "ntracks": trailing[0] if trailing else None,
            "chain_rows": trailing[1] if len(trailing) > 1 else None,
            "capabilities": capabilities, "diagnostics_supported": supports_diag,
            "trailing_bytes": list(trailing), "trailing_hex": trailing.hex(),
            "unparsed_extension_hex": extension_tail.hex(), "raw_hex": bytes(packet).hex()}


def parse_u32(data):
    """Five little-endian 7-bit digits, bounded to an unsigned 32-bit integer."""
    if len(data) != 5 or any(b > 127 for b in data) or data[4] > 15:
        raise ProtocolError("invalid diagnostic u32")
    return sum(b << (7 * i) for i, b in enumerate(data))


def parse_diag(packet):
    payload = editor_payload(packet, DIAG)
    if len(payload) < 3:
        raise ProtocolError("truncated diagnostic header")
    schema, rc, count = payload[:3]
    if schema != 1:
        raise ProtocolError(f"unsupported diagnostic schema {schema}")
    if len(payload) != 3 + 6 * count:
        raise ProtocolError("diagnostic count does not match reply length")
    if rc and count:
        raise ProtocolError("diagnostic error reply contains values")
    values, fields = {}, []
    for offset in range(3, len(payload), 6):
        field_id = payload[offset]
        if field_id in values:
            raise ProtocolError(f"duplicate diagnostic field {field_id}")
        value = parse_u32(payload[offset + 1:offset + 6])
        values[field_id] = value
        fields.append({"id": field_id, "name": DIAG_NAMES.get(field_id), "value": value})
    return {"schema": schema, "rc": rc, "fields": fields,
            "named": {name: values.get(field_id) for field_id, name in DIAG_NAMES.items()},
            "unavailable_ids": [field_id for field_id in DIAG_NAMES if field_id not in values],
            "raw_hex": bytes(packet).hex()}


class ReadOnlyLink:
    """A context-managed MIDI pair whose send path enforces the query allowlist."""

    def __init__(self, mido, input_name, output_name):
        self.mido, self.input_name, self.output_name = mido, input_name, output_name
        self.queue = queue.Queue()
        self.inp = self.out = None
        self._diag_supported = False

    def __enter__(self):
        try:
            self.inp = self.mido.open_input(self.input_name, callback=self._receive)
            self.out = self.mido.open_output(self.output_name, autoreset=False)
            time.sleep(0.3)
            return self
        except BaseException:
            self.close()
            raise

    def _receive(self, message):
        if message.type == "sysex":
            self.queue.put(bytes([0xF0, *message.data, 0xF7]))

    def send_query(self, packet):
        if packet not in (HS_QUERY, read_query(INFO), read_query(DIAG)):
            raise ValueError("query is not in the read-only allowlist")
        if packet == read_query(DIAG) and not self._diag_supported:
            raise ValueError("DIAG requires a valid INFO advertisement first")
        self.out.send(self.mido.Message("sysex", data=packet[1:-1]))

    def request(self, packet, accept, timeout):
        while True:
            try:
                self.queue.get_nowait()
            except queue.Empty:
                break
        self.send_query(packet)
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            try:
                reply = self.queue.get(timeout=max(0, end - time.monotonic()))
            except queue.Empty:
                return None
            if accept(reply):
                if packet == read_query(INFO):
                    try:
                        self._diag_supported = parse_info(reply)["diagnostics_supported"]
                    except ProtocolError:
                        self._diag_supported = False
                return reply
        return None

    def close(self):
        for port in (self.out, self.inp):
            if port is not None:
                try:
                    port.close()
                except Exception:
                    pass
        self.out = self.inp = None

    def __exit__(self, *_):
        self.close()


def select_ports(inputs, outputs, requested=None):
    """Select a recognizable, unambiguous pair without probing other MIDI gear."""
    eligible = [n for n in inputs if FM1_PORT.search(n)
                and (requested is None or requested.casefold() in n.casefold())]
    if not eligible:
        raise ProbeError("no FM-1/Felucca MIDI input matches; check the USB data cable")
    if len(eligible) != 1:
        raise ProbeError("multiple FM-1/Felucca MIDI inputs; select one with --port")
    name = eligible[0]
    if name in outputs:
        return name, name
    base = re.sub(r"\s+\d+$", "", name).casefold()
    matches = [n for n in outputs if FM1_PORT.search(n)
               and re.sub(r"\s+\d+$", "", n).casefold() == base]
    if len(matches) != 1:
        raise ProbeError("no unambiguous FM-1/Felucca MIDI output matches the input")
    return name, matches[0]


def enumerate_audio():
    """Inspect Felucca input formats without opening an audio stream."""
    try:
        import sounddevice as sd
    except ImportError:
        return {"available": False, "reason": "sounddevice is not installed", "inputs": []}
    try:
        devices, hosts = sd.query_devices(), sd.query_hostapis()
        result = []
        for index, device in enumerate(devices):
            if "felucca" not in device["name"].casefold() or not device["max_input_channels"]:
                continue
            formats = []
            for rate in (44100, 48000):
                supported, reason = True, None
                try:
                    sd.check_input_settings(device=index, channels=2, dtype="int16", samplerate=rate)
                except Exception as error:
                    supported, reason = False, str(error)
                formats.append({"channels": 2, "dtype": "int16", "samplerate": rate,
                                "supported": supported, "reason": reason})
            result.append({"index": index, "name": device["name"],
                           "hostapi": hosts[device["hostapi"]]["name"],
                           "max_input_channels": device["max_input_channels"],
                           "default_samplerate": device["default_samplerate"],
                           "formats": formats})
        return {"available": bool(result), "inputs": result,
                "reason": None if result else "no Felucca audio input is exposed"}
    except Exception as error:
        return {"available": False, "reason": str(error), "inputs": []}


def probe(mido, port=None, samples=1, interval=2.0, timeout=1.5):
    inputs = list(dict.fromkeys(mido.get_input_names()))
    outputs = list(dict.fromkeys(mido.get_output_names()))
    input_name, output_name = select_ports(inputs, outputs, port)
    report = {"schema": 1, "timestamp_utc": utc_now(), "read_only": True,
              "midi": {"input_names": inputs, "output_names": outputs,
                       "selected_input": input_name, "selected_output": output_name},
              "identity": None, "info": None, "diagnostics": [],
              "diagnostics_available": False, "diagnostics_reason": "INFO unavailable"}
    with ReadOnlyLink(mido, input_name, output_name) as link:
        packet = link.request(HS_QUERY, lambda p: parse_identity(p) is not None, timeout)
        if packet is None:
            raise ProbeError("FM-1 identity query timed out; the MIDI port may be in use")
        identity = parse_identity(packet)
        report["identity"] = {"text": identity.text, "model": identity.model,
                              "version": identity.version, "loader": identity.loader}
        if identity.model.casefold() != "fm-1" or identity.loader:
            raise ProbeError("device is not a running FM-1; editor queries were not sent")
        packet = link.request(read_query(INFO), lambda p: p.startswith(HEADER + bytes([INFO])), timeout)
        if packet is None:
            report["info_reason"] = "INFO request timed out"
        else:
            try:
                report["info"] = parse_info(packet)
            except ProtocolError as error:
                report["info_reason"] = str(error)
                report["info_raw_hex"] = packet.hex()
        if report["info"] is not None and report["info"]["diagnostics_supported"]:
            report["diagnostics_available"] = True
            report["diagnostics_reason"] = None
            for i in range(samples):
                if i:
                    time.sleep(interval)
                sample = {"timestamp_utc": utc_now(), "available": False}
                packet = link.request(read_query(DIAG), lambda p: p.startswith(HEADER + bytes([DIAG])), timeout)
                if packet is None:
                    sample["reason"] = "DIAG request timed out"
                else:
                    try:
                        diagnostic = parse_diag(packet)
                        sample.update(diagnostic)
                        sample["available"] = diagnostic["rc"] == 0
                        if diagnostic["rc"]:
                            sample["reason"] = f"DIAG returned error {diagnostic['rc']}"
                    except ProtocolError as error:
                        sample.update(reason=str(error), raw_hex=packet.hex())
                report["diagnostics"].append(sample)
            report["diagnostics_available"] = any(s["available"] for s in report["diagnostics"])
            if not report["diagnostics_available"]:
                report["diagnostics_reason"] = "no valid successful DIAG reply"
        elif report["info"] is not None:
            report["diagnostics_reason"] = "INFO does not advertise 44 01 01"
    report["audio"] = enumerate_audio()
    report["completed_utc"] = utc_now()
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", help="substring of a recognized FM-1/Felucca MIDI input name")
    parser.add_argument("--deps", type=Path, help="directory containing mido/python-rtmidi dependencies")
    parser.add_argument("--samples", type=int, default=1, help="DIAG samples, 1..60 (default: 1)")
    parser.add_argument("--interval", type=float, default=2, help="seconds between samples, 0.5..10 (default: 2)")
    parser.add_argument("--out", type=Path, help="save the JSON report to this file")
    args = parser.parse_args(argv)
    if not 1 <= args.samples <= 60 or not 0.5 <= args.interval <= 10:
        parser.error("--samples must be 1..60 and --interval must be 0.5..10")
    if (args.samples - 1) * args.interval > 300:
        parser.error("requested sampling interval exceeds the five-minute bound")
    if args.deps:
        sys.path.insert(0, str(args.deps.resolve()))
    try:
        try:
            import mido
            mido.set_backend("mido.backends.rtmidi")
        except ImportError as error:
            raise ProbeError("needs mido and python-rtmidi; use --deps or install them") from error
        report = probe(mido, args.port, args.samples, args.interval)
        encoded = json.dumps(report, indent=2) + "\n"
        if args.out:
            args.out.parent.mkdir(parents=True, exist_ok=True)
            args.out.write_text(encoded, encoding="utf-8")
        print(encoded, end="")
        return 0
    except KeyboardInterrupt:
        print("interrupted; MIDI ports closed", file=sys.stderr)
        return 130
    except Exception as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
