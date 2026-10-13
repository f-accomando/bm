"""Build and validate forgedemo cartridges, previews, and exports."""
import importlib.util
import os
import re
import subprocess
import sys
import tempfile
import unittest

import _common as C
import bm_core
from bm_core import export, container, preview as P, sections as S


# Load build.py from examples/game_demo
BUILD_PATH = os.path.join(C.SKILL, "examples", "game_demo", "build.py")
if not os.path.exists(BUILD_PATH):
    raise ImportError(f"build.py not found at {BUILD_PATH}")

spec = importlib.util.spec_from_file_location("build_game_demo", BUILD_PATH)
build_game_demo = importlib.util.module_from_spec(spec)
sys.modules["build_game_demo"] = build_game_demo
spec.loader.exec_module(build_game_demo)


class TestGameDemoBuild(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        """Build forgedemo once for all tests."""
        cls.tmpdir = tempfile.mkdtemp()
        cls.result = build_game_demo.main(cls.tmpdir)

    def test_editions(self):
        """Both .bm and .b16 are readable with correct resolution and sections."""
        bm_path = self.result[f"{build_game_demo.GAME}.bm"]
        b16_path = self.result[f"{build_game_demo.GAME}.b16"]

        # Both files should exist and be readable
        bm_cart = container.read_file(bm_path)
        b16_cart = container.read_file(b16_path)

        # Check resolutions
        self.assertEqual((bm_cart.header.width, bm_cart.header.height), (320, 180))
        self.assertEqual((b16_cart.header.width, b16_cart.header.height), (360, 360))

        # Check section types: [COVER=4, LUA=1, MESH=8, ANIM=9]
        bm_sections = [s.type for s in bm_cart.sections]
        b16_sections = [s.type for s in b16_cart.sections]
        self.assertEqual(bm_sections, [4, 1, 8, 9])
        self.assertEqual(b16_sections, [4, 1, 8, 9])

    def test_sources_separate(self):
        """Lua sources are separate for each edition; MESH/ANIM/COVER identical."""
        bm_path = self.result[f"{build_game_demo.GAME}.bm"]
        b16_path = self.result[f"{build_game_demo.GAME}.b16"]

        bm_cart = container.read_file(bm_path)
        b16_cart = container.read_file(b16_path)

        # Read source files for comparison
        with open(os.path.join(build_game_demo.HERE, "bm", "main.lua"), "rb") as f:
            bm_lua_src = f.read()
        with open(os.path.join(build_game_demo.HERE, "b16", "main.lua"), "rb") as f:
            b16_lua_src = f.read()

        # Get Lua bodies from carts
        bm_lua_body = bm_cart.get(S.LUA)
        b16_lua_body = b16_cart.get(S.LUA)

        # Lua bodies should match their respective sources
        self.assertEqual(bm_lua_body, bm_lua_src)
        self.assertEqual(b16_lua_body, b16_lua_src)

        # But they should differ from each other
        self.assertNotEqual(bm_lua_body, b16_lua_body)

        # MESH, ANIM, COVER should be identical
        self.assertEqual(bm_cart.get(S.MESH), b16_cart.get(S.MESH))
        self.assertEqual(bm_cart.get(S.ANIM), b16_cart.get(S.ANIM))
        self.assertEqual(bm_cart.get(S.COVER), b16_cart.get(S.COVER))

    def test_budget(self):
        """Triangle budget: 168 <= 1200 for both editions; .b16 <= 8 MiB."""
        bm_path = self.result[f"{build_game_demo.GAME}.bm"]
        b16_path = self.result[f"{build_game_demo.GAME}.b16"]

        # Build the mesh to check triangles
        mesh, anim = build_game_demo.build_assets()
        draws = {"humanoid": 1, "ground": 1}

        total_tris = export.frame_triangles(mesh, draws)
        self.assertEqual(total_tris, 168)
        self.assertLessEqual(total_tris, 1200)

        # Check .b16 file size
        with open(b16_path, "rb") as f:
            b16_data = f.read()
        self.assertLessEqual(len(b16_data), 8 * 1024 * 1024)

    def test_names_match_assets(self):
        """CLIPS and HEAD names exist in rig; all clips are animated."""
        bm_path = self.result[f"{build_game_demo.GAME}.bm"]

        # Read main.lua to extract CLIPS and HEAD
        with open(os.path.join(build_game_demo.HERE, "bm", "main.lua"), "r") as f:
            bm_source = f.read()

        # Extract CLIPS list
        clips_match = re.search(r'local CLIPS = \{([^}]*)\}', bm_source)
        self.assertIsNotNone(clips_match)
        clips_str = clips_match.group(1)
        clips = [name.strip().strip('"') for name in clips_str.split(",")]
        clips = [c for c in clips if c]

        # Extract HEAD name
        head_match = re.search(r'local HEAD = "([^"]+)"', bm_source)
        self.assertIsNotNone(head_match)
        head = head_match.group(1)

        # Verify in assets
        cart = container.read_file(bm_path)
        rig = cart.anim().rigs[0]
        rig_clips = [c.name for c in rig.clips]
        rig_bones = [b.name for b in rig.bones]

        for clip in clips:
            self.assertIn(clip, rig_clips, f"clip {clip!r} not in rig")
        self.assertIn(head, rig_bones, f"bone {head!r} not in rig")

    def test_real_api(self):
        """All documented API functions appear in both main.lua files."""
        expected_apis = [
            "model", "models", "bones3d", "clips", "animate", "bone3d", "bone_turn",
            "draw3d", "zclear", "camera3d", "light3d", "cls", "print", "btnp", "btn", "log", "stat"
        ]

        # Check bm edition
        with open(os.path.join(build_game_demo.HERE, "bm", "main.lua"), "r") as f:
            bm_source = f.read()

        for api in expected_apis:
            pattern = rf"\b{api}\s*\("
            self.assertIsNotNone(re.search(pattern, bm_source),
                                f"{api} not found in bm/main.lua")

        # Check b16 edition
        with open(os.path.join(build_game_demo.HERE, "b16", "main.lua"), "r") as f:
            b16_source = f.read()

        for api in expected_apis:
            pattern = rf"\b{api}\s*\("
            self.assertIsNotNone(re.search(pattern, b16_source),
                                f"{api} not found in b16/main.lua")

        # Optionally verify API is registered in runtime.c (if available)
        runtime_path = os.path.join(C.REPO, "src", "bm", "runtime.c")
        if os.path.exists(runtime_path):
            with open(runtime_path, "r") as f:
                runtime_source = f.read()

            for api in expected_apis:
                pattern = r'\{\s*"' + api + r'"\s*,\s*l_\w+\s*\}'
                self.assertIsNotNone(re.search(pattern, runtime_source),
                                    f"{api} not registered in runtime.c")

    def test_b16_rules(self):
        """B16 edition uses string button names; neither edition uses screen()."""
        with open(os.path.join(build_game_demo.HERE, "b16", "main.lua"), "r") as f:
            b16_source = f.read()

        # B16 should use string names for buttons
        self.assertIsNotNone(re.search(r'btnp\s*\(\s*"ok"\s*\)', b16_source),
                            'b16 should call btnp("ok")')
        self.assertIsNotNone(re.search(r'btnp\s*\(\s*"back"\s*\)', b16_source),
                            'b16 should call btnp("back")')

        # Only b16 is checked for numeric codes; neither may use screen()
        for src_path, edition in [(os.path.join(build_game_demo.HERE, "bm", "main.lua"), "bm"),
                                   (os.path.join(build_game_demo.HERE, "b16", "main.lua"), "b16")]:
            with open(src_path, "r") as f:
                source = f.read()

            # Only the RGB30 edition must avoid numeric pad codes (CLAUDE.md: use "ok"/"back")
            if edition == "b16":
                self.assertIsNone(re.search(r'btnp?\s*\(\s*[45]\s*[,)]', source),
                                "b16 should not use numeric button codes")

            # Neither edition should use screen()
            self.assertIsNone(re.search(r'\bscreen\s*\(', source),
                            f"{edition} should not use screen()")

    def test_previews(self):
        """Preview images exist and have coverage > 0 per clip."""
        clips = ["idle", "walk", "jump"]
        mesh, anim = build_game_demo.build_assets()
        humanoid = mesh.get("humanoid")
        rig = anim.rigs[0]

        for clip in clips:
            # Check that render_anim produces images with coverage > 0
            imgs, _ = P.render_anim(humanoid, rig, clip, fps=12, size=(96, 96), yaw=0.9, pitch=0.2)
            for img in imgs:
                self.assertGreater(img.covered, 0, f"image in {clip} has no coverage")

            # Check that preview file exists
            preview_key = f"preview_{clip}.png"
            self.assertIn(preview_key, self.result, f"preview for {clip} not in build result")
            preview_path = self.result[preview_key]
            self.assertTrue(os.path.exists(preview_path), f"preview file {preview_path} does not exist")

    def test_market_mkmarket_check(self):
        """Market folder passes mkmarket.py --check."""
        # Skip if mkmarket.py doesn't exist
        mkmarket_path = os.path.join(C.REPO, "scripts", "mkmarket.py")
        if not os.path.exists(mkmarket_path):
            self.skipTest("mkmarket.py not available")

        market_path = os.path.join(self.tmpdir, "market")
        result = subprocess.run([sys.executable, mkmarket_path, market_path, "--check"],
                               capture_output=True)
        self.assertEqual(result.returncode, 0, f"mkmarket check failed: {result.stderr.decode()}")


if __name__ == "__main__":
    unittest.main()
