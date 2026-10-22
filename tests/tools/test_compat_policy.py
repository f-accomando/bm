"""The C table of the compatibility policy against docs/spec/compat-policy.toml.

  python tests/tools/test_compat_policy.py build/host/test_compat

The host test `test_compat --grid` reads a grid of real headers with bm_compat_facts and prints the verdict of
bm_compat_classify. Here the same facts go through scripts/bm_weight.py classify (the toml itself): every action, rule
number and reason must be equal. Also: mkcompat --check (the generated header is current)."""
import os
import subprocess
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
import bm_weight  # noqa: E402

BIN = sys.argv.pop(1) if len(sys.argv) > 1 and os.path.isfile(sys.argv[1]) else None
KERNEL = {1: "pi-zero-w", 2: "rgb30"}
EDITION = {0: "bm", 2: "b16", 1: "project"}
ACTION = ("show", "grey", "hide")


class GridTest(unittest.TestCase):
    def test_c_table_is_current(self):
        r = subprocess.run([sys.executable, os.path.join(ROOT, "scripts", "mkcompat.py"), "--check"],
                           capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)

    @unittest.skipUnless(BIN, "needs the path of build/host/test_compat")
    def test_c_equals_toml(self):
        policy = bm_weight.load_policy()
        out = subprocess.run([BIN, "--grid"], capture_output=True, text=True, check=True).stdout.splitlines()
        self.assertGreater(len(out), 1000)
        for line in out:
            left, right = line.split(" -> ")
            k, m, e, d, w, h, t, audio, size = (int(x) for x in left.split())
            act, rule, why = right.split(" ", 2)
            declared = d != 0
            facts = {"edition": EDITION[e], "res": "%dx%d" % (w, h), "uses_3d": bool(t), "audio_bytes": audio,
                     "size_bytes": size, "declared": declared,
                     "targets": d if declared else (3 if e == 2 else 1)}
            want = bm_weight.classify(facts, KERNEL[k], policy, "market" if m else "menu")
            self.assertEqual((ACTION[int(act)], why, int(rule)), want, line)


if __name__ == "__main__":
    unittest.main()
