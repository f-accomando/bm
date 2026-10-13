"""Export of models, animations and game cartridges: models.bm, .bmm (BMRES), .bm/.b16 with cover."""
import os
import tempfile
import unittest

import _common as C
import bm_core
from bm_core import ValidationError, export, container, sections as S, imageio, mesh as meshmod, anim as animmod


class ModelsCart(unittest.TestCase):
    def test_models_sections(self):
        """models_cart builds LUA (1), MESH (8), ANIM (9) sections."""
        mesh, anim = C.forge_assets()
        cart = export.models_cart(mesh, anim, "Test", "Author")
        self.assertEqual([s.type for s in cart.sections], [S.LUA, S.MESH, S.ANIM])

        # Verify mesh content
        m = cart.mesh()
        humanoid = m.get("humanoid")
        self.assertIsNotNone(humanoid)
        self.assertEqual(len(humanoid.faces), 156)

        # Verify anim clips
        a = cart.anim()
        self.assertEqual(len(a.rigs), 1)
        rig = a.rigs[0]
        self.assertEqual([c.name for c in rig.clips], ["idle", "walk", "jump"])

    def test_models_equals_mkbm_pack(self):
        """export_models bytes match mkbm.pack() with same params."""
        if not os.path.exists(os.path.join(C.REPO, "scripts", "mkbm.py")):
            self.skipTest("repo scripts not available")

        mkbm = C.import_script("mkbm")
        mesh, anim = C.forge_assets()

        # Build with export_models
        with tempfile.TemporaryDirectory() as tmpdir:
            path = os.path.join(tmpdir, "test.bm")
            data = export.export_models(path, mesh, anim, "Title", "Author")
            with open(path, "rb") as f:
                exp_data = f.read()

        # Build with mkbm.pack
        enc_mesh = meshmod.encode(mesh)
        enc_anim = animmod.encode(anim) if anim.rigs else None
        pack_data = mkbm.pack(export.MODELS_LUA, title="Title", author="Author", res=(320, 180),
                              mesh=enc_mesh, extra=[(S.ANIM, enc_anim)] if enc_anim else [])

        self.assertEqual(exp_data, pack_data)

    def test_cart_equals_mkbm_pack_with_cover(self):
        """export_cart with cover bytes match mkbm.pack() with cover."""
        if not os.path.exists(os.path.join(C.REPO, "scripts", "mkbm.py")):
            self.skipTest("repo scripts not available")

        mkbm = C.import_script("mkbm")
        mesh, anim = C.forge_assets()
        img = imageio.Image(88, 88, 0xFF00FF)

        # Build with export_cart
        with tempfile.TemporaryDirectory() as tmpdir:
            path = os.path.join(tmpdir, "test.bm")
            data = export.export_cart(path, "function _draw() cls(0) end\n", mesh, anim, "Title", "Author",
                                      cover=img)
            with open(path, "rb") as f:
                exp_data = f.read()

        # Build with mkbm.pack
        enc_mesh = meshmod.encode(mesh)
        enc_anim = animmod.encode(anim)
        cover_body = export.cover_section(img)
        cover_w, cover_h = img.w, img.h

        pack_data = mkbm.pack(b"function _draw() cls(0) end\n", title="Title", author="Author",
                              res=(320, 180), cover=(cover_w, cover_h, cover_body[4:]),
                              mesh=enc_mesh, extra=[(S.ANIM, enc_anim)])

        self.assertEqual(exp_data, pack_data)

    def test_cart_order(self):
        """export_cart section order: COVER, LUA, MESH, ANIM (or without COVER/ANIM)."""
        mesh, anim = C.forge_assets()

        # Without cover or anim
        with tempfile.TemporaryDirectory() as tmpdir:
            path = os.path.join(tmpdir, "test.bm")
            anim_none = bm_core.Anim([])  # no rigs
            export.export_cart(path, "function _draw() end\n", mesh, anim_none, "T", "A")
            cart = container.read_file(path)
            self.assertEqual([s.type for s in cart.sections], [S.LUA, S.MESH])

        # With cover and anim
        with tempfile.TemporaryDirectory() as tmpdir:
            path = os.path.join(tmpdir, "test.bm")
            img = imageio.Image(16, 16, 0xFF00FF)
            export.export_cart(path, "function _draw() end\n", mesh, anim, "T", "A", cover=img)
            cart = container.read_file(path)
            self.assertEqual([s.type for s in cart.sections], [S.COVER, S.LUA, S.MESH, S.ANIM])


