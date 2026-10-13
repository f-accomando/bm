"""Round trip on every golden: read -> write gives the same bytes.

All goldens are laid out the way every writer lays them out (table, then
bodies in table order, each padded with zeros to 4), so a byte-identical
rebuild is expected for each. Where writers legitimately differ it is
said per case:
- legacy type 6 (MESH) / 7 (ANIM) would be rewritten as 8 / 9 (bm.h:77-79):
  no golden has them, tested on a synthetic file in test_negative.
- 16-byte names (bm3d.lua writes them, no NUL) need keep_long_names=True;
  no golden has one (checked by test_schema).
"""
import json
import unittest

import _common as C
from bm_core import anim, container, mesh, sections as S


class RoundTrip(unittest.TestCase):
    def test_container_bytes(self):
        for name in C.FILES:
            with self.subTest(name):
                data = C.load(name)
                cart = container.parse(data)
                self.assertEqual(container.build(cart), data)

    def test_mesh_bytes(self):
        for name in C.FILES:
            with self.subTest(name):
                cart = container.parse(C.load(name))
                body = cart.get(S.MESH)
                self.assertIsNotNone(body)
                self.assertEqual(mesh.encode(mesh.decode(body)), body)

    def test_anim_bytes(self):
        for name in C.FILES:
            cart = container.parse(C.load(name))
            body = cart.get(S.ANIM)
            if body is None:
                continue
            with self.subTest(name):
                self.assertEqual(anim.encode(anim.decode(body)), body)

    def test_json(self):
        """to_dict -> JSON text -> from_dict -> encode gives the same section bytes
        (floats are f32 values, exact in a Python float and in JSON repr)"""
        for name in C.FILES:
            with self.subTest(name):
                cart = container.parse(C.load(name))
                m = cart.mesh()
                m2 = mesh.Mesh.from_dict(json.loads(json.dumps(m.to_dict())))
                self.assertEqual(mesh.encode(m2), cart.get(S.MESH))
                a = cart.anim()
                if a:
                    a2 = anim.Anim.from_dict(json.loads(json.dumps(a.to_dict())))
                    self.assertEqual(anim.encode(a2), cart.get(S.ANIM))
                json.dumps(cart.to_dict())

    def test_replace_keeps_header(self):
        """set_mesh/set_anim on a parsed cart: header bytes kept (title, author,
        resolution, flags, 104..127) except count and CRC, like
        scripts/bmmesh.py:rewrite_cart"""
        data = C.load("village_models.bm")
        cart = container.parse(data)
        m = cart.mesh()
        m.models[0].verts[0][1] += 1.0
        cart.set_mesh(m)
        out = container.build(cart)
        self.assertEqual(out[:20], data[:20])
        self.assertEqual(out[24:128], data[24:128])
        self.assertNotEqual(out[20:24], data[20:24])
        self.assertEqual(container.parse(out).mesh().models[0].verts[0][1], m.models[0].verts[0][1])

    def test_same_as_bmmesh_rewrite(self):
        """bm_core's rewrite of new MESH bytes equals scripts/bmmesh.py:rewrite_cart"""
        bmmesh = C.import_script("bmmesh")
        data = C.load("village_models.bm")
        cart = container.parse(data)
        m = cart.mesh()
        m.models.pop()               # drop the villager: its rig must go too
        cart.set_mesh(m)
        cart.set_anim(None)
        secs = {t: b for t, b in bmmesh.cart_sections(data) if t != S.ANIM}
        secs[S.MESH] = mesh.encode(m)
        ref = bmmesh.rewrite_cart(data, secs)
        self.assertEqual(container.build(cart), ref)


if __name__ == "__main__":
    unittest.main()
