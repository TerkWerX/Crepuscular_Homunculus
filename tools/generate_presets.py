#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Validate the curated catalog and emit the FM-1 firmware preset bank.

Run from any directory. --check verifies outputs without changing files.
Only Python's standard library is required, including in packaged sources.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CATALOG = ROOT / 'assets/presets/terkinstein-presets.json'
FIELDS = ('id', 'name', 'instrument', 'macros', 'memory', 'fx', 'attack',
          'release', 'lfo', 'fm_index', 'fm_patch')
LEGACY_HASH = '89a987a8b8cbe8f4b9e7b31f2f5ae255afdf28f2aa5ac80c3a30241c2db44cae'


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def integer(value: object, upper: int, label: str) -> None:
    require(type(value) is int and 0 <= value <= upper, f'{label}: expected 0..{upper}')


def validate(data: dict) -> list[dict]:
    require(data.get('version') == 1, 'unsupported catalog schema')
    require(data.get('legacy_count') == 36, 'legacy bank must keep its 36 slots')
    require(data.get('default_drum_preset') == 30, 'original drum default must remain slot 31')
    require(data.get('bank_version') == '0.26.18-dev', 'unexpected bank version')
    rows = data['presets']
    require(len(rows) == 66, '64 factory choices plus two retained legacy definitions required')
    names: set[str] = set()
    settings: set[str] = set()
    fm_id = 0
    for i, row in enumerate(rows):
        require(all(k in row for k in FIELDS), f'slot {i+1}: missing sound field')
        require(row['id'] == i, f'slot {i+1}: IDs must be contiguous and stable')
        name = row['name']
        require(isinstance(name, str) and 1 <= len(name) <= 14
                and all(c in 'ABCDEFGHIJKLMNOPQRSTUVWXYZ 0123456789' for c in name),
                f'slot {i+1}: invalid display name')
        require(name not in names or (i in (64,65) and name == rows[(24,51)[i-64]]['name']), f'duplicate name: {name}')
        names.add(name)
        integer(row['instrument'], 3, f'{name} engine')
        require(len(row['macros']) == 4, f'{name}: four macros required')
        for j, v in enumerate(row['macros']):
            integer(v, 127, f'{name} macro {j+1}')
        for field in ('memory', 'attack', 'release'):
            integer(row[field], 127, f'{name} {field}')
        integer(row['fx'], 1, f'{name} FX')
        require(len(row['lfo']) == 4, f'{name}: four LFO fields required')
        for value, upper, label in zip(row['lfo'], (1, 2, 127, 127), ('enabled', 'shape', 'rate', 'depth')):
            integer(value, upper, f'{name} LFO {label}')
        require(row['role'] in ('legacy', 'bass', 'lead', 'keys', 'kit', 'atmosphere'), f'{name}: missing musical role')
        require(isinstance(row['description'], str) and row['description'], f'{name}: missing curation note')
        if row['instrument'] == 3:
            require(row['fm_index'] == fm_id, f'{name}: explicit FM patch IDs must be contiguous')
            fm_id += 1
            p = row['fm_patch']
            require(isinstance(p, dict) and len(p.get('operators', [])) == 6, f'{name}: six operators required')
            for j, op in enumerate(p['operators']):
                require(len(op) == 6, f'{name} OP{j+1}: six fields required')
                for k, value in enumerate(op):
                    integer(value, 31 if k == 0 else 127, f'{name} OP{j+1} field{k}')
            for field, upper in (('algorithm', 7), ('feedback', 127), ('pitch_depth', 48), ('pitch_time', 127)):
                integer(p[field], upper, f'{name} {field}')
        else:
            require(row['fm_index'] is None and row['fm_patch'] is None, f'{name}: non-FM preset has an FM patch')
        signature = json.dumps({k: row[k] for k in FIELDS if k not in ('id', 'name', 'fm_index')}, sort_keys=True)
        require(signature not in settings, f'{name}: duplicated complete sound settings')
        settings.add(signature)
    legacy = json.dumps([{k: row[k] for k in FIELDS} for row in rows[:36]], sort_keys=True, separators=(',', ':')).encode()
    require(hashlib.sha256(legacy).hexdigest() == LEGACY_HASH, 'legacy names, indices or sound settings changed')
    require(fm_id == 14, 'expected unchanged thirteen FM patches plus approved WICKED v2')
    prefix = json.dumps(rows[:64], sort_keys=True, separators=(',', ':')).encode()
    require(hashlib.sha256(prefix).hexdigest() == 'd2b49ac961ff1b040579df95e75b3ab8063ff68515ec5e1534087ab7d060b8ef', 'original64 definitions must remain byte-equivalent')
    require(data.get('catalog_revision') == 2, 'catalog revision required')
    require(data.get('factory_order') == [64 if i == 24 else 65 if i == 51 else i for i in range(64)], 'explicit stable factory selection map required')
    return rows


