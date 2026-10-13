"""bm_core against the repo's own Python readers (scripts/bmmesh.py), imported
by path for the comparison only. Every golden must decode to the same models
and rigs; village/models.bm is the reference case asked for."""
import json
import os
import struct
import unittest

import _common as C
from bm_core import ValidationError, container, glb, mesh, sections as S

bmmesh = C.import_script("bmmesh")


def as_bmmesh_models(m):
    """bm_core Mesh -> the (models, inset) of scripts/bmmesh.py:decode"""
    out = []
    for x in m.models:
        faces = [(f.a, f.b, f.c, f.colour, tuple(t / 8 for t in f.uv8)) for f in x.faces]
        out.append({"name": x.name, "verts": [tuple(v) for v in x.verts], "faces": faces})
    return out, m.inset / 256


def as_bmmesh_rigs(a):
    """bm_core Anim -> the list of scripts/bmmesh.py:decode_anim"""
    out = []
    for r in a.rigs:
        bones = [{"name": b.name, "parent": b.parent, "head": tuple(b.head), "tail": tuple(b.tail)} for b in r.bones]
        clips = [{"name": c.name, "mode": c.mode, "loop": c.loop, "length": c.length,
                  "keys": [{"t": k.time, "pose": [{"q": tuple(p.q), "t": tuple(p.t)} for p in k.pose]}
                           for k in c.keys]} for c in r.clips]
        out.append((r.model, bones, list(r.vbones), clips))
    return out


class CrossCheck(unittest.TestCase):
    def check(self, name):
        data = C.load(name)
        cart = container.parse(data)
        ref = dict(bmmesh.cart_sections(data))
        self.assertEqual([t for t, _ in bmmesh.cart_sections(data)], [s.type for s in cart.sections])
        models, inset = bmmesh.decode(ref[S.MESH])
        self.assertEqual(as_bmmesh_models(cart.mesh()), (models, inset))
        if S.ANIM in ref:
            self.assertEqual(as_bmmesh_rigs(cart.anim()), bmmesh.decode_anim(ref[S.ANIM]))

    def test_village(self):
        self.check("village_models.bm")

    def test_village_is_the_repo_file(self):
        with open(os.path.join(C.REPO, "carts", "village", "models.bm"), "rb") as f:
            self.assertEqual(f.read(), C.load("village_models.bm"),
                             "carts/village/models.bm changed: refresh the golden (README.md)")

    def test_every_golden(self):
        for name in C.FILES:
            with self.subTest(name):
                self.check(name)

    def test_bmmesh_encode_equals(self):
        """scripts/bmmesh.py:encode of bmmesh's own decode == bm_core.encode, for
        the goldens whose faces carry no light/extra (bmmesh.decode drops `extra`
        and the lit flag, so lit files are excluded: bmmesh_lit.bm)"""
        for name in C.FILES:
            if name == "bmmesh_lit.bm":
                continue
            with self.subTest(name):
                cart = container.parse(C.load(name))
                models, inset = bmmesh.decode(cart.get(S.MESH))
                self.assertEqual(bmmesh.encode(models, inset), mesh.encode(cart.mesh()))


class Glb(unittest.TestCase):
    """bm_core.glb is a port of the glTF half of scripts/bmmesh.py: same models"""

    def same(self, data):
        """bmmesh and bm_core agree on `data`: same models, or both refuse it"""
        try:
            ref, sheet, _ = bmmesh.glb_models(data)
        except ValueError:
            with self.assertRaises(ValidationError):
                glb.glb_models(data)
            return False
        got, sheet2 = glb.glb_models(data)
        self.assertEqual(sheet, sheet2)
        self.assertEqual(as_bmmesh_models(got)[0], [
            {"name": m["name"], "verts": [tuple(v) for v in m["verts"]],
             "faces": [(a, b, c, col, tuple(round(t * 8) / 8 for t in uv)) for a, b, c, col, uv in m["faces"]]}
            for m in ref])
        mesh.encode(got, kernel_limits=True)
        return True

    def test_kitchen_glb(self):
        """Meshy .glb files are textured without a bm sheet: bmmesh refuses them,
        bm_core must refuse them too"""
        found = 0
        for k in range(1, 5):
            p = os.path.join(C.REPO, "carts", "kitchen", "models", f"chef{k}.glb")
            if not os.path.exists(p):
                continue
            found += 1
            with open(p, "rb") as f, self.subTest(p):
                self.same(f.read())
        if not found:
            self.skipTest("no carts/kitchen/models/chef*.glb in this checkout")

    def test_synthetic_glb(self):
        """a small untextured glTF: a quad under a rotated, scaled node (z flip,
        winding turn), a mirrored copy (det < 0), vertex colours, a duplicate
        node name"""
        pos = struct.pack("<12f", 0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0)
        col = struct.pack("<16f", 1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 1, 1, 1, 1, 1, 1)
        idx = struct.pack("<6H", 0, 1, 2, 0, 2, 3)
        bin_ = pos + col + idx
        js = {"asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": [0, 1]}],
              "nodes": [{"name": "quad", "mesh": 0, "rotation": [0, 0.7071068, 0, 0.7071068], "scale": [2, 2, 2],
                         "children": [2]},
                        {"name": "quad", "mesh": 0, "scale": [-1, 1, 1], "translation": [3, 0, 0]},
                        {"mesh": 0, "translation": [0, 5, 0]}],
              "meshes": [{"name": "m", "primitives": [{"attributes": {"POSITION": 0, "COLOR_0": 1}, "indices": 2,
                                                       "material": 0}]}],
              "materials": [{"pbrMetallicRoughness": {"baseColorFactor": [0.5, 0.8, 1, 1]}}],
              "buffers": [{"byteLength": len(bin_)}],
              "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": 48},
                              {"buffer": 0, "byteOffset": 48, "byteLength": 64},
                              {"buffer": 0, "byteOffset": 112, "byteLength": 12}],
              "accessors": [{"bufferView": 0, "componentType": 5126, "count": 4, "type": "VEC3"},
                            {"bufferView": 1, "componentType": 5126, "count": 4, "type": "VEC4"},
                            {"bufferView": 2, "componentType": 5123, "count": 6, "type": "SCALAR"}]}
        j = json.dumps(js).encode()
        j += b" " * ((-len(j)) % 4)
        b = bin_ + b"\0" * ((-len(bin_)) % 4)
        data = (struct.pack("<4sII", b"glTF", 2, 12 + 8 + len(j) + 8 + len(b)) + struct.pack("<I4s", len(j), b"JSON")
                + j + struct.pack("<I4s", len(b), b"BIN\0") + b)
        self.assertTrue(self.same(data))
        got, _ = glb.glb_models(data)
        self.assertEqual([m.name for m in got.models], ["quad", "m", "quad2"])
        with self.assertRaises(ValidationError):
            glb.glb_models(b"glTF" + b"\0" * 4)


if __name__ == "__main__":
    unittest.main()
