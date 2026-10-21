#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Pack the approved three-frame homunculus in one shared RGB565/4-bit palette.

The original PNGs are never modified. Transparency is composited on black;
the common visible rectangle is stored once per frame, two pixels per byte.
Palette construction and nearest-color mapping are deterministic integer work.
The generated preview PNGs show the decoded RGB565 colors, not source colors.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import io
import json
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "assets/crepuscular-homunculus/bubbling-v1"
SIZE = 240
NAMES = ("A", "B", "C")


def pixels(image: Image.Image):
    """Pillow 12+ avoids deprecated getdata; keep older host Pillow compatible."""
    return image.get_flattened_data() if hasattr(image, "get_flattened_data") else image.getdata()


def rgb565(rgb: tuple[int, int, int]) -> int:
    r, g, b = rgb
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def expand565(value: int) -> tuple[int, int, int]:
    r, g, b = value >> 11, (value >> 5) & 63, value & 31
    return ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2))


def shared_palette(images: list[Image.Image]) -> list[int]:
    """Weighted median-cut over visible pixels; reserve index zero for black.

    Sorted RGB keys and explicit tie-breaks avoid reliance on a library's
    changing quantizer or dithering. Colors have their source pixel frequency.
    """
    counts: Counter[tuple[int, int, int]] = Counter()
    for image in images:
        counts.update(pixels(image))
    counts.pop((0, 0, 0), None)
    if not counts:
        raise ValueError("The three input frames must contain visible artwork.")
    buckets = [sorted(counts)]

    def dimensions(bucket: list[tuple[int, int, int]]) -> tuple[int, int]:
        spans = [max(c[n] for c in bucket) - min(c[n] for c in bucket) for n in range(3)]
        channel = max(range(3), key=lambda n: (spans[n], -n))
        return spans[channel], channel

    while len(buckets) < 15:
        candidates = [i for i, bucket in enumerate(buckets) if len(bucket) > 1]
        if not candidates:
            break
        selected = max(candidates, key=lambda i: (
            dimensions(buckets[i])[0] * sum(counts[c] for c in buckets[i]), -i))
        bucket = buckets.pop(selected)
        channel = dimensions(bucket)[1]
        bucket.sort(key=lambda c: (c[channel], c))
        total = sum(counts[c] for c in bucket)
        partial = 0
        split = 1
        for i, color in enumerate(bucket[:-1], 1):
            partial += counts[color]
            split = i
            if partial * 2 >= total:
                break
        buckets.extend((bucket[:split], bucket[split:]))

    colors = {0}
    for bucket in buckets:
        total = sum(counts[c] for c in bucket)
        average = tuple((sum(c[n] * counts[c] for c in bucket) + total // 2) // total
                        for n in range(3))
        colors.add(rgb565(average))
    palette = [0] + sorted(colors - {0}, key=lambda c: (sum(expand565(c)), c))
    return palette + [0] * (16 - len(palette))


def pack_indices(indices: bytes, width: int) -> bytes:
    if width <= 0 or width % 2 or len(indices) % width:
        raise ValueError("Packed rows require a positive even width and complete rows.")
    if any(value > 15 for value in indices):
        raise ValueError("Palette indices must fit in four bits.")
    return bytes((indices[i] << 4) | indices[i + 1] for i in range(0, len(indices), 2))


def decode_frame(packed: bytes, palette: list[int], bounds: tuple[int, int, int, int]) -> Image.Image:
    left, top, right, bottom = bounds
    width, height = right - left, bottom - top
    if len(packed) != width * height // 2:
        raise ValueError("Frame byte count does not match its bounds.")
    colors = [expand565(color) for color in palette]
    rgb = bytes(channel for byte in packed for index in (byte >> 4, byte & 15)
                for channel in colors[index])
    preview = Image.new("RGB", (SIZE, SIZE), (0, 0, 0))
    preview.paste(Image.frombytes("RGB", (width, height), rgb), (left, top))
    return preview


def load_images(input_dir: Path) -> tuple[list[Image.Image], list[dict]]:
    images, sources = [], []
    for name in NAMES:
        path = input_dir / f"homunculus_{name}.png"
        raw = path.read_bytes()
        with Image.open(io.BytesIO(raw)) as source:
            if source.size != (SIZE, SIZE):
                raise ValueError(f"{path.name}: expected {SIZE} x {SIZE}; found {source.size}.")
            background = Image.new("RGBA", source.size, (0, 0, 0, 255))
            background.alpha_composite(source.convert("RGBA"))
            images.append(background.convert("RGB"))
        sources.append({"file": path.name, "sha256": hashlib.sha256(raw).hexdigest()})
    return images, sources


def generate(input_dir: Path, header: Path, output_dir: Path) -> tuple[dict[Path, bytes], dict]:
    images, sources = load_images(input_dir)
    boxes = [image.getbbox() for image in images]
    if any(box is None for box in boxes):
        raise ValueError("Every frame must contain visible artwork.")
    left = min(box[0] for box in boxes)
    top = min(box[1] for box in boxes)
    right = max(box[2] for box in boxes)
    bottom = max(box[3] for box in boxes)
    # Extend by at most one black column so every row starts on a byte boundary.
    if (right - left) % 2:
        if right < SIZE:
            right += 1
        else:
            left -= 1
    bounds = (left, top, right, bottom)
    width, height = right - left, bottom - top
    palette = shared_palette(images)
    expanded = [expand565(color) for color in palette]
    color_map = {}
    for color in sorted(set(color for image in images for color in pixels(image))):
        color_map[color] = min(range(16), key=lambda index: (
            sum((a - b) ** 2 for a, b in zip(color, expanded[index])), index))
    frames = [pack_indices(bytes(color_map[c] for c in pixels(image.crop(bounds))), width)
              for image in images]
    frame_bytes = width * height // 2
    outputs = {}
    previews = []
    for name, packed in zip(NAMES, frames):
        preview = decode_frame(packed, palette, bounds)
        png = io.BytesIO()
        preview.save(png, format="PNG", compress_level=9)
        filename = f"homunculus_{name}_firmware.png"
        outputs[output_dir / filename] = png.getvalue()
        previews.append({"file": filename, "decoded_rgb_sha256": hashlib.sha256(preview.tobytes()).hexdigest(),
                         "packed_sha256": hashlib.sha256(packed).hexdigest()})

    lines = [
        "/* SPDX-License-Identifier: GPL-3.0-only */",
        "/* Generated by tools/make_homunculus.py; do not edit.",
        " * Approved Crepuscular Homunculus artwork; source PNGs are unchanged.",
        " * One common crop, row-major pixels, high nibble is the left pixel.",
        " * Palette is unswapped RGB565. Convert to LCD byte order when drawing. */",
        "#ifndef TERKINSTEIN_HOMUNCULUS_H",
        "#define TERKINSTEIN_HOMUNCULUS_H",
        "#include <stdint.h>",
        f"#define TK_HOMUNCULUS_X {left}",
        f"#define TK_HOMUNCULUS_Y {top}",
        f"#define TK_HOMUNCULUS_WIDTH {width}",
        f"#define TK_HOMUNCULUS_HEIGHT {height}",
        "#define TK_HOMUNCULUS_FRAME_COUNT 3",
        f"#define TK_HOMUNCULUS_ROW_BYTES {width // 2}",
        f"#define TK_HOMUNCULUS_FRAME_BYTES {frame_bytes}",
        "static const uint16_t tk_homunculus_palette[16] = {",
        "    " + ", ".join(f"0x{color:04x}" for color in palette) + ",",
        "};",
        "static const uint8_t tk_homunculus_frames[TK_HOMUNCULUS_FRAME_COUNT][TK_HOMUNCULUS_FRAME_BYTES] = {",
    ]
    for name, packed in zip(NAMES, frames):
        lines.append(f"    {{ /* Frame {name} */")
        for offset in range(0, len(packed), 16):
            lines.append("        " + ", ".join(f"0x{value:02x}" for value in packed[offset:offset + 16]) + ",")
        lines.append("    },")
    lines.extend(["};", "#endif", ""])
    header_bytes = "\n".join(lines).encode("utf-8")
    outputs[header] = header_bytes
    manifest = {
        "format": "FM-1 shared-palette 4-bit RGB565 animation v1",
        "generator": "tools/make_homunculus.py",
        "canvas": {"width": SIZE, "height": SIZE, "background": "black"},
        "crop": {"x": left, "y": top, "width": width, "height": height},
        "palette_method": "deterministic weighted median-cut, 15 visible colors plus black; no dithering",
        "palette_rgb565": [f"{color:04x}" for color in palette],
        "palette_rgb888_preview": [list(color) for color in expanded],
        "packing": "row-major, high nibble first, black index 0, RGB565 unswapped",
        "row_bytes": width // 2,
        "frame_bytes": frame_bytes,
        "frame_count": 3,
        "pixel_bytes": frame_bytes * 3,
        "palette_bytes": 32,
        "total_asset_bytes": frame_bytes * 3 + 32,
        "playback": {"frames": [0, 1, 2, 1], "frame_duration_ms": 200},
        "sources": sources,
        "previews": previews,
        "header_sha256": hashlib.sha256(header_bytes).hexdigest(),
        "note": "Only offline color conversion and common cropping; source artwork and layout are unchanged. "
                "Previews show exact decoded RGB565 palette colors on black. No hardware qualification.",
    }
    outputs[output_dir / "firmware-manifest.json"] = (json.dumps(manifest, indent=2) + "\n").encode("utf-8")
    return outputs, manifest


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input-dir", type=Path, default=ASSETS)
    parser.add_argument("--header", type=Path, default=ROOT / "firmware/gen/terkinstein_homunculus.h")
    parser.add_argument("--output-dir", type=Path, default=ASSETS)
    parser.add_argument("--check", action="store_true", help="Verify generated files without writing anything.")
    args = parser.parse_args()
    try:
        outputs, manifest = generate(args.input_dir, args.header, args.output_dir)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    if args.check:
        stale = [str(path) for path, expected in outputs.items()
                 if not path.is_file() or path.read_bytes() != expected]
        if stale:
            parser.exit(1, "Missing or stale homunculus assets:\n" + "\n".join(stale) + "\n")
    else:
        for path, data in outputs.items():
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
    crop = manifest["crop"]
    print(f"Homunculus: 3 frames, {crop['width']} x {crop['height']} crop at ({crop['x']}, {crop['y']}), "
          f"{manifest['frame_bytes']:,} bytes/frame + 32 palette bytes = {manifest['total_asset_bytes']:,} flash bytes")
    print("Assets are current." if args.check else "Generated header, decoded firmware previews and manifest.")


if __name__ == "__main__":
    main()
