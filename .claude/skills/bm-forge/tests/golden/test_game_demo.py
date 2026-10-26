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

    def test_cartridge(self):
        """The .bm is readable with the right resolution, sections and compatibility block."""
        bm_path = self.result[f"{build_game_demo.GAME}.bm"]
        bm_cart = container.read_file(bm_path)
        self.assertEqual((bm_cart.header.width, bm_cart.header.height), (320, 180))

        # Check section types: [COVER=4, LUA=1, MESH=8, ANIM=9]
        self.assertEqual([s.type for s in bm_cart.sections], [4, 1, 8, 9])

        # Compatibility block: flag bit 4 and targets = pi
        with open(bm_path, "rb") as f:
            raw = f.read(128)
        self.assertTrue(int.from_bytes(raw[18:20], "little") & 16)
        self.assertEqual(raw[120], 1)

    def test_source(self):
        """The Lua body of the .bm is the source file."""
        bm_cart = container.read_file(self.result[f"{build_game_demo.GAME}.bm"])
        with open(os.path.join(build_game_demo.HERE, "bm", "main.lua"), "rb") as f:
            self.assertEqual(bm_cart.get(S.LUA), f.read())

    def test_budget(self):
        """Triangle budget: 168 <= 1200."""
        mesh, anim = build_game_demo.build_assets()
        draws = {"humanoid": 1, "ground": 1}

        total_tris = export.frame_triangles(mesh, draws)
        self.assertEqual(total_tris, 168)
        self.assertLessEqual(total_tris, 1200)

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
        """All documented API functions appear in main.lua."""
        expected_apis = [
            "model", "models", "bones3d", "clips", "animate", "bone3d", "bone_turn",
            "draw3d", "zclear", "camera3d", "light3d", "cls", "print", "btnp", "btn", "log", "stat"
        ]

        with open(os.path.join(build_game_demo.HERE, "bm", "main.lua"), "r") as f:
            bm_source = f.read()

        for api in expected_apis:
            pattern = rf"\b{api}\s*\("
            self.assertIsNotNone(re.search(pattern, bm_source),
                                f"{api} not found in bm/main.lua")

        # Optionally verify API is registered in runtime.c (if available)
        runtime_path = os.path.join(C.REPO, "src", "bm", "runtime.c")
        if os.path.exists(runtime_path):
            with open(runtime_path, "r") as f:
                runtime_source = f.read()

            for api in expected_apis:
                pattern = r'\{\s*"' + api + r'"\s*,\s*l_\w+\s*\}'
                self.assertIsNotNone(re.search(pattern, runtime_source),
                                    f"{api} not registered in runtime.c")

    def test_rules(self):
        """The game does not use screen()."""
        with open(os.path.join(build_game_demo.HERE, "bm", "main.lua"), "r") as f:
            source = f.read()
        self.assertIsNone(re.search(r'\bscreen\s*\(', source), "bm should not use screen()")

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
