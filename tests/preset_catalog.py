# SPDX-License-Identifier: GPL-3.0-only
"""Free-firmware test names/types from its own authored source catalog."""
import json
from pathlib import Path
ROWS=json.loads((Path(__file__).resolve().parents[1]/'assets/presets/terkinstein-presets.json').read_text())['presets']
assert len(ROWS)==64 and [r['id'] for r in ROWS]==list(range(64))
PRESET_NAMES=tuple(r['name'] for r in ROWS)
PRESET_INSTRUMENTS=tuple(r['instrument'] for r in ROWS)
KIT=2
