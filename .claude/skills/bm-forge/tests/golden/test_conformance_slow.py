"""SLOW, optional: the console's real kernel under QEMU reads the MESH/ANIM
it is given (tests/qemu_test.py test_models, test_animation; tests/qemu_test.py:6102,6169).

Runs only with BM_FORGE_SLOW=1 and qemu-system-arm on PATH (CI installs it,
.github/workflows/ci.yml:55; this WSL does not have it, references/TOOLS.md §3.4),
and a built kernel in build/. It only wires to the existing QEMU tests by
name: it does not yet feed bm_core's own output to the kernel (NON VERIFICATO
path; next step is a QEMU test that loads a bm_core-written cart).
"""
import os
import shutil
import subprocess
import sys
import unittest

import _common as C

SLOW = os.environ.get("BM_FORGE_SLOW") == "1"
QEMU = shutil.which("qemu-system-arm")
TESTS = ["test_models", "test_animation"]


def skip_reason():
    if not SLOW:
        return "slow: set BM_FORGE_SLOW=1 to run the QEMU conformance tests"
    if not QEMU:
        return "slow: qemu-system-arm not found on PATH"
    if not os.path.exists(os.path.join(C.REPO, "build", "kernel.img")):
        return "slow: build/kernel.img missing (run make first)"
    return None


@unittest.skipIf(skip_reason(), skip_reason() or "")
class SlowConformance(unittest.TestCase):
    slow = True

    def test_qemu(self):
        for name in TESTS:
            with self.subTest(name):
                r = subprocess.run([sys.executable, "tests/qemu_test.py", "--build", "build", "-k", name],
                                   cwd=C.REPO, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                   universal_newlines=True, timeout=900)
                self.assertEqual(r.returncode, 0, r.stdout[-3000:])


if __name__ == "__main__":
    unittest.main()
