#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Real main-loop photo controls, holds, input ownership and SAVE guards."""
import sys
from pathlib import Path
from check_remote import run,sysex,answer
from PIL import Image

def frame(t,rid,seq,action,a=0,b=0,token=1):
    data=(action,token&127,token>>7,seq&127,seq>>7)
    return sysex(t,0x41,rid,data+(() if action==3 else (a,b)))

def main():
    exe,folder=map(lambda p:Path(p).resolve(),sys.argv[1:3]);folder.mkdir(parents=True,exist_ok=True)
    checks=0
    def check(ok,why):
        nonlocal checks
        checks+=1;assert ok,why
    def test(name,lines,end=1200):
        lines=sorted(['10 btn 8',sysex(20,0x41,1,(0,)),*lines],key=lambda s:int(s.split()[0]))
        return run(exe,folder,name,lines,end)
    queries=[sysex(800,2,90),sysex(810,0x0C,91)]
    # All button taps enter the same handlers as a real held/released button.
    for button in range(14):
        base=[sysex(40,0x16,2,(14,))]
        physical=test(f'physical-{button}',base+[f'100 hold {button} 1',f'140 hold {button} 0',*queries])[2]
        virtual=test(f'virtual-{button}',base+[frame(100,3,1,1,(1<<button)&127,(1<<button)>>7),frame(140,4,2,1),*queries])[2]
        check(answer(physical,90)==answer(virtual,90),f'button {button} changed state differently')
        check(answer(physical,91)==answer(virtual,91),f'button {button} entered wrong page')
    # All seven encoder roles use the ordinary page/control dispatch.
    for encoder in range(7):
        base=[sysex(40,0x16,2,(14,))]
        physical=test(f'physical-encoder-{encoder}',base+[f'100 enc {encoder} 1',*queries])[2]
        virtual=test(f'virtual-encoder-{encoder}',base+[frame(100,3,1,2,encoder,65),*queries])[2]
        check(answer(physical,90)==answer(virtual,90),f'encoder {encoder} changed wrong field')
    # A long virtual HOME cannot invoke physical HOME's factory reset.
    lines=[sysex(40,0x16,2,(57,))]
    lines += [frame(t,3+i,i+1,1,0,2) for i,t in enumerate(range(100,1251,150))]
    lines += [frame(1320,20,9,1),sysex(1400,2,90)]
    replies=test('home-no-factory-reset',lines,1600)[2]
    check(answer(replies,90)[10]==57,'virtual HOME held reset the sound')
    # FX held long opens its editor without also toggling the enable switch.
    lines=[sysex(60,2,89)]+[frame(t,3+i,i+1,1,4,0) for i,t in enumerate(range(100,851,150))]
    lines += [frame(900,20,7,1),sysex(1000,2,90),sysex(1010,0x0C,91)]
    out,_,replies=test('fx-held',lines,1200)
    check(answer(replies,89)[27]==answer(replies,90)[27],'FX hold also toggled effects')
    check(answer(replies,91)[3]==13,'FX hold did not enter FX editor')
    with Image.open(Path(f'{out}-1000.ppm')) as image:image.resize((720,720),Image.Resampling.NEAREST).save(folder/'virtual-fx-hold.png')
    # An expired or explicitly canceled partial tap does not execute a short tap.
    for mode in ('expired','canceled','usb-reset'):
        lines=[sysex(60,2,89),frame(100,3,1,1,4,0)]
        if mode=='canceled':lines += [frame(200,4,2,3)]
        elif mode=='usb-reset':lines += ['200 usbreset 0']
        lines += queries
        replies=test(mode,lines)[2]
        check(answer(replies,89)[27]==answer(replies,90)[27],f'{mode} synthesized an FX tap')
        check(answer(replies,91)[3]==0,f'{mode} synthesized FX hold')
    # Physical and virtual holders share logical OR ownership, with one release.
    physical=test('overlap-baseline',['100 hold 2 1','300 hold 2 0',*queries])[2]
    overlap=test('overlap',['100 hold 2 1',frame(150,3,1,1,4,0),'200 hold 2 0',frame(300,4,2,1),*queries])[2]
    check(answer(physical,90)==answer(overlap,90),'overlap generated duplicate FX tap')
    # A retried relative request can never apply a second detent.
    replies=test('retry',[sysex(40,0x16,2,(14,)),frame(100,3,1,2,6,65),frame(110,3,1,2,6,65),*queries])[2]
    check(answer(replies,90)[10]==15,'retry applied relative encoder twice')
    check(len(replies[3])==2 and replies[3][0][2]==replies[3][1][2],'retry ACK changed')
    # Stale token cannot regain ownership after a new claim.
    replies=test('old-session',[frame(100,3,1,1,4,0),sysex(150,0x41,4,(0,)),frame(200,5,2,1,4,0),*queries])[2]
    check(answer(replies,5,3)==b'\3','old session accepted')
    check(answer(replies,91)[3]==0,'old session changed the page')
    # Virtual EDIT consumes physical shortcuts silently, like physical EDIT.
    out,log,replies=test('edit-shortcut',[frame(100,3,1,1,64,0),'120 keys 2','160 keys 0',frame(180,4,2,1),*queries])
    check('MIDI out' not in log,'EDIT shortcut played a musical note')
    check(answer(replies,91)[3]==9,'EDIT OP1 shortcut entered wrong editor')
    # Virtual SAVE retains the real flash deferral while a key is held.
    out,log,replies=test('save-silence',[sysex(40,0x16,2,(14,)),'100 keys 1',frame(200,3,1,1,0,4),
        frame(230,4,2,1),sysex(800,2,90),'900 keys 0',sysex(8200,2,91)],8500)
    check(bool(answer(replies,90)[3]&8),'SAVE wrote while a physical key was held')
    check(not answer(replies,91)[3]&8 and 'settings saved' in log,'SAVE failed to complete after silence')
    print(f'Panel input: {checks} actual-main checks passed')

if __name__=='__main__':main()
