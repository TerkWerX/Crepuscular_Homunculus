"""Regenerate TERKINSTEIN's original fixed-point FM tables offline."""
import math
from pathlib import Path

tables = [
    ("int16_t", "tk_fm_sine_quarter", [round(32760 * math.sin(math.pi * i / 512)) for i in range(257)]),
    ("uint32_t", "tk_fm_envelope_steps", [math.ceil(8388608 * 32 / (44 + i * i * 32)) for i in range(128)]),
    ("uint32_t", "tk_fm_pitch_factors", [round(16777216 * 2 ** ((i - 24) / 12)) for i in range(49)]),
]
source = "/* SPDX-License-Identifier: GPL-3.0-only */\n/* Offline generated fixed-point FM tables; see tools/gen_fm_tables.py. */\n#ifndef TERKINSTEIN_FM_TABLES_H\n#define TERKINSTEIN_FM_TABLES_H\n"
for kind, name, values in tables:
    source += f"static const {kind} {name}[{len(values)}] = {{\n"
    for offset in range(0, len(values), 8):
        source += "    " + ",".join(str(value) + ("u" if kind == "uint32_t" else "") for value in values[offset:offset + 8]) + ",\n"
    source += "};\n"
source += "#endif\n"
(Path(__file__).resolve().parents[1] / "firmware/src/terkinstein_fm_tables.h").write_text(source)
