"""Tests for tools/stack2mesh.py: sprite stack -> MESH model (.bmm)."""
import os
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import stack2mesh as s2m  # noqa: E402

FORGE = os.path.join(ROOT, ".claude", "skills", "bm-forge", "scripts", "bm_forge.py")
RED, BLUE, GREEN = (255, 0, 0), (0, 0, 255), (0, 255, 0)
CLEAR = (0, 0, 0, 0)


def png_rgba(w, h, px):
    """px[y][x] = (r, g, b) or (r, g, b, a) -> PNG bytes, RGBA, filter 0"""
    raw = b""
    for row in px:
        raw += b"\0" + b"".join(bytes(p if len(p) == 4 else p + (255,)) for p in row)

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def stack_pixels(slices):
    """slices (lists of rows of RGB or None) -> sheet rows, side by side"""
    n, h, w = len(slices), len(slices[0]), len(slices[0][0])
    return [[(slices[k][j][i] or CLEAR) for k in range(n) for i in range(w)] for j in range(h)]


def synthetic():
    """3 slices of 4x4: 25 voxels; a red 4x4 slab, then a 2x2 red column over it, one blue voxel on slice 2"""
    def empty():
        return [[None] * 4 for _ in range(4)]
    s0 = [[RED] * 4 for _ in range(4)]
    s1, s2 = empty(), empty()
    for s in (s1, s2):
        for j in (1, 2):
            for i in (1, 2):
                s[j][i] = RED
    s1[0][0] = BLUE
    return [s0, s1, s2]


def as_colours(slices):
    return [[[None if c is None else c for c in row] for row in sl] for sl in slices]


def rgb(t):
    return t[0] << 16 | t[1] << 8 | t[2]


class Stack2MeshTest(unittest.TestCase):
    def setUp(self):
        self.src = synthetic()
        self.slices = [[[None if c is None else rgb(c) for c in row] for row in sl] for sl in self.src]

    def test_png_and_cut(self):
        data = png_rgba(12, 4, stack_pixels(self.src))
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "s.png")
            with open(p, "wb") as f:
                f.write(data)
            w, h, px = s2m.read_png(p)
        self.assertEqual((w, h), (12, 4))
        got = s2m.cut_slices(px, w, h, 4, 4, 3)
        self.assertEqual(got, self.slices)
        self.assertEqual(s2m.count_voxels(got), 16 + 5 + 4)

    def test_vertical_layout(self):
        sheet = [row for sl in self.src for row in [[(c or CLEAR) for c in r] for r in sl]]
        h = len(sheet)
        px = [[(rgb(c[:3]), len(c) == 3) for c in row] for row in sheet]
        got = s2m.cut_slices(px, 4, h, 4, 4, 3, direction="v")
        self.assertEqual(got, self.slices)

    def test_sheet_too_small(self):
        px = [[(0, False)] * 8 for _ in range(4)]
        with self.assertRaises(ValueError):
            s2m.cut_slices(px, 8, 4, 4, 4, 3)

    def test_greedy_box_count(self):
        boxes = s2m.merge_boxes(self.slices)
        # slab 4x4 on slice 0; the 2x2 red column over slices 1-2 is ONE box; one blue voxel
        self.assertEqual(len(boxes), 3)
        self.assertEqual(sum(b[2] * b[3] * (b[5] - b[4]) for b in boxes), 25)
        tall = [b for b in boxes if b[5] - b[4] == 2]
        self.assertEqual(len(tall), 1)
        self.assertEqual(tall[0][:4], (1, 1, 2, 2))

    def test_checker_does_not_merge(self):
        sl = [[rgb(RED) if (i + j) % 2 else rgb(BLUE) for i in range(4)] for j in range(4)]
        self.assertEqual(len(s2m.merge_boxes([sl, sl])), 16)   # 16 columns of 2 slices

    def test_model_counts_and_round_trip(self):
        model, boxes = s2m.build(self.slices, "synth")
        self.assertEqual(len(model.verts), 3 * 8)
        self.assertEqual(len(model.faces), 3 * 12)
        self.assertEqual(s2m.mesh_to_slices(model, 4, 4, 3), self.slices)

    def test_round_trip_through_file_with_step(self):
        sys.path.insert(0, os.path.join(ROOT, ".claude", "skills", "bm-forge"))
        from bm_core import container
        model, _ = s2m.build(self.slices, "synth", step=2.0)
        with tempfile.TemporaryDirectory() as d:
            out = os.path.join(d, "synth.bmm")
            s2m.write_bmm(out, model)
            got = container.read_file(out).mesh().models[0]
        self.assertEqual(max(v[1] for v in got.verts), 6.0)       # 3 slices x step 2
        self.assertEqual(s2m.mesh_to_slices(got, 4, 4, 3, 2.0), self.slices)

    def test_too_big_reports(self):
        sl = [[rgb((i * 7 % 256, j * 9 % 256, 1 + (i + j) % 200)) for i in range(32)] for j in range(32)]
        with self.assertRaises(ValueError) as cm:
            s2m.build([sl, sl], "big")
        self.assertIn("limit is 512 boxes", str(cm.exception))
        model, boxes = s2m.build([sl, sl], "big", kernel_limits=True)   # 1024 boxes: fits 65535
        self.assertEqual(len(boxes), 1024)

    def test_max_tris(self):
        with self.assertRaises(ValueError):
            s2m.build(self.slices, "synth", max_tris=35)

    def test_cli_and_forge_check(self):
        with tempfile.TemporaryDirectory() as d:
            png, out = os.path.join(d, "synth.png"), os.path.join(d, "synth.bmm")
            with open(png, "wb") as f:
                f.write(png_rgba(12, 4, stack_pixels(self.src)))
            r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "stack2mesh.py"), png, "-o", out,
                                "--size", "4", "4", "-n", "3"], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertIn("25 voxels, 3 boxes, 24 vertices, 36 triangles", r.stdout)
            c = subprocess.run([sys.executable, FORGE, "check", out, "--no-sync"], capture_output=True, text=True)
            self.assertEqual(c.returncode, 0, c.stdout + c.stderr)
            bad = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "stack2mesh.py"), png, "-o", out,
                                  "--size", "4", "4", "-n", "5"], capture_output=True, text=True)
            self.assertEqual(bad.returncode, 2)


if __name__ == "__main__":
    unittest.main()