def c_output(rows: list[dict], digest: str) -> str:
    lines = ['/* SPDX-License-Identifier: GPL-3.0-only */',
             '/* Generated by tools/generate_presets.py; edit the JSON catalog. */',
             f'/* Catalog SHA-256: {digest} */',
             '#ifndef TERKINSTEIN_PRESETS_H', '#define TERKINSTEIN_PRESETS_H',
             'static const tk_preset_t PRESETS[] = {']
    engines = ('TK_INSTRUMENT_PAD', 'TK_INSTRUMENT_METAL', 'TK_INSTRUMENT_DRUM_KIT', 'TK_INSTRUMENT_FM')
    shapes = ('TK_LFO_TRIANGLE', 'TK_LFO_PULSE', 'TK_LFO_RAMP')
    for r in rows:
        on, shape, rate, depth = r['lfo']
        fields = ', '.join(str(r[k]) for k in ('memory', 'fx', 'attack', 'release'))
        macros = ', '.join(map(str, r['macros']))
        lines.append(f'    {{"{r["name"]}", {{{macros}}}, {fields}, {on}, {shapes[shape]}, {rate}, {depth}, {engines[r["instrument"]]}}},')
    lines += ['};', '/* 255 denotes a non-FM instrument; IDs 0..3 retain tk_fm_factory. */',
              'static const uint8_t TK_PRESET_FM_PATCH[66] = {',
              '    ' + ', '.join(str(r['fm_index'] if r['fm_index'] is not None else 255) for r in rows),
              '};', '/* Independently authored patches, IDs 4..13. */',
              'static const tk_fm_patch_t TK_NEW_FM_FACTORY[] = {']
    for r in rows:
        if r['fm_index'] is None or r['fm_index'] < 4:
            continue
        p = r['fm_patch']
        operators = ', '.join('{' + ', '.join(map(str, op)) + '}' for op in p['operators'])
        controls = ', '.join(str(p[k]) for k in ('algorithm', 'feedback', 'pitch_depth', 'pitch_time'))
        lines += [f'    /* {r["name"]} */', '    {{' + operators + '}, ' + controls + '},']
    lines += ['};', '#define TK_FACTORY_COUNT 64u', '#define TK_CATALOG_REVISION 2u',
              'static const uint8_t TK_FACTORY_SOUND[64] = {',
              '    ' + ', '.join(str(64 if i == 24 else 65 if i == 51 else i) for i in range(64)),
              '};', '#endif', '']
    return '\n'.join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true', help='fail if generated outputs are stale')
    args = parser.parse_args()
    raw = CATALOG.read_bytes()
    data = json.loads(raw)
    rows = validate(data)
    digest = hashlib.sha256(raw).hexdigest()
    outputs = {ROOT / 'firmware/gen/terkinstein_presets.h': c_output(rows, digest)}
    stale = []
    for path, contents in outputs.items():
        if args.check:
            if not path.exists() or path.read_bytes() != contents.encode('utf-8'):
                stale.append(str(path.relative_to(ROOT)))
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(contents.encode('utf-8'))
    if stale:
        parser.exit(1, 'Stale generated preset files: ' + ', '.join(stale) + '\nRun tools/generate_presets.py.\n')
    counts = [sum(r['instrument'] == e for r in rows) for e in range(4)]
    print(f'Curated 64-choice / 66-definition bank {"verified" if args.check else "generated"}: '
          f'{counts[0]} PAD / {counts[1]} METAL / {counts[2]} KIT / {counts[3]} FM; original 64 locked; only two approved revisions selectable in their factory positions.')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
