"""CLI check subcommand: validation, limits, budget, drift and similarity (scripts/bm_forge.py check)."""
import json
import os
import tempfile
import unittest

import _common as C
import bm_core
from bm_core import container, export, similarity, shapes


def _write_forge(tmpdir, name="models.bm"):
    """the forge humanoid + ground models file (export_models, 320x180 bm); -> (path, Mesh)"""
    mesh, anim = C.forge_assets()
    path = os.path.join(tmpdir, name)
    export.export_models(path, mesh, anim, "Forge check", "test")
    return path, mesh


class CliCheck(unittest.TestCase):
    """Test the bm-forge check subcommand."""

    def test_check_village_ok(self):
        """a reference cartridge passes: exit 0 and OK on stdout"""
        code, out, err = C.run_cli(["check", C.path("village_models.bm"), "--no-sync"])
        self.assertEqual(code, 0, err)
        self.assertIn("OK", out)

    def test_check_goldens_bm(self):
        """two more bm goldens (animated figure, lit mesh) pass together: exit 0"""
        code, out, err = C.run_cli(["check", C.path("anim_figure.bm"), C.path("bmmesh_lit.bm"), "--no-sync"])
        self.assertEqual(code, 0, out + err)
        self.assertIn("OK", out)

    def test_check_luahost_bme_fails(self):
        """the luahost stand-in .bme has no project bit: read_file refuses it (exit 1)"""
        code, out, err = C.run_cli(["check", C.path("studio_blocks.bme"), "--no-sync"])
        self.assertEqual(code, 1, out + err)
        self.assertIn("project bit", out)

    def test_check_corrupt(self):
        """one byte flipped in the body breaks the CRC: exit 1 and CRC on stdout"""
        data = bytearray(C.load("village_models.bm"))
        data[200] ^= 0xFF
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "corrupt.bm")
            with open(path, "wb") as f:
                f.write(bytes(data))
            code, out, err = C.run_cli(["check", path, "--no-sync"])
        self.assertEqual(code, 1, out + err)
        self.assertIn("CRC", out)

    def test_check_missing(self):
        """a file that does not exist is a usage/IO error: exit 2"""
        code, out, err = C.run_cli(["check", C.path("no_such_file.bm"), "--no-sync"])
        self.assertEqual(code, 2)
        self.assertIn("not found", err)

    def test_check_budget_enforced(self):
        """--budget 10 is enforced (exit 1); without it the budget is information only (exit 0)"""
        with tempfile.TemporaryDirectory() as tmp:
            path, _ = _write_forge(tmp)
            code, out, err = C.run_cli(["check", path, "--budget", "10", "--no-sync"])
            self.assertEqual(code, 1, out + err)
            self.assertIn("FAIL budget", out)
            code, out, err = C.run_cli(["check", path, "--no-sync"])
            self.assertEqual(code, 0, out + err)
            self.assertIn("budget:", out)

    def test_check_kernel_limits(self):
        """5000 vertices pass only with --kernel-limits (tool limit 4096); 625 boxes of 8 vertices.
        shapes.boxes_model and export.models_cart validate with tool limits, so the Model is
        built with shapes.box and the cart with set_mesh(kernel_limits=True)."""
        verts, faces = [], []
        for i in range(625):
            vs, fs = shapes.box([float(i), 0.0, 0.0], [float(i) + 0.5, 0.5, 0.5], 0xFFFFFF, len(verts))
            verts += vs
            faces += fs
        big = bm_core.Model("big", verts, faces)
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "big.bm")
            cart = container.new_cart(export.MODELS_LUA, 320, 180, "Big", "test")
            cart.set_mesh(bm_core.Mesh([big]), kernel_limits=True)
            container.write_file(path, cart, kernel_limits=True)
            code, out, err = C.run_cli(["check", path, "--no-sync"])
            self.assertEqual(code, 1, out + err)
            self.assertIn("FAIL", out)
            code, out, err = C.run_cli(["check", path, "--kernel-limits", "--no-sync"])
            self.assertEqual(code, 0, out + err)
            self.assertIn("65535/65535", out)

    def test_check_reference_model(self):
        """a file against itself: mean=1.0000, exit 0; against a ground-only file: below --min-iou, exit 3"""
        with tempfile.TemporaryDirectory() as tmp:
            path, mesh = _write_forge(tmp)
            code, out, err = C.run_cli(["check", path, "--reference", path, "--no-sync"])
            self.assertEqual(code, 0, out + err)
            self.assertIn("mean=1.0000", out)

            ground_path = os.path.join(tmp, "ground.bm")
            export.export_models(ground_path, bm_core.Mesh([mesh.get("ground")]), None, "Ground", "test")
            code, out, err = C.run_cli(["check", path, "--reference", ground_path, "--ref-model", "ground",
                                        "--no-sync"])
            self.assertEqual(code, 3, out + err)
            self.assertIn("mean=", out)

    def test_check_reference_png(self):
        """a silhouette PNG of the same model (front view) passes: exit 0"""
        with tempfile.TemporaryDirectory() as tmp:
            path, mesh = _write_forge(tmp)
            mask = similarity.model_mask(mesh.models[0], view="front", size=(96, 96))
            png = os.path.join(tmp, "mask.png")
            similarity.mask_image(mask, 96, 96).save_png(png)
            code, out, err = C.run_cli(["check", path, "--reference", png, "--no-sync"])
            self.assertEqual(code, 0, out + err)
            self.assertIn("iou front=", out)

    def test_check_json(self):
        """--json prints only one object whose exit matches the code"""
        code, out, err = C.run_cli(["check", C.path("village_models.bm"), "--no-sync", "--json"])
        obj = json.loads(out)
        self.assertEqual(obj["exit"], code)
        self.assertTrue(obj["files"][0]["ok"])

    def test_check_sync(self):
        """the drift check runs by default: exit 0 (in sync) or 4 (drift, listed as DRIFT)"""
        code, out, err = C.run_cli(["check", C.path("village_models.bm")])
        self.assertIn(code, (0, 4), out + err)
        if code == 4:
            self.assertIn("DRIFT", out)


if __name__ == "__main__":
    unittest.main()
