#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Verify firmware asset packing against independent pixel and alpha examples."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("make_homunculus", ROOT / "tools/make_homunculus.py")
asset = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(asset)


class HomunculusAssets(unittest.TestCase):
    def test_nibble_order_rgb565_and_canvas_location(self):
        indices = bytes([1, 2, 3, 0, 0, 3, 2, 1])
        self.assertEqual(asset.pack_indices(indices, 4), bytes([0x12, 0x30, 0x03, 0x21]))
        palette = [0, 0xf800, 0x07e0, 0x001f] + [0] * 12
        image = asset.decode_frame(bytes([0x12, 0x30, 0x03, 0x21]), palette, (10, 11, 14, 13))
        self.assertEqual(image.getpixel((10, 11)), (255, 0, 0))
        self.assertEqual(image.getpixel((11, 11)), (0, 255, 0))
        self.assertEqual(image.getpixel((12, 11)), (0, 0, 255))
        self.assertEqual(image.getpixel((13, 12)), (255, 0, 0))
        self.assertEqual(image.getpixel((9, 11)), (0, 0, 0))
        self.assertEqual(asset.rgb565((255, 255, 255)), 0xffff)

    def test_common_crop_black_alpha_and_reproducible_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            for number, name in enumerate(asset.NAMES):
                frame = Image.new("RGBA", (240, 240), (255, 255, 255, 0))
                frame.putpixel((10 + number, 20), (255, 0, 0, 255))
                # Opaque black is not visible artwork and does not enlarge the crop.
                frame.putpixel((239, 239), (0, 0, 0, 255))
                frame.save(base / f"homunculus_{name}.png")
            header = base / "out.h"
            outputs, manifest = asset.generate(base, header, base / "out")
            second, second_manifest = asset.generate(base, header, base / "out")
            self.assertEqual(outputs, second)
            self.assertEqual(manifest, second_manifest)
            self.assertEqual(manifest["crop"], {"x": 10, "y": 20, "width": 4, "height": 1})
            self.assertEqual(manifest["palette_rgb565"][0], "0000")
            self.assertEqual(manifest["frame_bytes"], 2)
            self.assertFalse(header.exists())
            self.assertEqual(manifest["total_asset_bytes"], 38)

    def test_reject_unsupported_inputs_and_empty_frames(self):
        with self.assertRaises(ValueError):
            asset.pack_indices(bytes([0, 1, 2]), 3)
        with self.assertRaises(ValueError):
            asset.pack_indices(bytes([0, 16]), 2)
        with self.assertRaises(ValueError):
            asset.shared_palette([Image.new("RGB", (240, 240), "black")])


if __name__ == "__main__":
    unittest.main()
