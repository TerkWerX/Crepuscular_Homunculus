#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Native panel gestures, protocol readback, persistence and PCM with fake FM-1."""
import os, re, subprocess, sys
from pathlib import Path
from PIL import Image
from check_remote import answer, decode, run, sysex

def main():
    exe, folder = map(lambda s: Path(s).resolve(), sys.argv[1:3])
    folder.mkdir(parents=True, exist_ok=True)
    checks = 0
    def check(ok, why):
        nonlocal checks
        checks += 1
        assert ok, why
    def ordered(lines): return sorted(lines, key=lambda s: int(s.split()[0]))
    nav = ['100 btn 11','200 hold 3 1','950 hold 3 0','1000 btn 3',
           '1100 hold 3 1','1850 hold 3 0']
    setup = ['10 btn 8', sysex(30,0x16,1,(14,)),sysex(50,0x11,2,(1,)),
             sysex(60,0x12,3,(1,2)),sysex(70,0x17,4,(1,43)),
             sysex(80,0x10,5,(1,15,3,60,100,64,0,64,80,4,0,67,70,127,1))]
    song, journal = folder/'save.core', folder/'save.nor'
    assert not song.exists() and not journal.exists(), 'Use a fresh output folder'
    lines = ordered(setup + nav + [sysex(1900,0x0C,10), sysex(1920,2,11),
        '2000 enc 0 15','2100 enc 2 -48','2200 enc 3 -40',sysex(2250,3,12,(1,15)),
        '2300 btn 3',sysex(2350,3,13,(1,15)), '2400 btn 13',sysex(2450,3,14,(1,15)),
        '2500 btn 3',sysex(2550,3,15,(1,15)),'2600 enc 6 10',sysex(2650,2,16),
        '2700 btn 12',sysex(2750,0x0C,17),sysex(2770,2,18),
        '2900 btn 12',sysex(2950,2,19), '3000 enc 0 1',
        '3400 enc 0 -1','3500 enc 1 4','3600 enc 2 12','3700 enc 3 10',
        sysex(3800,3,20,(1,15)), '5000 btn 9',sysex(5500,2,21)])
    script, out = folder/'edit-save.txt', folder/'edit-save'
    script.write_text('\n'.join(lines+['6000 end'])+'\n')
    env = dict(os.environ, TK_HOST_SETTINGS_PATH=str(song), TK_HOST_JOURNAL_PATH=str(journal))
    result = subprocess.run([str(exe),str(out),str(script)], env=env, check=True, capture_output=True,text=True)
    out.with_suffix('.log').write_text(result.stdout); replies={}
    for m in re.finditer(r'(\d+) ms\s+REMOTE out ([0-9A-F]+)',result.stdout):
        cmd,rid,payload=decode(bytes.fromhex(m[2])); replies.setdefault(rid,[]).append((int(m[1]),cmd,payload))
    check(answer(replies,10)[3] == 19, 'held SEL in STEP EDIT must open native piano roll')
    check(not answer(replies,11)[3]&1, 'opening editor must not start transport')
    expected=bytes((0,1,15,3,60,60,16,0,64,80,4,0,67,70,127,1))
    check(answer(replies,12)==expected,'length/velocity edit must change only C4')
    check(answer(replies,13)==expected,'SEL on existing onset must preserve all fields')
    check(answer(replies,14)==bytes((0,1,15,2,64,80,4,0,67,70,127,1)), 'REC must remove only selected pitch')
    check(answer(replies,15)==expected,'SEL must reinsert sorted pitch with chosen gate/level')
    check(answer(replies,16)[10]==14 and answer(replies,16)[16]==43,'PRESETS in roll must not replace hidden live/track sound')
    check(answer(replies,17)[3]==19 and answer(replies,18)[3]&1, 'PLAY must start sequencer and retain roll view')
    check(not answer(replies,19)[3]&1,'PLAY again must stop')
    check(answer(replies,20)==bytes((0,1,15,3,60,60,16,0,64,90,16,0,67,70,127,1)), 'second chord member independently resized')
    check(not answer(replies,21)[3]&8,'physical SAVE completed on fake flash')
    with Image.open(Path(f'{out}-4200.ppm')) as img:
        img.resize((720,720),Image.Resampling.NEAREST).save(folder/'piano-chord.png')
    reboot=folder/'reboot.txt';reboot.write_text('10 btn 8\n'+sysex(100,3,1,(1,15))+'\n500 end\n')
    result=subprocess.run([str(exe),str(folder/'reboot'),str(reboot)],env=env,check=True,capture_output=True,text=True)
    decoded=[decode(bytes.fromhex(m[1]))[2] for m in re.finditer(r'REMOTE out ([0-9A-F]+)',result.stdout)]
    check(decoded[0]==answer(replies,20),'fresh-process reload must retain all individual chord gates')
    # Identical external audio across navigation; no MIDI-triggered audition.
    notes=['50 midi 144 60 100','3000 midi 128 60 0']
    base,_,_=run(exe,folder,'pcm-baseline',ordered(setup+notes),4000)
    viewed,_,_=run(exe,folder,'pcm-roll',ordered(setup+notes+nav),4000)
    check(base.with_suffix('.wav').read_bytes()==viewed.with_suffix('.wav').read_bytes(),'navigation must leave existing external MIDI PCM unchanged')
    _,_,cancel=run(exe,folder,'held-sel-leave',ordered(setup+nav+[
        '2000 hold 3 1','2100 btn 8','2800 hold 3 0',sysex(3000,0x0C,1)]),3400)
    check(answer(cancel,1)[3]==0,'leaving roll cancels unfinished SEL hold rather than reopening an arrangement page')
    print(f'PASS: {checks} real-app piano gesture/save/PCM checks (host only)')
if __name__=='__main__': main()
