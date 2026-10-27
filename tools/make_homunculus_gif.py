#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Create website loops from approved frames, retaining a shared 256-color palette.

Only composition, palette conversion and nearest-neighbor scaling are applied.
The source artwork is untouched; these website files are not firmware assets.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
from pathlib import Path

from PIL import Image

from make_homunculus import ASSETS, NAMES, SIZE, load_images


def generate(input_dir: Path, output_dir: Path) -> tuple[dict[Path, bytes], dict]:
    images, sources = load_images(input_dir)
    montage = Image.new("RGB", (SIZE, SIZE * len(images)), "black")
    for index, image in enumerate(images):
        montage.paste(image, (0, SIZE * index))
    shared = montage.quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    frames = [image.quantize(palette=shared, dither=Image.Dither.NONE) for image in images]
    sequence = [0, 1, 2, 1]
    outputs, records = {}, []
    for size in (240, 480):
        resized = [frames[index].resize((size, size), Image.Resampling.NEAREST) for index in sequence]
        stream = io.BytesIO()
        resized[0].save(stream, format="GIF", save_all=True, append_images=resized[1:],
                        duration=200, loop=0, disposal=1, optimize=False)
        data = stream.getvalue()
        # Confirm actual encoded playback: repeated B must not collapse duration.
        with Image.open(io.BytesIO(data)) as verification:
            if verification.n_frames != 4 or verification.size != (size, size) or verification.info.get("loop") != 0:
                raise ValueError("GIF encoding changed the required playback structure.")
            for index, expected in enumerate(resized):
                verification.seek(index)
                if verification.info.get("duration") != 200:
                    raise ValueError("GIF encoding changed a frame duration.")
                if verification.convert("RGB").tobytes() != expected.convert("RGB").tobytes():
                    raise ValueError("GIF encoding changed decoded frame pixels.")
        filename = f"crepuscular-homunculus-bubbling-{size}.gif"
        outputs[output_dir / filename] = data
        records.append({"file": filename, "size": [size, size], "bytes": len(data),
                        "sha256": hashlib.sha256(data).hexdigest(), "decoded_frames_verified": 4})
    manifest = {
        "generator": "tools/make_homunculus_gif.py",
        "sources": sources,
        "background": "black",
        "sequence": [NAMES[index] for index in sequence],
        "frame_duration_ms": 200,
        "loop_duration_ms": 800,
        "loop": "forever",
        "palette": "shared 256-color median-cut palette from the original full-color source frames; no dithering",
        "scale": "240px original canvas; 480px nearest-neighbor 2x enlargement",
        "files": records,
        "note": "Website artwork only. No logo, title, firmware installation or source-image edits.",
    }
    outputs[output_dir / "website-gif-manifest.json"] = (json.dumps(manifest, indent=2) + "\n").encode("utf-8")
    return outputs, manifest


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input-dir", type=Path, default=ASSETS)
    parser.add_argument("--output-dir", type=Path, default=ASSETS)
    parser.add_argument("--check", action="store_true", help="Verify outputs without writing anything.")
    args = parser.parse_args()
    try:
        outputs, manifest = generate(args.input_dir, args.output_dir)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    if args.check:
        stale = [str(path) for path, expected in outputs.items()
                 if not path.is_file() or path.read_bytes() != expected]
        if stale:
            parser.exit(1, "Missing or stale website GIFs:\n" + "\n".join(stale) + "\n")
    else:
        for path, data in outputs.items():
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
    for record in manifest["files"]:
        print(f"{record['file']}: {record['bytes']:,} bytes, 4 frames at 200ms, infinite loop")
    print("GIF outputs are current." if args.check else "Website GIFs generated and decoded playback verified.")


if __name__ == "__main__":
    main()
