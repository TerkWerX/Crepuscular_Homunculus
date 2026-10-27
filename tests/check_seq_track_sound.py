#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Real main-loop SEQ/HOME/EDIT sound regression; fake IO, silent WAV only."""
from pathlib import Path
import json
import os
import re
import subprocess
import sys
import wave
from PIL import Image
from check_remote import answer, decode, sysex


def main():
    exe, folder = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
    expect_bug = '--expect-bug' in sys.argv[3:]
    folder.mkdir(parents=True, exist_ok=True)
    checks = 0
    def check(value, why):
        nonlocal checks
        checks += 1
        assert value, why
    def run(name, lines, end=3000, saved=None, journal=None):
        out = folder/name
        script, settings, nor = out.with_suffix('.txt'), out.with_suffix('.bin'), out.with_suffix('.nor')
        for path in (settings, nor):
            if path.exists(): path.unlink()  # Only this test's generated fixtures.
        if saved is not None: settings.write_bytes(saved)
        if journal is not None: nor.write_bytes(journal)
        env = {k:v for k,v in os.environ.items() if not k.upper().startswith('TK_HOST_')}
        env.update(TK_HOST_SETTINGS_PATH=str(settings), TK_HOST_JOURNAL_PATH=str(nor), TK_SCREEN_VALIDATE='1')
        script.write_text('\n'.join(sorted(lines, key=lambda x:int(x.split()[0])))+f'\n{end} end\n')
        result = subprocess.run([str(exe),str(out),str(script)],env=env,text=True,
                                capture_output=True,check=True,timeout=90)
        out.with_suffix('.log').write_text(result.stdout+result.stderr)
        replies = {}
        for m in re.finditer(r'(\d+) ms\s+REMOTE out ([0-9A-F]+)',result.stdout):
            command,rid,payload = decode(bytes.fromhex(m[2]))
            replies.setdefault(rid,[]).append((int(m[1]),command,payload))
        with wave.open(str(out)+'.wav','rb') as f: pcm=f.readframes(f.getnframes())
        return pcm,replies,settings.read_bytes() if settings.exists() else None,nor.read_bytes() if nor.exists() else None,out
    # Pure setup commands establish distinct sounds. Navigation and sound
    # selection under test use physical SELECT/HOME/PRESETS/SEQ/EDIT events.
    presets=(30,43,3,36)
    setup=['10 btn 8',sysex(30,0x16,1,(14,))]
    setup += [sysex(50+t*10,0x17,2+t,(t,(p-1)%64)) for t,p in enumerate(presets)]
    notes=['600 keys 0x80','950 keys 0']
    def nav(track): return ['100 btn 11',f'120 enc 0 {track-1}','160 btn 8','200 enc 6 1']
    # A kit's first 96 noise frames deliberately vary with command revision;
    # navigation legitimately changes revisions. Compare the deterministic
    # kick body/tail after that transient, plus exact queried patch fields.
    def slice_audio(pcm): return pcm[620*44100//1000*4:2000*44100//1000*4]
    a=run('drum-home',setup+nav(0)+notes)
    b=run('drum-step-entry',setup+nav(0)+['300 btn 11','350 btn 6']+notes)
    equal=slice_audio(a[0])==slice_audio(b[0])
    if expect_bug:
        check(not equal,'baseline unexpectedly already matches HOME and SEQ EDIT')
        (folder/'reproduction.json').write_text(json.dumps(dict(
            bug_reproduced=True,HOME_and_SEQ_EDIT_PCM_equal=equal,
            path='SEQ SELECT DRUM / HOME / PRESETS to SCRAP ALTAR / SEQ / EDIT / panel C',
            hardware_IO=False),indent=2)+'\n')
        print('REPRODUCED: SCRAP ALTAR HOME and SEQ EDIT produce different PCM (old app).')
        return
    check(equal,'SCRAP ALTAR step entry must sound exactly like its selected HOME capture')
    for track,preset in enumerate(presets):
        home=run(f'home-{track}',setup+nav(track)+notes)
        step=run(f'step-{track}',setup+nav(track)+['300 btn 11','350 btn 6']+notes+[
            sysex(1100,2,20),sysex(1150,3,21,(track,0)),sysex(1200,9,22,(track,))])
        check(slice_audio(home[0])==slice_audio(step[0]),f'track{track}: HOME / step-entry PCM differs')
        state=answer(step[1],20)
        check(state[10]==14 and state[15+track]==preset,f'track{track}: wrong LIVE/captured preset')
        check(answer(step[1],21)[3]>=1 and 48 in answer(step[1],21)[4::4],f'track{track}: step was not entered')
        browse=run(f'browse-{track}',setup+nav(track)+['300 btn 11','350 btn 6','500 enc 6 1',
            sysex(550,2,20),'600 enc 0 1','700 enc 6 1',sysex(750,2,21)])
        state=answer(browse[1],20)
        check(state[10]==14 and state[15+track]==(preset+1)%64,f'track{track}: step PRESETS must edit capture only')
        other=(track+1)%4;state=answer(browse[1],21)
        check(state[10]==14 and state[15+track]==(preset+1)%64 and state[15+other]==presets[other],
              f'track{track}: SELECT left a stale workbench')
        # SELECT while editing must match the target track's own HOME audition.
        moved=run(f'moved-{track}',setup+nav(track)+['300 btn 11','350 btn 6','450 enc 0 1']+notes)
        # Target has not been advanced by PRESETS in this fixture.
        target=run(f'target-{track}',setup+['100 btn 11',f'120 enc 0 {other-1}','160 btn 8']+notes)
        check(slice_audio(moved[0])==slice_audio(target[0]),f'track{track}: new selected track auditions wrong patch')
    # Exiting step entry restores LIVE, without recapturing the track.
    plain=run('live-reference',setup+notes)
    leave=run('step-to-live',setup+nav(0)+['300 btn 11','350 btn 6','450 btn 6']+notes+[
        sysex(1200,2,20)])
    check(slice_audio(plain[0])==slice_audio(leave[0]),'leaving step entry did not restore independent LIVE')
    check(answer(leave[1],20)[15]==30,'leaving step entry overwrote captured kit')
    # Existing keys/HOLD belong to their original gesture when navigating.
    held=['250 keys 0x80','1700 keys 0']
    plain=run('held-plain',setup+nav(1)+held)
    moved=run('held-navigation',setup+nav(1)+held+['300 btn 11','350 btn 6','450 enc 0 1'])
    check(plain[0]==moved[0],'navigation changed held panel note or tail PCM')
    # Ordinary USB keyboard ownership stays LIVE throughout captured editing.
    midi=['250 midi 144 60 100','1700 midi 128 60 0']
    plain=run('usb-live',setup+midi)
    moved=run('usb-edit',setup+nav(0)+midi+['300 btn 11','350 btn 6','450 enc 0 1'])
    check(plain[0]==moved[0],'step editor changed ordinary external LIVE keyboard sound')
    # SAVE/restart from step context stores real LIVE plus all captured sounds,
    # all banks and the entered note, never the temporary editor workbench.
    save=run('save-step',setup+nav(0)+['300 btn 11','350 btn 6']+notes+[
        '5000 btn 9',sysex(5600,2,30),sysex(5650,9,31,(0,)),sysex(5700,3,32,(0,0)),
        sysex(5750,7,33)],6500)
    check(save[2] is not None and save[3] is not None,'native SAVE did not complete')
    reload=run('reload-step',[sysex(100,2,30),sysex(150,9,31,(0,)),
        sysex(200,3,32,(0,0)),sysex(250,7,33)],1500,saved=save[2],journal=save[3])
    state=answer(reload[1],30)
    check(state[10]==14 and state[15]==30 and not (state[3]&1),'restart lost LIVE/kit or started playback')
    for rid in (31,32):check(answer(save[1],rid)==answer(reload[1],rid),f'native SAVE/restart mismatch {rid}')
    # Bank revision/dirty notifications restart after boot; active banks,
    # lengths and music are persistent. Compare the actual bank steps below.
    check(answer(save[1],33)[5:33]==answer(reload[1],33)[5:33], 'restart changed active banks or lengths')
    query=[];rid=1
    for track in range(4):
        query.append(sysex(100+rid*2,9,rid,(track,)));rid+=1
        for bank in range(4):
            for step in (0,15,63):
                query.append(sysex(100+rid*2,8,rid,(track,bank,step)));rid+=1
    # Fresh readback before and after a second SAVE/restart compares all16
    # banks' first/boundary/final steps and all four complete captured patches.
    readback=run('readback-all-banks',query+['700 btn 9'],1500,saved=save[2],journal=save[3])
    again=run('readback-all-banks-restart',query,1500,saved=readback[2],journal=readback[3])
    for r in range(1,rid):check(answer(readback[1],r)==answer(again[1],r),f'bank/patch readback mismatch {r}')
    # These exact records also serve as old-firmware readback compatibility fixtures.
    for name in ('drum-step-entry','step-1','browse-0','save-step'):
        ppm=folder/f'{name}-1000.ppm'
        if ppm.exists():
            with Image.open(ppm) as im:im.resize((720,720),Image.Resampling.NEAREST).save(ppm.with_suffix('.png'))
    (folder/'results.json').write_text(json.dumps(dict(checks=checks,passed=True,
        PCM='silent offline, actual main loop',hardware_IO=False),indent=2)+'\n')
    print(f'PASS: {checks} SEQ captured-sound checks; four-track PCM, navigation, releases, LIVE ownership and SAVE/restart.')


if __name__=='__main__':main()
