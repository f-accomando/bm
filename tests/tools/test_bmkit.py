"""Tests for scripts/bmkit.py: the island kit (.bmk) builds the same every time, reads back, and holds what the
bm SDK 3D map needs (layers floor, height, walls, objects), models with a map class, a shared sheet and 2D sprites."""
import os, struct, sys, unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
import bmkit  # noqa: E402
import bmres  # noqa: E402


class IslandKitTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.kit = bmkit.build_island()
        cls.data = bmres.dump(cls.kit)
        cls.back = bmres.load(cls.data, "island.bmk")

    def test_same_bytes_every_time(self):
        self.assertEqual(self.data, bmres.dump(bmkit.build_island()))

    def test_is_a_kit_with_one_sheet(self):
        self.assertEqual(self.back.kind, bmres.KIT)
        self.assertEqual(self.data[:8], b"BMRES\0\0\0")
        self.assertEqual(struct.unpack_from("<H", self.data, 12)[0], 6)
        types = [t for t, _ in self.back.sections]
        self.assertEqual(sum(1 for t in types if t in bmres.SHEETS), 1)
        for need in (bmres.SEC_MESH, bmres.SEC_MAP, bmres.SEC_LAYERS, bmres.SEC_FLAGS, bmres.SEC_SPRITES, bmres.SEC_INFO):
            self.assertIn(need, types)

    def test_layers_of_the_3d_map(self):
        layers = dict(bmres.layers_get(self.back))
        self.assertEqual(tuple(layers), bmkit.LAYER_NAMES)
        w = bmres.map_get(self.back)[0]
        self.assertEqual(w, bmkit.MAP_TILES * 2)
        floor, height, objs = layers["floor"], layers["height"], layers["objects"]
        water = bmkit.tile_id("water")
        self.assertEqual(floor[0], water)                          # the corner is the sea, at height 0
        self.assertEqual(height[0], 0)
        c = bmkit.MAP_TILES // 2
        self.assertEqual(objs[(c * 2) * w + c * 2], bmkit.OBJECTS[5][1])     # the campfire in the middle
        self.assertGreater(height[(c * 2) * w + c * 2], 0)         # on land
        self.assertTrue(any(v == bmkit.PLAYER_START for v in objs))

    def test_every_object_id_is_in_the_table(self):
        errors = bmkit.describe(self.back)[1]
        self.assertEqual(errors, [])

    def test_models_have_a_map_class_and_look_out(self):
        _, recs = bmres.mesh_parse(self.back.get(bmres.SEC_MESH))
        want = {"palm": bmkit.SOLID, "rock": bmkit.SOLID, "bush": bmkit.DECOR, "grass": bmkit.DECOR}
        got = {name: struct.unpack_from("<I", rec, 20)[0] for name, rec in recs}
        for name, cls in want.items():
            self.assertEqual(got[name] & 14, cls, name)
            self.assertEqual(got[name] & 1, 0, name)               # not "lit"
        self.assertEqual(sum(1 for m in bmkit.MODELS if bmkit.volume(m().model()) is not None
                             and bmkit.volume(m().model()) < -1e-6), 0)

    def test_sprites_of_the_objects_are_in_the_sheet(self):
        zones = {z["name"]: z for z in bmres.sprites_decode(self.back.get(bmres.SEC_SPRITES))}
        for name, _, model, sprite, *_ in bmkit.OBJECTS:
            self.assertIn(sprite, zones)
        self.assertEqual(zones["campfire2d"]["frames"], 4)

    def test_water_tile_flag(self):
        flags = bmres.flags_get(self.back)
        t = bmkit.TILES["water"]
        self.assertEqual(flags[(t * 2, 0)], 8)

    def test_a_rock_without_class_bits_still_reads(self):
        # the map class is the free bits of the model flags: a model with 0 reads as before
        m = {"name": "x", "verts": [(0, 0, 0), (1, 0, 0), (0, 1, 0)], "faces": [(0, 1, 2, 0xFFFFFF, None)]}
        body = bmres.bmmesh.encode([m])
        self.assertEqual(struct.unpack_from("<I", body, 8 + 20)[0], 0)
        m["flags"] = bmkit.SOLID | 1
        body = bmres.bmmesh.encode([m])
        self.assertEqual(struct.unpack_from("<I", body, 8 + 20)[0], bmkit.SOLID)


if __name__ == "__main__":
    unittest.main()
