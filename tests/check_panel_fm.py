#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual-app checks for labeled key shortcuts, FM patches and legacy SAVE."""
from __future__ import annotations
import argparse
from pathlib import Path
import struct
import tempfile
from PIL import Image
from check_external_midi import Panel, ordered, peak

FM_AT=3456                       # four-byte host header + payload offset 3452
MONO_AT=3696
LABELS=(1,3,5,8,10,13,15,17,20,22)

def chord(key: int, start: int=100) -> str:
    return (f"{start} hold 6 1\n{start+20} keys {1<<key}\n"
            f"{start+70} hold 6 0\n{start+90} keys 0\n")

def main() -> None:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable",type=Path)
    parser.add_argument("output",type=Path)
    args=parser.parse_args()
    prefix=args.output.resolve(); prefix.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="tk-panel-") as temp:
        panel=Panel(args.executable.resolve(),prefix,Path(temp))
        factory=panel.run("fm-factory","10 btn 8\n30 enc 6 32\n400 btn 9\n1000 end\n")[2]
        assert factory is not None and struct.unpack_from("<II",factory)==(3700,11)
        for index,key in enumerate(LABELS):
            audio,midi,saved,out=panel.run(f"label-{index}",
                "10 btn 8\n30 enc 6 32\n"+chord(key)+"700 btn 9\n1000 end\n",fps=2)
            assert not midi and peak(audio,0,900)==0, (key,"label gesture played a note")
            assert saved is not None and saved[MONO_AT]==(index==8)
            with Image.open(Path(f"{out}-00001.ppm")) as screen:
                screen.resize((480,480),Image.Resampling.NEAREST).save(out.with_suffix(".png"))
        # A key already sounding before EDIT stays a key, and shortcut keys
        # remain consumed after releasing EDIT until the key itself releases.
        _,midi,saved,_=panel.run("already-held",
            "10 btn 8\n100 keys 2\n150 hold 6 1\n250 hold 6 0\n"
            "400 keys 0\n1000 btn 9\n6000 end\n")
        assert [(ms,st,n) for ms,st,n,_ in midi]==[(100,0x90,42),(400,0x80,42)]
        assert saved is not None and saved[MONO_AT]==0
        _,midi,_,_=panel.run("consume-through-release",
            "10 btn 8\n100 hold 6 1\n120 keys 2\n170 hold 6 0\n"
            "400 keys 0\n600 keys 2\n900 keys 0\n6000 end\n")
        assert [(ms,st,n) for ms,st,n,_ in midi]==[(600,0x90,42),(900,0x80,42)]
        # Physical taps use release; a modifier chord must not execute its
        # page-specific tap action (ENV HOLD would emit CC64).
        _,midi,_,_=panel.run("env-modifier",
            "10 btn 8\n50 btn 4\n"+chord(3)+"1200 end\n")
        assert not midi,"EDIT+OP2 also toggled ENV HOLD"
        _,midi,_,_=panel.run("env-tap",
            "10 btn 8\n50 btn 4\n100 hold 6 1\n180 hold 6 0\n1000 end\n")
        assert (180,0xB0,64,127) in midi
        print("Panel: all ten label shortcuts are silent; held notes and EDIT tap actions keep ownership")
        for operator,key in enumerate(LABELS[:6]):
            events=("220 enc 2 3\n230 enc 3 -7\n240 enc 4 4\n"
                    "250 enc 5 5\n260 enc 0 -6\n270 enc 1 8\n")
            _,_,saved,_=panel.run(f"operator-{operator}",
                "10 btn 8\n30 enc 6 32\n"+chord(key)+events+"500 btn 9\n1000 end\n")
            assert saved is not None
            at=FM_AT+operator*6
            old=factory[at:at+6]
            expect=bytes((min(31,old[0]+3),max(0,old[1]-7),min(127,old[2]+4),
                          min(127,old[3]+5),max(0,old[4]-6),min(127,old[5]+8)))
            assert saved[at:at+6]==expect,(operator,saved[at:at+6],expect)
            for other in range(6):
                if other!=operator:
                    pos=FM_AT+other*6
                    assert saved[pos:pos+6]==factory[pos:pos+6]
        _,_,pitch,out=panel.run("pitch",
            "10 btn 8\n30 enc 6 32\n"+chord(15)+
            "220 enc 2 7\n230 enc 3 9\n240 enc 4 12\n700 btn 9\n1000 end\n",fps=2)
        assert pitch is not None and pitch[FM_AT+38]==31 and pitch[FM_AT+39]==factory[FM_AT+39]+9
        assert pitch[3454]==14
        _,_,glob,out=panel.run("routing",
            "10 btn 8\n30 enc 6 32\n"+chord(17)+
            "220 enc 4 3\n230 enc 2 12\n700 btn 9\n1000 end\n",fps=2)
        assert glob is not None and glob[FM_AT+36]==4 and glob[FM_AT+37]==factory[FM_AT+37]+12
        for label in ("pitch","routing"):
            screen=prefix.with_name(prefix.name+"-"+label)
            with Image.open(Path(f"{screen}-00001.ppm")) as image:
                image.resize((480,480),Image.Resampling.NEAREST).save(screen.with_suffix(".png"))
        print("FM editor: each actual operator has ratio/level/ADSR; PIT and GLO edit the correct saved fields")
        # Selecting MONO while recording cannot accidentally record its key.
        _,_,empty,_=panel.run("rec-shortcuts",
            "10 btn 8\n100 btn 11\n120 btn 13\n"+chord(20,200)+
            "1000 btn 13\n1200 btn 9\n6000 end\n")
        assert empty is not None and not any(empty[24+64*13+step*13] for step in range(64))
        # Capture the FM timbre once; further live edits never rewrite it.
        _,_,captured,_=panel.run("capture-fm",
            "10 btn 8\n30 enc 6 32\n100 btn 11\n120 btn 13\n"
            "250 midi 0x90 60 100\n"+chord(1,400)+
            "700 enc 3 -30\n900 midi 0x80 60 0\n1200 btn 13\n1500 btn 9\n15000 end\n")
        assert captured is not None and captured[FM_AT+80:FM_AT+120]==factory[FM_AT:FM_AT+40]
        assert captured[FM_AT:FM_AT+40]!=captured[FM_AT+80:FM_AT+120]
        print("FM capture: selected track keeps its six operators; modifier keys cannot enter REC")
        # Factory target endpoint and zero are exact for FM as for old engines.
        _,_,end,_=panel.run("fm-graft-end",
            "10 btn 8\n30 enc 6 32\n100 btn 6\n200 enc 0 127\n400 btn 9\n1000 end\n")
        _,_,target,_=panel.run("fm-graft-target","10 btn 8\n30 enc 6 33\n400 btn 9\n1000 end\n")
        assert end is not None and target is not None and end[FM_AT:FM_AT+40]==target[FM_AT:FM_AT+40]
        reloaded=panel.run("fm-reload","10 btn 8\n400 btn 9\n1000 end\n",store=end)[2]
        assert reloaded==end
        # A non-factory schema-9 save migrates all original fields exactly,
        # initializing FM metadata and POLY without changing existing sounds.
        source=panel.run("old-source",
            "10 btn 8\n100 enc 6 30\n150 enc 2 17\n200 midi 0xB0 24 91\n400 btn 9\n1000 end\n")[2]
        assert source is not None
        old=bytearray(source[:3456]); struct.pack_into("<II",old,0,3452,9)
        migrated=panel.run("schema9-migrate","10 btn 8\n400 btn 9\n1000 end\n",store=bytes(old))[2]
        assert migrated is not None and migrated[8:3455]==source[8:3455] and migrated[MONO_AT]==0
        invalid=bytearray(factory); invalid[FM_AT]=32
        fallback=panel.run("invalid-operator","10 btn 8\n400 btn 9\n1000 end\n",store=bytes(invalid))[2]
        assert fallback is not None and fallback[13]==0
        print("SAVE: schema-9 migration preserves existing fields; FM morph/capture reload and malformed rejection pass")
    print("PASS: labeled panel controls and six-operator FM app integration (host only)")

if __name__=="__main__": main()
