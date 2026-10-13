"""Test similarity metrics: PNG reader, masks, IoU."""
import struct
import sys
import unittest
import zlib

import _common as C
from bm_core import ValidationError, imageio, mesh as meshmod, similarity


class Similarity(unittest.TestCase):
    """PNG reader, mask creation, IoU metrics."""

    def test_png_roundtrip_rgb(self):
        """3x2 RGB image through png_bytes and read_png: pixels equal, alpha None."""
        # Create a simple 3x2 RGB image
        pixels = [0xFF0000, 0x00FF00, 0x0000FF, 0xFFFFFF, 0x000000, 0x808080]
        img = imageio.Image(3, 2, pixels=pixels)

        # Encode to PNG
        png_data = imageio.png_bytes(img)

        # Decode from PNG
        decoded, alpha = similarity.read_png(png_data)

        self.assertEqual(decoded.w, 3)
        self.assertEqual(decoded.h, 2)
        self.assertEqual(decoded.pixels, pixels)
        self.assertIsNone(alpha)

    def test_png_rgba_filters(self):
        """3x3 RGBA PNG with row filters 0, 1, 4 decodes to expected pixels and alpha."""
        # Build a hand-crafted 3x3 RGBA PNG with specific filters
        w, h = 3, 3

        # Construct raw image data with filters
        # Filter 0 (None): no transformation
        row0_filter = 0
        row0_data = bytes([
            0xFF, 0x00, 0x00, 0xFF,  # Red with full alpha
            0x00, 0xFF, 0x00, 0xFF,  # Green with full alpha
            0x00, 0x00, 0xFF, 0xFF,  # Blue with full alpha
        ])

        # Filter 1 (Sub): each byte is difference from left
        row1_filter = 1
        row1_raw = [
            0xFF, 0x00, 0x00, 0xFF,  # First pixel
            0x01, 0xFF, 0x00, 0x00,  # Green - Red in R, 0 in G, 0 in B, 0 in A
            0xFF, 0x01, 0xFF, 0x00,  # Blue - Green
        ]
        row1_data = bytes(row1_raw)

        # Filter 4 (Paeth): differences using Paeth predictor
        row2_filter = 4
        row2_raw = [
            0xFF, 0xFF, 0xFF, 0x80,  # White with 0x80 alpha
            0x00, 0x00, 0x00, 0x00,  # Black with 0x00 alpha
            0x80, 0x80, 0x80, 0x7F,  # Grey with 0x7F alpha
        ]
        row2_data = bytes(row2_raw)

        # Construct the filtered image data
        raw_image = b''
        raw_image += bytes([row0_filter]) + row0_data
        raw_image += bytes([row1_filter]) + row1_data
        raw_image += bytes([row2_filter]) + row2_data

        # Compress
        compressed = zlib.compress(raw_image, 9)

        # Build PNG
        png = b'\x89PNG\r\n\x1a\n'

        # IHDR chunk
        ihdr_data = struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)
        ihdr_chunk = (struct.pack(">I", len(ihdr_data)) + b'IHDR' + ihdr_data +
                      struct.pack(">I", zlib.crc32(b'IHDR' + ihdr_data) & 0xFFFFFFFF))

        # IDAT chunk
        idat_chunk = (struct.pack(">I", len(compressed)) + b'IDAT' + compressed +
                      struct.pack(">I", zlib.crc32(b'IDAT' + compressed) & 0xFFFFFFFF))

        # IEND chunk
        iend_chunk = (struct.pack(">I", 0) + b'IEND' +
                      struct.pack(">I", zlib.crc32(b'IEND') & 0xFFFFFFFF))

        png_data = png + ihdr_chunk + idat_chunk + iend_chunk

        # Decode
        decoded, alpha = similarity.read_png(png_data)

        self.assertEqual(decoded.w, 3)
        self.assertEqual(decoded.h, 3)
        self.assertEqual(len(decoded.pixels), 9)
        self.assertEqual(len(alpha), 9)

        # Check row 0 (no filter): should be clear red, green, blue
        self.assertEqual(decoded.pixels[0], 0xFF0000)
        self.assertEqual(decoded.pixels[1], 0x00FF00)
        self.assertEqual(decoded.pixels[2], 0x0000FF)
        self.assertEqual(alpha[0], 0xFF)
        self.assertEqual(alpha[1], 0xFF)
        self.assertEqual(alpha[2], 0xFF)

    def test_png_rejects(self):
        """PNG with unsupported bit depth, interlace, or too large dimensions raises ValidationError."""
        # Test bit depth 16
        png = b'\x89PNG\r\n\x1a\n'
        ihdr_data = struct.pack(">IIBBBBB", 10, 10, 16, 2, 0, 0, 0)  # bit_depth=16
        ihdr_chunk = (struct.pack(">I", len(ihdr_data)) + b'IHDR' + ihdr_data +
                      struct.pack(">I", zlib.crc32(b'IHDR' + ihdr_data) & 0xFFFFFFFF))
        iend_chunk = (struct.pack(">I", 0) + b'IEND' +
                      struct.pack(">I", zlib.crc32(b'IEND') & 0xFFFFFFFF))
        png_data = png + ihdr_chunk + iend_chunk

        with self.assertRaises(ValidationError) as ctx:
            similarity.read_png(png_data)
        self.assertIn("bit depth", str(ctx.exception))

        # Test interlace 1
        png = b'\x89PNG\r\n\x1a\n'
        ihdr_data = struct.pack(">IIBBBBB", 10, 10, 8, 2, 0, 0, 1)  # interlace=1
        ihdr_chunk = (struct.pack(">I", len(ihdr_data)) + b'IHDR' + ihdr_data +
                      struct.pack(">I", zlib.crc32(b'IHDR' + ihdr_data) & 0xFFFFFFFF))
        iend_chunk = (struct.pack(">I", 0) + b'IEND' +
                      struct.pack(">I", zlib.crc32(b'IEND') & 0xFFFFFFFF))
        png_data = png + ihdr_chunk + iend_chunk

        with self.assertRaises(ValidationError) as ctx:
            similarity.read_png(png_data)
        self.assertIn("interlace", str(ctx.exception))

        # Test image too large
        png = b'\x89PNG\r\n\x1a\n'
        ihdr_data = struct.pack(">IIBBBBB", 600, 1, 8, 2, 0, 0, 0)  # 600 > REF_MAX
        ihdr_chunk = (struct.pack(">I", len(ihdr_data)) + b'IHDR' + ihdr_data +
                      struct.pack(">I", zlib.crc32(b'IHDR' + ihdr_data) & 0xFFFFFFFF))
        iend_chunk = (struct.pack(">I", 0) + b'IEND' +
                      struct.pack(">I", zlib.crc32(b'IEND') & 0xFFFFFFFF))
        png_data = png + ihdr_chunk + iend_chunk

        with self.assertRaises(ValidationError) as ctx:
            similarity.read_png(png_data)
        self.assertIn("larger than", str(ctx.exception))

    def test_iou_identical(self):
        """Humanoid vs itself: mean IoU == 1.0."""
        mesh, anim = C.forge_assets()
        humanoid = mesh.get("humanoid")

        result = similarity.compare_models(humanoid, humanoid)
        self.assertEqual(result["mean"], 1.0)
        for view in ["front", "side", "top"]:
            self.assertEqual(result[view], 1.0)

    def test_iou_scaled(self):
        """Humanoid with every vertex x2: mean IoU >= 0.99."""
        mesh, anim = C.forge_assets()
        humanoid = mesh.get("humanoid")

        # Create a scaled copy using the Model dataclass API
        scaled_verts = [[v[0] * 2, v[1] * 2, v[2] * 2] for v in humanoid.verts]
        scaled = meshmod.Model(
            name=humanoid.name,
            verts=scaled_verts,
            faces=humanoid.faces,
            flags=humanoid.flags,
        )

        result = similarity.compare_models(humanoid, scaled)
        self.assertGreaterEqual(result["mean"], 0.99)

    def test_iou_different(self):
        """Humanoid vs ground: mean IoU < 0.5."""
        mesh, anim = C.forge_assets()
        humanoid = mesh.get("humanoid")
        ground = mesh.get("ground")

        result = similarity.compare_models(humanoid, ground)
        self.assertLess(result["mean"], 0.5)

    def test_png_reference_roundtrip(self):
        """compare_png(humanoid, png_bytes(mask_image(model_mask(...)))) == 1.0."""
        mesh, anim = C.forge_assets()
        humanoid = mesh.get("humanoid")

        # Generate reference mask at 64x64
        mask = similarity.model_mask(humanoid, "front", size=(64, 64))

        # Convert to image and then to PNG
        ref_image = similarity.mask_image(mask, 64, 64)
        png_data = imageio.png_bytes(ref_image)

        # Compare the model to the PNG
        iou_val = similarity.compare_png(humanoid, png_data, "front")
        self.assertEqual(iou_val, 1.0)

    def test_iou_size_mismatch(self):
        """IoU with different mask sizes raises ValidationError."""
        mask_a = [True, False, True]
        mask_b = [True, False]

        with self.assertRaises(ValidationError) as ctx:
            similarity.iou(mask_a, mask_b)
        self.assertIn("differ in size", str(ctx.exception))


if __name__ == "__main__":
    unittest.main()
