"""bm_core.syncwatch: the recorded hashes match the watched files."""
import unittest

import _common as C  # noqa: F401  (puts the skill on sys.path)
from bm_core import syncwatch


class SyncWatch(unittest.TestCase):
    def test_in_sync(self):
        drift = syncwatch.check()
        self.assertEqual(drift, [], "watched files changed since bm_core was checked: "
                         "see bm_core/README.md 'Keeping in sync'")

    def test_watch_list(self):
        self.assertEqual(syncwatch.WATCH, ["core/src/bm/bm.h", "core/src/bm/format.c", "core/scripts/bmmesh.py",
                                           "core/src/script/bm3d.lua", "core/src/gpu/version3d.h", "core/src/bm/runtime.c",
                                       "core/src/bm/r3d.c", "bm/animator/main.lua"])
        self.assertTrue(all(syncwatch.digest(p) for p in syncwatch.WATCH))


if __name__ == "__main__":
    unittest.main()
