"""CLI for export subcommand: models, bmm, cart, folder, market."""
import os
import struct
import tempfile
import unittest

import _common as C
import bm_core
from bm_core import ValidationError, container, sections as S


class CliExport(unittest.TestCase):
    """Test the bm-forge CLI export subcommands."""

    def setUp(self):
        """Build a test models file with humanoid + ground, skeleton, and animations."""
        # Use forge_assets to get mesh and anim
        self.mesh, self.anim = C.forge_assets()

        # Get the Lua files from examples
        self.ex = os.path.join(C.SKILL, "examples")
        self.bm_lua_path = os.path.join(self.ex, "game_demo", "bm", "main.lua")
        self.b16_lua_path = os.path.join(self.ex, "game_demo", "b16", "main.lua")

        # Build a temp models file for use as --from
        self.temp_models_path = None

    def _make_temp_models_file(self):
        """Create a temporary models file from forge_assets."""
        if self.temp_models_path is not None:
            return self.temp_models_path

        tmpdir = tempfile.mkdtemp()
        self.temp_models_path = os.path.join(tmpdir, "models.bm")

        # Build mesh and anim
        cart = bm_core.export.models_cart(self.mesh, self.anim, "Test Models", "Test Author")
        container.write_file(self.temp_models_path, cart)

        return self.temp_models_path

    def tearDown(self):
        """Clean up temp files."""
        if self.temp_models_path and os.path.exists(self.temp_models_path):
            import shutil
            base_dir = os.path.dirname(self.temp_models_path)
            if os.path.exists(base_dir):
                shutil.rmtree(base_dir, ignore_errors=True)

    def test_export_models(self):
        """export models FROM -o OUT produces a readable .bm file."""
        with tempfile.TemporaryDirectory() as tmpdir:
            from_path = self._make_temp_models_file()
            out_path = os.path.join(tmpdir, "out.bm")

            code, stdout, stderr = C.run_cli(["export", "models", from_path, "-o", out_path])

            self.assertEqual(code, 0, f"stderr: {stderr}")
            self.assertTrue(os.path.exists(out_path))

            # Verify output is readable
            cart = container.read_file(out_path)
            mesh = cart.mesh()
            self.assertIsNotNone(mesh)
            self.assertEqual(len(mesh.models), 2)  # humanoid + ground

    def test_export_bmm_subset(self):
        """export bmm --model humanoid keeps only humanoid and its rig."""
        with tempfile.TemporaryDirectory() as tmpdir:
            from_path = self._make_temp_models_file()
            out_path = os.path.join(tmpdir, "out.bmm")

            code, stdout, stderr = C.run_cli(["export", "bmm", from_path, "-o", out_path, "--model", "humanoid"])

            self.assertEqual(code, 0, f"stderr: {stderr}")
            self.assertTrue(os.path.exists(out_path))

            # Verify output
            cart = container.read_file(out_path)
            mesh = cart.mesh()
            self.assertIsNotNone(mesh)
            self.assertEqual(len(mesh.models), 1)
            self.assertEqual(mesh.models[0].name, "humanoid")

            # Check that anim is preserved with only humanoid rig
            anim = cart.anim()
            self.assertIsNotNone(anim)
            self.assertEqual(len(anim.rigs), 1)
            self.assertEqual(anim.rigs[0].model, "humanoid")

    def test_export_cart_bm(self):
        """export cart for .bm has 320x180 resolution and COVER section when --cover-model given."""
        with tempfile.TemporaryDirectory() as tmpdir:
            from_path = self._make_temp_models_file()
            out_path = os.path.join(tmpdir, "test.bm")

            code, stdout, stderr = C.run_cli([
                "export", "cart", from_path,
                "--lua", self.bm_lua_path,
                "-o", out_path,
                "--title", "Test Game",
                "--cover-model", "humanoid"
            ])

            self.assertEqual(code, 0, f"stderr: {stderr}")
            self.assertTrue(os.path.exists(out_path))

            # Verify resolution
            hd = container.read_file(out_path).header
            width, height = hd.width, hd.height
            self.assertEqual(width, 320)
            self.assertEqual(height, 180)

            # Verify COVER section exists
            cart = container.read_file(out_path)
            cover_section = None
            for sect in cart.sections:
                if sect.type == S.COVER:
                    cover_section = sect
                    break
            self.assertIsNotNone(cover_section, "COVER section not found")

    def test_export_cart_b16(self):
        """export cart for .b16 has 360x360 resolution by default."""
        with tempfile.TemporaryDirectory() as tmpdir:
            from_path = self._make_temp_models_file()
            out_path = os.path.join(tmpdir, "test.b16")

            code, stdout, stderr = C.run_cli([
                "export", "cart", from_path,
                "--lua", self.b16_lua_path,
                "-o", out_path,
                "--title", "Test Game"
            ])

            self.assertEqual(code, 0, f"stderr: {stderr}")
            self.assertTrue(os.path.exists(out_path))

            # Verify resolution
            hd = container.read_file(out_path).header
            width, height = hd.width, hd.height
            self.assertEqual(width, 360)
            self.assertEqual(height, 360)

            # Verify edition
            cart = container.read_file(out_path)
            ed = container.edition_of_cart(cart)
            self.assertEqual(ed, "b16")

    def test_export_cart_budget(self):
        """export cart --budget 10 rejects if triangles exceed budget (exit code 1)."""
        with tempfile.TemporaryDirectory() as tmpdir:
            from_path = self._make_temp_models_file()
            out_path = os.path.join(tmpdir, "test.bm")

            code, stdout, stderr = C.run_cli([
                "export", "cart", from_path,
                "--lua", self.bm_lua_path,
                "-o", out_path,
                "--title", "Test Game",
                "--budget", "10"
            ])

            self.assertEqual(code, 1, f"expected code 1, got {code}: stderr={stderr}")
            self.assertFalse(os.path.exists(out_path))
            self.assertIn("budget", stderr.lower())

    def test_export_cart_bme_refused(self):
        """export cart -o x.bme exits with code 1 (ValidationError not special-cased)."""
        with tempfile.TemporaryDirectory() as tmpdir:
            from_path = self._make_temp_models_file()
            out_path = os.path.join(tmpdir, "test.bme")

            code, stdout, stderr = C.run_cli([
                "export", "cart", from_path,
                "--lua", self.bm_lua_path,
                "-o", out_path,
                "--title", "Test Game"
            ])

            self.assertEqual(code, 1, f"expected code 1, got {code}: stderr={stderr}")
            self.assertFalse(os.path.exists(out_path))
            self.assertIn(".bme", stderr.lower())

    def test_export_folder(self):
        """export folder creates main.lua, models.bm, make.txt; second run without --overwrite gives code 1."""
        with tempfile.TemporaryDirectory() as tmpdir:
            from_path = self._make_temp_models_file()
            root_path = os.path.join(tmpdir, "repo")
            os.makedirs(root_path)

            # First run: should succeed
            code, stdout, stderr = C.run_cli([
                "export", "folder", from_path,
                "--lua", self.bm_lua_path,
                "--root", root_path,
                "--game", "testgame",
                "--title", "Test Game"
            ])

            self.assertEqual(code, 0, f"stderr: {stderr}")

            # Check created files
            game_dir = os.path.join(root_path, "testgame")
            self.assertTrue(os.path.exists(os.path.join(game_dir, "main.lua")))
            self.assertTrue(os.path.exists(os.path.join(game_dir, "models.bm")))
            self.assertTrue(os.path.exists(os.path.join(game_dir, "make.txt")))

            # Second run without --overwrite: should fail
            code, stdout, stderr = C.run_cli([
                "export", "folder", from_path,
                "--lua", self.bm_lua_path,
                "--root", root_path,
                "--game", "testgame",
                "--title", "Test Game"
            ])

            self.assertEqual(code, 1, f"expected code 1, got {code}: stderr={stderr}")

    def test_export_market(self):
        """export market builds info.txt with license and about.txt line, and outputs paths."""
        with tempfile.TemporaryDirectory() as tmpdir:
            from_path = self._make_temp_models_file()

            # First build .bm and .b16 via export cart
            bm_path = os.path.join(tmpdir, "game.bm")
            b16_path = os.path.join(tmpdir, "game.b16")

            C.run_cli([
                "export", "cart", from_path,
                "--lua", self.bm_lua_path,
                "-o", bm_path,
                "--title", "Test Game"
            ])

            C.run_cli([
                "export", "cart", from_path,
                "--lua", self.b16_lua_path,
                "-o", b16_path,
                "--title", "Test Game"
            ])

            # Now export market
            market_root = os.path.join(tmpdir, "market")
            os.makedirs(market_root)

            code, stdout, stderr = C.run_cli([
                "export", "market",
                "--root", market_root,
                "--id", "forgedemo",
                "--version", "1.0",
                "--license", "MIT",
                "--about", "Test game",
                "--bm", bm_path,
                "--b16", b16_path
            ])

            self.assertEqual(code, 0, f"stderr: {stderr}")

            # Check info.txt
            info_path = os.path.join(market_root, "forgedemo", "info.txt")
            self.assertTrue(os.path.exists(info_path))

            with open(info_path, "r") as f:
                info_content = f.read()
            self.assertIn("license: MIT", info_content)
            self.assertIn("version: 1.0", info_content)
            self.assertIn("about: Test game", info_content)

            # Check stdout contains about.txt line
            self.assertIn("about.txt line:", stdout)
            self.assertIn("forgedemo: MIT |", stdout)


if __name__ == "__main__":
    unittest.main()
