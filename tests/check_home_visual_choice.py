#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual-main display selection, saved choice and exact musical parity."""
import argparse
import json
from pathlib import Path
import struct
from PIL import Image
from check_external_midi import Panel, ordered, peak

AT = 4 + 3697  # host length header plus existing settings padding position


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('baseline', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    folder = args.output.resolve();folder.mkdir(parents=True, exist_ok=True)
    new = Panel(args.executable.resolve(), folder/'new', folder)
    old = Panel(args.baseline.resolve(), folder/'old', folder)
    checks = 0

    def check(ok, why):
        nonlocal checks
        checks += 1
        assert ok, why

    def run(panel, label, lines, store=None, fps=0):
        result = panel.run(label, ordered(lines), store=store, fps=fps)
        return result

    base = run(new, 'factory', '10 btn 8\n1000 btn 9\n1500 end\n')[2]
    check(base is not None and struct.unpack_from('<II', base) == (3700, 12), 'factory settings layout is unchanged')
    choose = '10 btn 8\n100 btn 7\n150 btn 3\n250 enc 3 1\n1000 btn 9\n1500 end\n'
    rift = run(new, 'choose-rift', choose, base, 4)
    wanted = bytearray(base);wanted[AT] = 0xA5
    check(rift[2] == wanted, 'real GLO/SEL/K2 gesture edits only the tagged cosmetic byte')
    check(not rift[1] and peak(rift[0], 0, 1500) == 0, 'choosing a display starts no notes or MIDI')
    reload = run(new, 'rift-reload', '10 btn 8\n1000 btn 9\n1500 end\n', rift[2])
    check(reload[2] == rift[2], 'fresh process retains the chosen display')
    backward = run(old, 'old-firmware-compatibility', '10 btn 8\n1000 btn 9\n1500 end\n', rift[2])
    compatible = bytearray(backward[2]);compatible[AT] = 0
    check(compatible == base, 'old firmware still reads all musical settings from the new SAVE')
    back = run(new, 'choose-homunculus', choose.replace('enc 3 1', 'enc 3 -1'), rift[2])
    check(back[2] == base, 'turning K2 left restores the original SAVE exactly')

    navigation = ('400 btn 7\n450 btn 3\n500 enc 3 1\n600 btn 8\n'
                  '1500 btn 7\n1550 btn 3\n1600 enc 3 -1\n1700 btn 8\n')
    music = ('10 btn 8\n100 enc 6 {preset}\n200 keys 0x4880\n'
             '1200 midi 0xB0 64 127\n1800 keys 0\n2200 midi 0xB0 64 0\n'
             '10000 btn 9\n11000 end\n')
    for preset, family in [(0, 'pad'), (30, 'metal'), (36, 'kit'), (48, 'fm')]:
        events = ordered(music.format(preset=preset), navigation)
        updated = run(new, 'switch-'+family, events, base, 4 if preset == 0 else 0)
        previous = run(old, 'baseline-'+family, events, base)
        check(updated[:3] == previous[:3], family+': display changes alter no PCM, MIDI timestamps or final saved song')
        check(peak(updated[0], 220, 1100) > 0, family+': held music actually sounded in the fixture')
        if preset == 0:
            for suffix, frame in [('home-thorn-rift', 4), ('home-homunculus', 9)]:
                with Image.open(Path(str(updated[3])+f'-{frame:05d}.ppm')) as image:
                    image.resize((480, 480), Image.Resampling.NEAREST).save(folder/(suffix+'.png'))
    seq = ordered('10 btn 8\n100 btn 12\n4000 btn 12\n10000 btn 9\n11000 end\n', navigation)
    check(run(new, 'switch-sequence', seq, base)[:3] == run(old, 'baseline-sequence', seq, base)[:3],
          'sequencer audio, generated notes, timing and saved patterns stay exact')
    with Image.open(Path(str(rift[3])+'-00002.ppm')) as image:
        image.resize((480, 480), Image.Resampling.NEAREST).save(folder/'controls-thorn-rift.png')
    report = dict(checks=checks, settings_schema=12, settings_bytes=3700,
                  preference_payload_offset=3697, existing_save_compatible=True,
                  held_notes_pedals_four_families_and_sequencer='exact PCM/MIDI/save parity',
                  host_only=True, hardware_qualified=False)
    (folder/'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print('PASS actual-main HOME choice:', checks, 'checks; exact musical parity; hardware timing unqualified')


if __name__ == '__main__':
    main()
