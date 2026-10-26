"""Opt-in: runs the forgedemo cartridge in the console's real runtime (bmhost).

Needs BM_FORGE_HOST=1, a POSIX host (WSL) and <repo>/build/host/bmhost-bin
(`make build/host/bmhost-bin`; rebuild it when bm-core moved, it may be stale).
Source of the command line: tests/host/bmhost.c (--seconds, --input, --shots,
--square), Makefile test-gameapi (greps a log() line).
Never replaces a console check: bmhost is the real runtime on a PC, not the Pi.
"""
import importlib.util
import os
import re
import subprocess
import tempfile
import unittest

import _common as C

BMHOST = os.path.join(C.REPO, "build", "host", "bmhost-bin")
DEMO = os.path.join(C.SKILL, "examples", "game_demo")
ENABLED = (os.environ.get("BM_FORGE_HOST") == "1" and os.name == "posix"
           and os.path.exists(BMHOST))


@unittest.skipUnless(ENABLED, "set BM_FORGE_HOST=1 on WSL with build/host/bmhost-bin built")
class BmhostDemo(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        spec = importlib.util.spec_from_file_location("build_demo_host", os.path.join(DEMO, "build.py"))
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        cls.out = cls.tmp.name
        cls.paths = mod.main(os.path.join(cls.out, "build"))
        cls.game = mod.GAME

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_cart(self, ext, extra=()):
        shots = os.path.join(self.out, "shots_" + ext)
        os.makedirs(shots, exist_ok=True)
        cart = self.paths[f"{self.game}.{ext}"]
        cmd = [BMHOST, cart, "--seconds", "7", "--input", os.path.join(DEMO, "input.txt"),
               "--shots", shots] + list(extra)
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=180)
        tail = "\n".join(r.stderr.splitlines()[-15:])
        self.assertEqual(r.returncode, 0, tail)
        m = re.search(r"(?m)^forgedemo: (\d+)/(\d+) checks passed", r.stderr)
        self.assertIsNotNone(m, tail)
        self.assertEqual(m.group(1), m.group(2), tail)
        self.assertNotIn("forgedemo: FAIL", r.stderr)
        return shots

    def test_bm_runs(self):
        shots = self.run_cart("bm")
        data = []
        for name in ("idle", "walk", "jump"):
            with open(os.path.join(shots, name + ".png"), "rb") as f:
                data.append(f.read())
        self.assertEqual(len(set(data)), 3, "the three clip shots must differ")


if __name__ == "__main__":
    unittest.main()