class CartValidation(unittest.TestCase):
    def test_editions(self):
        """export_cart enforces edition constraints: .b16 360x360, .bm Pi resolution."""
        mesh, anim = C.forge_assets()
        lua = "function _draw() end\n"

        # .b16 at 360x360: ok
        with tempfile.TemporaryDirectory() as tmpdir:
            path = os.path.join(tmpdir, "test.b16")
            export.export_cart(path, lua, mesh, anim, "T", "A", res=(360, 360))
            self.assertTrue(os.path.exists(path))

        # .b16 at 320x180: error
        with tempfile.TemporaryDirectory() as tmpdir:
            path = os.path.join(tmpdir, "test.b16")
            with self.assertRaises(ValidationError):
                export.export_cart(path, lua, mesh, anim, "T", "A", res=(320, 180))

        # .bm at 360x360: error
        with tempfile.TemporaryDirectory() as tmpdir:
            path = os.path.join(tmpdir, "test.bm")
            with self.assertRaises(ValidationError):
                export.export_cart(path, lua, mesh, anim, "T", "A", res=(360, 360))

        # .bme (project): error
        with tempfile.TemporaryDirectory() as tmpdir:
            path = os.path.join(tmpdir, "test.bme")
            with self.assertRaises(ValidationError):
                export.export_cart(path, lua, mesh, anim, "T", "A")

    def test_budget(self):
        """frame_triangles and check_budget enforce triangle budgets."""
        mesh, anim = C.forge_assets()

        # humanoid 156 faces + ground 12 faces = 168 triangles
        total = export.frame_triangles(mesh, {"humanoid": 1, "ground": 1})
        self.assertEqual(total, 168)

        # exceeding budget raises
        with tempfile.TemporaryDirectory() as tmpdir:
            path = os.path.join(tmpdir, "test.bm")
            with self.assertRaises(ValidationError):
                export.export_cart(path, "function _draw() end\n", mesh, anim, "T", "A",
                                   draws={"humanoid": 8}, budget=1200)  # 156*8 = 1248

        # unknown model name raises
        with tempfile.TemporaryDirectory() as tmpdir:
            path = os.path.join(tmpdir, "test.bm")
            with self.assertRaises(ValidationError):
                export.export_cart(path, "function _draw() end\n", mesh, anim, "T", "A",
                                   draws={"unknown": 1})


class CoverSection(unittest.TestCase):
    def test_cover_section(self):
        """cover_section encodes Image to COVER bytes: 4 bytes (w,h) + 4 bytes per pixel."""
        img = imageio.Image(88, 88, 0xFF00FF)
        body = export.cover_section(img)
        # 4 (header) + 88*88*4 (RGBA) = 30976
        self.assertEqual(len(body), 4 + 88 * 88 * 4)

        # 513x1 is out of bounds (max 512)
        img_bad = imageio.Image(513, 1, 0xFF00FF)
        with self.assertRaises(ValidationError):
            export.cover_section(img_bad)


class Bmm(unittest.TestCase):
    def test_bmm(self):
        """export_bmm creates BMRES file: kind=RES_MODEL, sections [MESH, ANIM]."""
        mesh, anim = C.forge_assets()

        with tempfile.TemporaryDirectory() as tmpdir:
            path = os.path.join(tmpdir, "HUMANOID.BMM")
            data = export.export_bmm(path, mesh, anim, "Title", "Author")

            # Read and check
            cart = container.read_file(path)
            self.assertTrue(cart.header.is_res)
            self.assertEqual(cart.header.kind, container.RES_MODEL)
            self.assertEqual([s.type for s in cart.sections], [S.MESH, S.ANIM])

            # bmres.load should work (if available)
            if os.path.exists(os.path.join(C.REPO, "scripts", "bmres.py")):
                bmres = C.import_script("bmres")
                parsed = bmres.load(data)
                self.assertIsNotNone(parsed)


if __name__ == "__main__":
    unittest.main()
