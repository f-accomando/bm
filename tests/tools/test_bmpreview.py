"""Tests for scripts/bmpreview.py: a COVER and a PREVIEW inside a resource file."""
import os, struct, sys, tempfile, unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
import bmkit  # noqa: E402
import bmmesh  # noqa: E402
import bmpreview  # noqa: E402
import bmres  # noqa: E402


def cube_kit():
    m = {"name": "cube", "verts": [(-1, -1, -1), (1, -1, -1), (1, 1, -1), (-1, 1, -1), (-1, -1, 1), (1, -1, 1), (1, 1, 1), (-1, 1, 1)],
         "faces": [(0, 3, 2, 0xC04040, None), (0, 2, 1, 0xC04040, None), (4, 5, 6, 0x40C040, None), (4, 6, 7, 0x40C040, None),
                   (0, 1, 5, 0x4040C0, None), (0, 5, 4, 0x4040C0, None), (3, 7, 6, 0xC0C040, None), (3, 6, 2, 0xC0C040, None),
                   (0, 4, 7, 0x40C0C0, None), (0, 7, 3, 0x40C0C0, None), (1, 2, 6, 0xC040C0, None), (1, 6, 5, 0xC040C0, None)]}
    return bmres.File(bmres.KIT, "cube", "t", [[bmres.SEC_MESH, bmmesh.encode([m])]])


class PreviewTest(unittest.TestCase):
    def test_embed_and_read_back(self):
        f = bmpreview.embed(cube_kit(), cover=32, size=24, frames=6)
        g = bmres.load(bmres.dump(f), "cube.bmk")
        self.assertEqual(g.sections[0][0], bmres.SEC_COVER)                      # the cover comes first
        w, h, rgba = bmres.cover_get(g)
        self.assertEqual((w, h, len(rgba)), (32, 32, 32 * 32 * 4))
        self.assertEqual(rgba[3], 0)                                             # the corner is clear
        self.assertTrue(any(rgba[i * 4 + 3] == 255 for i in range(w * h)))       # the cube is drawn
        fw, fh, fps, pal, frames = bmres.preview_get(g)
        self.assertEqual((fw, fh, len(frames), fps), (24, 24, 6, 8))
        self.assertEqual(pal[0], (0, 0, 0))
        self.assertNotEqual(frames[0], frames[2])                                # it turns

    def test_same_bytes_every_time(self):
        a = bmres.dump(bmpreview.embed(cube_kit(), cover=32, size=24, frames=4))
        b = bmres.dump(bmpreview.embed(cube_kit(), cover=32, size=24, frames=4))
        self.assertEqual(a, b)

    def test_add_twice_keeps_one_of_each(self):
        f = bmpreview.embed(bmpreview.embed(cube_kit(), cover=32, size=24, frames=4), cover=32, size=24, frames=4)
        types = [t for t, _ in f.sections]
        self.assertEqual(types.count(bmres.SEC_COVER), 1)
        self.assertEqual(types.count(bmres.SEC_PREVIEW), 1)

    def test_broken_preview_is_refused(self):
        f = bmpreview.embed(cube_kit(), cover=32, size=24, frames=4)
        body = bytearray(f.get(bmres.SEC_PREVIEW))
        body[-1] = 255                                                           # a colour number out of the palette
        f.put(bmres.SEC_PREVIEW, bytes(body))
        with self.assertRaises(bmres.ResError):
            bmres.check(f)
        f.put(bmres.SEC_PREVIEW, bytes(body[:-3]))
        with self.assertRaises(bmres.ResError):
            bmres.check(f)

    def test_a_sound_file_has_no_cover(self):
        f = bmres.File(bmres.SOUND, "s", "t", [[bmres.SEC_COVER, struct.pack("<HH", 1, 1) + bytes(4)]])
        with self.assertRaises(bmres.ResError):
            bmres.check(f)

    def test_a_game_cover_does_not_go_into_a_kit(self):
        cart = bmres.File(bmres.CART, "g", "t", [[bmres.SEC_COVER, struct.pack("<HH", 1, 1) + bytes(4)],
                                                 [bmres.SEC_MESH, cube_kit().get(bmres.SEC_MESH)]])
        kit = bmres.extract_kit(cart)
        self.assertIsNone(kit.get(bmres.SEC_COVER))

    def test_the_island_kit_carries_its_picture(self):
        scene = bmkit.scene_model([m().model() for m in bmkit.MODELS])
        self.assertGreater(len(scene["faces"]), 1000)
        self.assertEqual(scene["name"], "scene")


if __name__ == "__main__":
    unittest.main()
