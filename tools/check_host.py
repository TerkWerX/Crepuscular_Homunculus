#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Focused free-firmware host suite. Fake files only; never opens MIDI/audio."""
import argparse,os,subprocess
from pathlib import Path
from preview import ROOT,compiler_command

TESTS=('engine','controllers','transport','fm','update_identity','product_label','home_visual_choice','homunculus_intro',
       'homunculus_visual','song_persistence','catalog_revision_app','panel_input_app','panel_track_edit_app',
       'piano_app','remote_protocol','screen','visual','solo_app','metronome_app','home_reset_app')

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cc',default=os.environ.get('CC'))
    p.add_argument('--asan',action='store_true');args=p.parse_args()
    cc=compiler_command(args.cc);msvc=Path(cc[0]).stem.lower()=='cl'
    out=ROOT/'build/host-checks';out.mkdir(parents=True,exist_ok=True)
    env={k:v for k,v in os.environ.items() if not k.upper().startswith('TK_HOST_')}
    for name in TESTS:
        source=ROOT/f'tests/test_{name}.c';exe=out/(name+('.exe' if os.name=='nt' else ''))
        if msvc:
            flags=['/nologo','/W3','/D_CRT_SECURE_NO_WARNINGS',f'/I{ROOT/"firmware/src"}',f'/I{ROOT/"firmware/gen"}']
            flags+=['/Od','/Zi','/fsanitize=address'] if args.asan else ['/O2']
            command=cc+flags+[f'/Fe:{exe}',f'/Fo:{out/(name+".obj")}',f'/Fd:{out/(name+".pdb")}',str(source)]
        else:
            flags=['-std=c99','-O2','-I'+str(ROOT/'firmware/src'),'-I'+str(ROOT/'firmware/gen')]
            if args.asan:flags+=['-g','-fsanitize=address','-fno-omit-frame-pointer']
            command=cc+flags+['-o',str(exe),str(source)]
        subprocess.run(command,cwd=ROOT,env=env,check=True)
        subprocess.run([str(exe)]+([str(out)] if name in ('product_label','home_visual_choice') else []),cwd=out,env=env,check=True)
    print(f'Passed {len(TESTS)} host groups'+(' with ASan' if args.asan else '')+'. No hardware qualification.')
if __name__=='__main__':main()
