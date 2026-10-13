"""CLI preview and doctor subcommands (scripts/bm_forge.py preview, doctor)."""
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

import _common as C

PNG_SIG = b"\x89PNG\r\n\x1a\n"


class CliPreview(unittest.TestCase):
    """Test the bm-forge preview and doctor subcommands."""

    @classmethod
    def setUpClass(cls):
        """a models file built with the CLI chain mesh -> skel -> anim (humanoid + ground, walk clip),
        and a mesh-only file (no rig) of the humanoid"""
        cls.tmp = tempfile.mkdtemp()
        ex = os.path.join(C.SKILL, "examples")
        ground = os.path.join(ex, "game_demo", "ground_boxes.json")
        m, s, a, only = (os.path.join(cls.tmp, n) for n in ("m.bm", "s.bm", "models.bm", "only.bm"))
        steps = [
            ["mesh", os.path.join(ex, "humanoid_boxes.json"), ground, "-o", m],
            ["skel", os.path.join(ex, "humanoid.json"), "--in", m, "-o", s],
            ["anim"] + [os.path.join(ex, n + ".json") for n in ("idle", "walk", "jump")] + ["--in", s, "-o", a],
            ["mesh", os.path.join(ex, "humanoid_boxes.json"), "-o", only],
        ]
        for argv in steps:
            code, out, err = C.run_cli(argv)
            if code != 0:
                raise AssertionError(f"building the fixture {argv[0]} failed: {out}{err}")
        cls.models = a
        cls.only = only

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def _path(self, name):
        return os.path.join(self.tmp, name)

    def test_preview_still_png(self):
        """a still PNG at --size 64x64: PNG signature and width 64 read from IHDR"""
        out = self._path("still.png")
        code, stdout, stderr = C.run_cli(["preview", self.models, "--size", "64x64", "-o", out])
        self.assertEqual(code, 0, stdout + stderr)
        self.assertIn("wrote", stdout)
        with open(out, "rb") as f:
            data = f.read()
        self.assertEqual(data[:8], PNG_SIG)
        w, h = struct.unpack(">II", data[16:24])          # IHDR width, height
        self.assertEqual((w, h), (64, 64))

    def test_preview_clip_gif(self):
        """--clip walk -o x.gif: a GIF89a"""
        out = self._path("walk.gif")
        code, stdout, stderr = C.run_cli(["preview", self.models, "--clip", "walk", "-o", out])
        self.assertEqual(code, 0, stdout + stderr)
        with open(out, "rb") as f:
            self.assertTrue(f.read().startswith(b"GIF89a"))

    def test_preview_clip_sheet_png(self):
        """--clip walk -o sheet.png: a contact sheet PNG, exit 0"""
        out = self._path("sheet.png")
        code, stdout, stderr = C.run_cli(["preview", self.models, "--clip", "walk", "-o", out])
        self.assertEqual(code, 0, stdout + stderr)
        with open(out, "rb") as f:
            self.assertEqual(f.read(8), PNG_SIG)

    def test_preview_turntable(self):
        """--turntable 4 --size 32x32 -o t.gif: exit 0 and a GIF89a"""
        out = self._path("t.gif")
        code, stdout, stderr = C.run_cli(["preview", self.models, "--turntable", "4", "--size", "32x32", "-o", out])
        self.assertEqual(code, 0, stdout + stderr)
        with open(out, "rb") as f:
            self.assertTrue(f.read().startswith(b"GIF89a"))

    def test_preview_mask_then_compare(self):
        """--mask writes the silhouette PNG; --compare on it: exit 0 and front=1.0000 (same model, same view)"""
        mask = self._path("mask.png")
        code, stdout, stderr = C.run_cli(["preview", self.models, "--mask", "-o", mask])
        self.assertEqual(code, 0, stdout + stderr)
        self.assertTrue(os.path.exists(mask))
        code, stdout, stderr = C.run_cli(["preview", self.models, "--compare", mask,
                                          "-o", self._path("cmp.png")])
        self.assertEqual(code, 0, stdout + stderr)
        self.assertIn("front=1.0000", stdout)

    def test_preview_bad_ext(self):
        """-o x.jpg is a usage error (exit 2) and nothing is written"""
        out = self._path("x.jpg")
        code, stdout, stderr = C.run_cli(["preview", self.models, "-o", out])
        self.assertEqual(code, 2, stdout + stderr)
        self.assertFalse(os.path.exists(out))

    def test_preview_clip_without_rig(self):
        """--clip on a mesh-only file: no rig for the model, exit 1"""
        code, stdout, stderr = C.run_cli(["preview", self.only, "--clip", "walk", "-o", self._path("nr.gif")])
        self.assertEqual(code, 1, stdout + stderr)
        self.assertIn("no rig", stderr)

    def test_doctor(self):
        """doctor: exit 0 and the report lines; no optional module is imported by it"""
        before = "trimesh" in sys.modules
        code, stdout, stderr = C.run_cli(["doctor"])
        self.assertEqual(code, 0, stdout + stderr)
        for text in ("blender:", "module numpy:", "bmhost-bin:", "core needs none"):
            self.assertIn(text, stdout)
        self.assertEqual("trimesh" in sys.modules, before)

    def test_doctor_subprocess(self):
        """doctor as a separate process: exit 0"""
        r = subprocess.run([sys.executable, C.CLI, "doctor"], capture_output=True, text=True,
                           encoding="utf-8", errors="replace", timeout=120)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)


if __name__ == "__main__":
    unittest.main()
