"""Tests for scripts/mkicons.py: inside() and carts icons."""
import os
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "scripts"))

try:
    import mkicons
    HAS_MKICONS = True
except ImportError:
    HAS_MKICONS = False


class MkiconsTest(unittest.TestCase):
    def setUp(self):
        if not HAS_MKICONS:
            self.skipTest("Pillow missing")

    def test_inside_corners_and_center(self):
        self.assertEqual(mkicons.inside(64, 64), (64, 64))
        self.assertEqual(mkicons.inside(0, 0), (9, 9))
        self.assertEqual(mkicons.inside(127, 127), (118, 118))
        self.assertEqual(mkicons.inside(127, 0), (118, 9))

    def test_cart_icons_exist(self):
        self.assertEqual(len(mkicons.ICONS), 7)
        for name in mkicons.ICONS:
            path = os.path.join(ROOT, "carts", name, "icon.png")
            self.assertTrue(os.path.isfile(path), f"Missing icon: {path}")

    def test_border_points_moved_inward(self):
        self.assertNotEqual(mkicons.inside(64, 0), (64, 0))
        self.assertNotEqual(mkicons.inside(0, 64), (0, 64))
        self.assertNotEqual(mkicons.inside(127, 64), (127, 64))
        self.assertNotEqual(mkicons.inside(64, 127), (64, 127))


if __name__ == '__main__':
    unittest.main()
