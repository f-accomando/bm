"""Tests for the map layers of scripts/mkbm.py: read_map and read_layers."""
import os, struct, sys, tempfile, unittest, warnings

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
import mkbm  # noqa: E402

# read_map opens its file without closing it; that is the script's, not what is tested here
warnings.simplefilter("ignore", ResourceWarning)


def cells(*values):
    return b"".join(struct.pack("<H", v) for v in values)


class MkbmMapTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)

    def csv(self, name, text):
        path = os.path.join(self._tmp.name, name)
        with open(path, "w", newline="\n") as fh:
            fh.write(text)
        return path

    def test_read_map_skips_blank_and_comment_lines(self):
        path = self.csv("m.csv", "# a comment\n1,2,3\n\n   \n4,5,6\n# end\n")
        self.assertEqual(mkbm.read_map(path), (3, 2, cells(1, 2, 3, 4, 5, 6)))

    def test_read_map_pads_short_rows_with_zero(self):
        path = self.csv("m.csv", "1,2,3\n4\n5,6\n")
        self.assertEqual(mkbm.read_map(path), (3, 3, cells(1, 2, 3, 4, 0, 0, 5, 6, 0)))

    def test_read_map_width_is_the_longest_row(self):
        path = self.csv("m.csv", "1,2\n3,4,5,6\n")
        w, h, data = mkbm.read_map(path)
        self.assertEqual((w, h), (4, 2))
        self.assertEqual(len(data), 4 * 2 * 2)

    def test_second_layer_smaller_is_padded_to_the_first(self):
        a = self.csv("a.csv", "1,2,3\n4,5,6\n7,8,9\n")
        b = self.csv("b.csv", "9,9\n9,9\n")
        layers = mkbm.read_layers(["back=" + a, "front=" + b])
        self.assertEqual([x[0] for x in layers], ["back", "front"])
        self.assertEqual(layers[1][1:3], (3, 3))
        self.assertEqual(layers[1][3], cells(9, 9, 0, 9, 9, 0, 0, 0, 0))

    def test_a_single_unnamed_layer_is_main(self):
        a = self.csv("a.csv", "1,2\n")
        self.assertEqual(mkbm.read_layers([a]), [("main", 2, 1, cells(1, 2))])

    def test_second_layer_bigger_exits(self):
        a = self.csv("a.csv", "1,2\n3,4\n")
        b = self.csv("b.csv", "1,2,3\n4,5,6\n")
        with self.assertRaises(SystemExit) as cm:
            mkbm.read_layers(["a=" + a, "b=" + b])
        self.assertIn("bigger than the first layer", str(cm.exception))

    def test_two_layers_with_the_same_name_exit(self):
        a = self.csv("a.csv", "1,2\n")
        with self.assertRaises(SystemExit) as cm:
            mkbm.read_layers(["x=" + a, "x=" + a])
        self.assertIn("two layers called", str(cm.exception))

    def test_more_than_eight_layers_exit(self):
        a = self.csv("a.csv", "1\n")
        specs = [f"l{i}={a}" for i in range(mkbm.LAYERS_MAX)]
        self.assertEqual(len(mkbm.read_layers(specs)), mkbm.LAYERS_MAX)   # 8 are fine
        with self.assertRaises(SystemExit) as cm:
            mkbm.read_layers(specs + [f"l8={a}"])
        self.assertIn("at most 8 layers", str(cm.exception))

    def test_name_over_16_bytes_exits(self):
        a = self.csv("a.csv", "1\n")
        ok = "n" * mkbm.LAYER_NAME
        self.assertEqual(mkbm.read_layers([f"{ok}={a}"])[0][0], ok)       # exactly 16 is fine
        with self.assertRaises(SystemExit) as cm:
            mkbm.read_layers([f"{ok}x={a}"])
        self.assertIn("at most 16 bytes", str(cm.exception))


if __name__ == "__main__":
    unittest.main()
