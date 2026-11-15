# SPDX-License-Identifier: GPL-3.0-only
from pathlib import Path
import hashlib,json,os,re,subprocess,sys
from PIL import Image

import argparse
R=Path(__file__).resolve().parent.parent
ap=argparse.ArgumentParser(description='Capture illustrated-guide pages using real firmware on fake hardware only.')
ap.add_argument('--host',type=Path,help='Optional already-built native host executable')
ap.add_argument('--cc',help='C compiler; otherwise discovered by preview.py')
args=ap.parse_args()
P=R/'build/illustrated-guide';P.mkdir(parents=True,exist_ok=True)
OUT=P/'captures';OUT.mkdir(exist_ok=False)
IMAGES=R/'assets/tutorial/0.26.23-ns1';IMAGES.mkdir(parents=True,exist_ok=True)
sys.path.insert(0,str(R/'tools'));sys.path.insert(0,str(R/'tests'))
from check_remote import sysex,decode
if args.host:EXE=args.host.resolve()
else:
 from preview import build,compiler_command
 EXE=P/('guide-host.exe' if os.name=='nt' else 'guide-host')
 build(compiler_command(args.cc),R/'firmware/src/terkinstein.c',EXE,host=True,sanitize=False)
rows=json.loads((R/'assets/presets/terkinstein-presets.json').read_text(encoding='utf-8'))['presets']
factory=json.loads((R/'assets/presets/terkinstein-presets.json').read_text(encoding='utf-8'))['factory_order']
names=[rows[i]['name'] for i in factory]
patterns=[[(0,36),(4,36),(8,36),(12,36)],[(0,60),(4,63),(8,67),(12,63)],[(0,48)],[(0,36),(6,36),(8,43),(14,36)]]
setup=['10 btn 8']
t=30;rid=1
for tr,preset in enumerate(['SCRAP ALTAR','WICKED FELT','ASH CHOIR','HOLLOW ROOT']):
    if preset not in names:preset='ASH CHOIR'
    setup.append(sysex(t,0x17,rid,(tr,factory[names.index(preset)]))); t+=10;rid+=1
    setup.append(sysex(t,0x12,rid,(tr,0))); t+=10;rid+=1
    for step,note in patterns[tr]:
        setup.append(sysex(t,0x10,rid,(tr,step,1,note,100,16 if tr==1 else 4,0)));t+=10;rid+=1
assert t<500
seq=['600 btn 11','650 enc 0 -1']
scenes={
 'home':[],
 'night-suture':[sysex(600,0x16,100,(47,))],
 'night-suture-old':[sysex(600,0x16,100,(112,))],
 'memory':['600 btn 3'],
 'piano-roll':['600 btn 11','700 hold 3 1','1500 hold 3 0','1700 btn 3','1900 hold 3 1','2700 hold 3 0'],
 'sound-edit':['600 btn 6'],
 'track-sound':seq+['700 btn 8'],
 'sequencer':seq+['700 enc 4 50'],
 'step-entry':seq+['700 btn 6'],
 'envelope':['600 btn 4'],
 'fx':['600 hold 2 1','1400 hold 2 0'],
 'lfo':['600 hold 5 1','1400 hold 5 0'],
 'arpeggiator':['600 hold 10 1','1400 hold 10 0'],
 'arrangement':seq+['700 hold 3 1','1500 hold 3 0'],
 'step-edit':seq+['700 hold 3 1','1500 hold 3 0','1700 btn 3'],
 'control-feel':['600 btn 7','700 btn 3'],
 'metronome':['600 hold 11 1','1400 hold 11 0'],
 'saved':['3900 btn 9'],
 'reset-warning':['3300 hold 8 1','4100 hold 8 0'],
}
manifest={}
for name,events in scenes.items():
    script=OUT/(name+'.txt')
    script.write_text('\n'.join(sorted(setup+events,key=lambda s:int(s.split()[0]))+['4500 end'])+'\n',encoding='utf-8')
    out=OUT/name
    env=dict(os.environ,TK_HOST_SETTINGS_PATH=str(OUT/(name+'.settings')),TK_HOST_JOURNAL_PATH=str(OUT/(name+'.journal')))
    run=subprocess.run([str(EXE),str(out),str(script)],env=env,capture_output=True,text=True,check=True)
    out.with_suffix('.log').write_text(run.stdout,encoding='utf-8')
    assert 'ERROR' not in run.stdout
    replies=[decode(bytes.fromhex(x)) for x in re.findall(r'REMOTE out ([0-9A-F]+)',run.stdout)]
    assert len(replies)==sum(' sysex ' in x for x in setup+events)
    assert all(payload[0]==0 for command,rid,payload in replies),'Synthetic setup refused'
    raw=Path(str(out)+'-4200.ppm')
    with Image.open(raw) as im:
        assert im.size==(240,240)
        im.save(IMAGES/(name+'.png'))
    manifest[name+'.png']={'kind':'native host capture','source_frame_ms':4200,'script_sha256':hashlib.sha256(script.read_bytes()).hexdigest()}
for name,evidence in manifest.items():
    evidence['sha256']=hashlib.sha256((IMAGES/name).read_bytes()).hexdigest()
    with Image.open(IMAGES/name) as im:evidence['pixels']=list(im.size)
record={'firmware_version':'0.26.23-ns1','hardware_photographs':False,'capture':'Native release host preview; no hardware, MIDI device, audio playback or firmware change. All captures are exact native 240x240 pixels converted losslessly from PPM to PNG.','firmware_source_sha256':hashlib.sha256((R/'firmware/src/terkinstein.c').read_bytes()).hexdigest(),'factory_bank_sha256':hashlib.sha256((R/'firmware/gen/terkinstein_presets.h').read_bytes()).hexdigest(),'host_executable_sha256':hashlib.sha256(EXE.read_bytes()).hexdigest(),'images':manifest}
(P/'capture-provenance.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
print('Captured '+str(len(scenes))+' current native pages; no hardware accessed.')
