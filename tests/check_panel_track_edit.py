#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Selected-track HOME through real app input, protocol, PCM and SAVE/restart."""
import os
from pathlib import Path
import re
import subprocess
import sys
from PIL import Image
from check_remote import answer, decode, run, sysex

def main():
    exe, folder = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
    folder.mkdir(parents=True, exist_ok=True)
    checks = 0
    def check(ok, why):
        nonlocal checks
        checks += 1
        assert ok, why
    def ordered(lines): return sorted(lines, key=lambda line: int(line.split()[0]))
    setup = ['10 btn 8', sysex(30, 0x16, 1, (14,))]
    setup += [sysex(50 + t*10, 0x17, 2+t, (t, p)) for t, p in enumerate((55,43,3,36))]
    for track, preset in enumerate((55,43,3,36)):
        nav = ['100 btn 11', sysex(120, 0x11, 10, (track,)), '160 btn 8']
        out, _, replies = run(exe, folder, f'home-track-{track}', ordered(setup + nav + [
            sysex(220, 2, 11), sysex(240, 9, 12, (track,)), '1100 enc 2 7',
            sysex(1150, 9, 13, (track,)), '1200 enc 6 1', sysex(1250, 2, 14),
            '1300 btn 8', sysex(1400, 2, 15)]), 1700)
        check(answer(replies, 11)[10] == 14 and answer(replies, 11)[15+track] == preset,
              'HOME navigation changed the independent live or track preset')
        check(answer(replies, 13)[2] == min(127, answer(replies, 12)[2] + 7),
              'track HOME macro edits did not reach the selected captured patch')
        check(answer(replies, 14)[15+track] == (preset+1)%64 and answer(replies, 14)[10] == 14,
              'PRESETS in track context edited the wrong sound')
        check(answer(replies, 15)[10] == 14, 'return to live lost its preset')
        with Image.open(Path(f'{out}-1000.ppm')) as image:
            image.resize((720,720), Image.Resampling.NEAREST).save(Path(f'{out}-1000.png'))
    # Entering a track context must not republish its patch to an external
    # ordinary live keyboard, even while that keyboard is already sounding.
    notes = ['90 midi 144 60 100', '1250 midi 128 60 0']
    a, _, _ = run(exe, folder, 'external-live-baseline', ordered(setup+notes), 2100)
    b, _, _ = run(exe, folder, 'external-live-track-view', ordered(setup+notes+[
        '100 btn 11', sysex(120,0x11,10,(1,)), '160 btn 8', '900 btn 8']), 2100)
    check(a.with_suffix('.wav').read_bytes() == b.with_suffix('.wav').read_bytes(),
          'track view changed existing ordinary live MIDI PCM')
    # SAVE uses the restored real settings, never the temporary workbench.
    settings, journal = folder/'save.bin', folder/'save.nor'
    for path in (settings, journal):
        if path.exists(): path.unlink()
    env = dict(os.environ, TK_HOST_SETTINGS_PATH=str(settings), TK_HOST_JOURNAL_PATH=str(journal))
    script=folder/'save-context.txt'
    script.write_text('\n'.join(ordered(setup + ['100 btn 11',sysex(120,0x11,10,(1,)),
        '160 btn 8','200 enc 2 7','250 btn 9',sysex(500,2,11)]) + ['1500 end'])+'\n')
    subprocess.run([str(exe),str(folder/'save-context'),str(script)],env=env,check=True,capture_output=True)
    script=folder/'restart.txt'
    script.write_text(sysex(100,2,12)+'\n'+sysex(150,9,13,(1,))+'\n500 end\n')
    result=subprocess.run([str(exe),str(folder/'restart'),str(script)],env=env,check=True,text=True,capture_output=True)
    replies={}
    for m in re.finditer(r'(\d+) ms\s+REMOTE out ([0-9A-F]+)',result.stdout):
        command,rid,payload=decode(bytes.fromhex(m[2])); replies.setdefault(rid,[]).append((int(m[1]),command,payload))
    check(answer(replies,12)[10] == 14 and answer(replies,12)[16] == 43,
          'SAVE/restart stored a track workbench over the independent live sound')
    # The protocol replies from the initial test give this catalog's exact base.
    _,_,base=run(exe,folder,'macro-base',ordered(setup+[sysex(100,9,15,(1,))]),200)
    check(answer(replies,13)[2] == min(127,answer(base,15)[2]+7),
          'SAVE/restart lost the captured macro edit')
    # Recording from the captured HOME context keeps the displayed instrument.
    _,_,replies=run(exe,folder,'record-track-context',ordered(setup+[
        '100 btn 11',sysex(120,0x11,10,(1,)), '160 btn 8',
        sysex(210,0x11,20,(0,)),sysex(220,0x15,21,(1,)),sysex(230,0x1F,22,(0,1,0)),
        sysex(240,7,23),'250 btn 13',sysex(260,7,24),
        '300 keys 1','450 keys 0','600 btn 13',sysex(650,2,11),sysex(700,3,12,(1,0)),
        sysex(750,0x15,13,(0,))]),1200)
    check(answer(replies,11)[10] == 14 and answer(replies,11)[16] == 43,
          'physical REC replaced the track sound with the independent live patch')
    check(answer(replies,12)[3] == 1, 'recording from track HOME lost the physical note')
    check(answer(replies,11)[4] == 1, 'physical REC followed a remote selection instead of the displayed track')
    check(answer(replies,23)[9] == answer(replies,24)[9] == 1,
          'recording the displayed track canceled another track\'s pending bank')
    print(f'PASS: {checks} actual-app track HOME checks (host only)')

if __name__ == '__main__': main()
