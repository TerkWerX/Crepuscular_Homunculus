#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build the desktop simulator, render its WAV/screens, and optionally check DSP."""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent


def compiler_command(value: str | None) -> list[str]:
    if value:
        # An exact compiler path can contain spaces without shell quoting.
        if Path(value).is_file():
            return [value]
        parts = shlex.split(value, posix=os.name != "nt")
        return [p.strip('"') for p in parts]
    for name in ("cc", "clang", "gcc", "cl"):
        if path := shutil.which(name):
            return [path]
    raise RuntimeError(
        "No C compiler found. Install gcc/clang, use a Visual Studio Developer "
        "PowerShell, or pass --cc PATH (also accepts CC). See README.md."
    )


def build(cc: list[str], source: Path, output: Path, *, host: bool, sanitize: bool) -> None:
    msvc = Path(cc[-1]).stem.lower() == "cl"
    if sanitize and msvc:
        raise RuntimeError("--sanitize needs gcc or clang with UndefinedBehaviorSanitizer; MSVC cl does not support it.")
    if msvc:
        args = ["/nologo", "/O2", "/W4", "/D_CRT_SECURE_NO_WARNINGS"]
        args += [f"/I{ROOT / 'firmware/src'}", f"/I{ROOT / 'firmware/gen'}"]
        if host:
            args += ["/DTERKINSTEIN_HOST"]
        args += [f"/Fe:{output}", f"/Fo:{output.with_suffix('.obj')}", str(source)]
    else:
        args = ["-std=c99", "-O2", "-Wall", "-Wextra", "-Wno-unused-function"]
        args += ["-I", str(ROOT / "firmware/src"), "-I", str(ROOT / "firmware/gen")]
        if host:
            args += ["-DTERKINSTEIN_HOST"]
        if sanitize:
            args += ["-g", "-fsanitize=undefined", "-fno-sanitize-recover=undefined"]
        args += ["-o", str(output), str(source)]
    print(f"Build {source.relative_to(ROOT)} with {Path(cc[-1]).name}", flush=True)
    subprocess.run(cc + args, cwd=ROOT, check=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC"), help="C compiler executable or command (default: CC, then cc/clang/gcc/cl)")
    parser.add_argument("--script", type=Path, help="host event script; default is a 12-second C minor pad")
    parser.add_argument("--out", type=Path, default=ROOT / "build/host/terkinstein", help="output prefix for WAV and screens")
    parser.add_argument("--fps", type=int, default=0, help="screen movie frame rate 1..120, or 0 for three snapshots")
    parser.add_argument("--test", action="store_true", help="compile and run DSP regressions before the preview")
    parser.add_argument("--test-only", action="store_true", help="run DSP regressions without rendering the preview")
    parser.add_argument("--sanitize", action="store_true", help="enable UndefinedBehaviorSanitizer (gcc/clang)")
    parser.add_argument("--keep-ppm", action="store_true", help="keep PPM files after PNG conversion")
    args = parser.parse_args()
    if not 0 <= args.fps <= 120:
        parser.error("--fps must be 0..120")
    if args.script and not args.script.is_file():
        parser.error(f"script does not exist: {args.script}")
    if args.fps and not args.script:
        # Host positional arguments need a script when FPS is provided.
        args.script = ROOT / "tests/scripts/minor-pad.txt"
    prefix = args.out.resolve()
    prefix.parent.mkdir(parents=True, exist_ok=True)
    executable = prefix.parent / ("terkinstein.exe" if os.name == "nt" else "terkinstein")
    try:
        cc = compiler_command(args.cc)
        if args.test or args.test_only:
            test_exe = prefix.parent / ("test-engine.exe" if os.name == "nt" else "test-engine")
            build(cc, ROOT / "tests/test_engine.c", test_exe, host=False, sanitize=args.sanitize)
            subprocess.run([str(test_exe)], cwd=ROOT, check=True)
        if args.test_only:
            return 0
        try:
            from PIL import Image
        except ImportError as exc:
            raise RuntimeError("PNG previews need Pillow: python -m pip install pillow") from exc
        build(cc, ROOT / "firmware/src/terkinstein.c", executable, host=True, sanitize=args.sanitize)
        command = [str(executable), str(prefix)]
        if args.script:
            command += [str(args.script.resolve()), str(args.fps)]
        subprocess.run(command, cwd=ROOT, check=True)
        for ppm in sorted(prefix.parent.glob(prefix.name + "-*.ppm")):
            with Image.open(ppm) as screen:
                screen.resize((480, 480), Image.Resampling.NEAREST).save(ppm.with_suffix(".png"))
            if not args.keep_ppm:
                ppm.unlink()
            print(f"Screen: {ppm.with_suffix('.png')}")
        print(f"Audio: {prefix}.wav")
        return 0
    except (RuntimeError, OSError, subprocess.CalledProcessError) as exc:
        print(f"preview: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
