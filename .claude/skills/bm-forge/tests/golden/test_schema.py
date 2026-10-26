"""Schema of every golden: header, section list, models, rigs, as the real
tools wrote them (values read once and fixed here; README.md says where each
file comes from)."""
import unittest

import _common as C
from bm_core import ValidationError, container, edition_of_cart, sections as S

VILLAGE = [("ground", 169, 288), ("house", 123, 180), ("tree", 12, 16), ("bush", 8, 8), ("well", 58, 86),
           ("fence", 10, 16), ("crates", 20, 36)]

# name -> (w, h, flags, [section types], [(model, nv, nf)], [(rig, bones, nv, [clips])])
EXPECT = {
    "village_models.bm": (320, 180, 0, [S.LUA, S.SHEET8, S.MESH, S.ANIM], VILLAGE + [("villager", 112, 168)],
                          [("villager", 7, 112, ["idle", "walk", "wave"])]),
    "anim_figure.bm": (640, 360, 0, [S.LUA, S.MESH, S.ANIM], [("figure", 16, 24)],
                       [("figure", 2, 16, ["wave", "still"])]),
    "bmmesh_lit.bm": (320, 180, 0, [S.LUA, S.MESH, S.ANIM], [("litbox", 6, 3), ("flat", 3, 1)],
                      [("litbox", 3, 6, ["bend", "sway"])]),
    "studio_blocks.bme": (640, 360, 0, [S.LUA, S.MESH, S.SHEET, S.ANIM], [("model", 15, 16)],
                          [("model", 2, 15, ["anim1"])]),
    "studio_copy3d.bme": (640, 360, 0, [S.COVER, S.LUA, S.SHEET8, S.MESH, S.ANIM],
                          VILLAGE + [("villager", 112, 168)], [("villager", 7, 112, ["idle", "walk", "wave"])]),
    "mesh_village.bme": (320, 180, 0, [S.COVER, S.LUA, S.SHEET8, S.MESH, S.ANIM],
                         VILLAGE + [("villager", 111, 164)], [("villager", 7, 111, ["idle", "walk", "wave"])]),
    "mesh_astrowin.bme": (640, 360, 0, [S.COVER, S.LUA, S.MESH], [("hero", 30, 32), ("cube", 25, 48),
                                                                  ("plane", 32, 40)], []),
}

# the luahost stand-in container (tests/studio/tools3d_host.lua:49-66 pack_cart) writes flags 0:
# the kernel's cart_write sets the project bit after bm_rewrite_with (src/bm/runtime.c:6467-6468),
# the stand-in does not. So these .bme fail bm_core.check_path; their bytes are still valid.
NO_PROJECT_BIT = {"studio_blocks.bme", "studio_copy3d.bme", "mesh_village.bme", "mesh_astrowin.bme"}


class Schema(unittest.TestCase):
    def test_every_golden_listed(self):
        self.assertEqual(sorted(EXPECT), sorted(C.FILES))

    def test_schema(self):
        for name, (w, h, flags, types, models, rigs) in EXPECT.items():
            with self.subTest(name):
                cart = container.parse(C.load(name))
                hd = cart.header
                self.assertEqual(hd.magic, container.MAGIC_CART)
                self.assertEqual((hd.width, hd.height, hd.flags, hd.pixel_format), (w, h, flags, 1))
                self.assertEqual([s.type for s in cart.sections], types)
                self.assertEqual([s.stored_type for s in cart.sections], types)        # no legacy 6/7
                self.assertTrue(all(s.reserved == 0 for s in cart.sections))
                self.assertEqual(edition_of_cart(cart), "bm")
                m = cart.mesh()
                self.assertEqual([(x.name, len(x.verts), len(x.faces)) for x in m.models], models)
                for x in m.models:
                    self.assertLessEqual(len(x.name.encode()), 15)                      # no 16-byte name
                a = cart.anim()
                got = [(r.model, len(r.bones), len(r.vbones), [c.name for c in r.clips]) for r in a.rigs] if a else []
                self.assertEqual(got, rigs)
                info = container.validate(cart)
                self.assertIn(S.LUA, info)

    def test_paths(self):
        for name in C.FILES:
            with self.subTest(name):
                cart = container.parse(C.load(name))
                if name in NO_PROJECT_BIT:
                    with self.assertRaises(ValidationError):
                        container.check_path(C.path(name), cart)
                else:
                    container.check_path(C.path(name), cart)

    def test_village_materials(self):
        """FORMATS.md: village models have inset 64 and no material bits; villager
        clips are smooth (1) and loop"""
        cart = container.parse(C.load("village_models.bm"))
        m = cart.mesh()
        self.assertEqual(m.inset, 64)
        self.assertTrue(all(f.material == 0 for x in m.models for f in x.faces))
        self.assertTrue(all(c.mode == 1 and c.loop for c in cart.anim().rigs[0].clips))

    def test_lit_golden(self):
        """bmmesh_lit.bm: lit flag, material bits and lights as scripts/bmmesh.py wrote them"""
        m = container.parse(C.load("bmmesh_lit.bm")).mesh()
        litbox, flat = m.models
        self.assertTrue(litbox.lit)
        self.assertFalse(flat.lit)
        self.assertEqual(m.inset, 128)
        f0, f1, f2 = litbox.faces
        self.assertEqual(f0.colour, 0x60FF8000)
        self.assertEqual(f0.material, 0x60)
        self.assertEqual(f0.light(True), (128, 128, 128, 255, 64, 0, 10, 20, 30))
        self.assertTrue(f1.textured)
        self.assertEqual(f1.colour & 0x7F000000, 0x10000000)
        self.assertNotEqual(f1.extra, 0)
        self.assertEqual(f2.colour, 0x060000FF)
        self.assertEqual(flat.faces[0].uv_px(), (0.125, 0.25, 1.5, 2.0, 3.875, 4.0))
        self.assertEqual(flat.faces[0].colour, 0x08123456)
        bend, sway = container.parse(C.load("bmmesh_lit.bm")).anim().rigs[0].clips
        self.assertEqual((bend.mode, bend.loop, sway.mode, sway.loop), (2, False, 0, True))


if __name__ == "__main__":
    unittest.main()
