"""Broken or refused input: every case must raise ValidationError (never a
bare struct.error / IndexError), on bytes (decode) and on the data model
(encode) where both exist."""
import copy
import os
import struct
import tempfile
import unittest

import _common as C
from bm_core import ValidationError, anim, container, mesh, sections as S
from bm_core.anim import Anim, Bone, Clip, Key, Pose, Rig
from bm_core.mesh import Face, Mesh, Model


def tri_model(name="m", nv=3):
    verts = [[float(i), 0.0, float(i % 2)] for i in range(nv)]
    return Model(name, verts, [Face(0, 1, 2)])


def rig_for(model="m", nv=3, nb=1, nclips=1, nkeys=1):
    bones = [Bone(f"b{i}", i - 1) for i in range(nb)]
    clips = [Clip(f"c{k}", 1.0, [Key(min(1.0, j / 10), [Pose() for _ in range(nb)]) for j in range(nkeys)])
             for k in range(nclips)]
    return Rig(model, bones, [0] * nv, clips)


class Container(unittest.TestCase):
    def setUp(self):
        self.data = C.load("anim_figure.bm")

    def bad(self, data, **kw):
        with self.assertRaises(ValidationError) as cm:
            container.parse(data, **kw)
        self.assertTrue(str(cm.exception))
        return cm.exception

    def test_truncated(self):
        for n in (0, 7, 127, 128, 140, len(self.data) - 1):
            with self.subTest(n=n):
                self.bad(self.data[:n])

    def test_truncated_crc_fixed(self):
        """cut inside a body with a matching CRC: section out of bounds"""
        e = self.bad(C.fix_crc(self.data[:len(self.data) - 8]))
        self.assertIn("out of bounds", str(e))

    def test_bad_magic(self):
        self.assertIn("magic", str(self.bad(b"XXCART\0\0" + self.data[8:])))

    def test_res_refused_when_cart_only(self):
        d = bytearray(self.data)
        d[0:8] = container.MAGIC_RES
        self.bad(bytes(d), allow_res=False)

    def test_bad_crc(self):
        d = bytearray(self.data)
        d[-1] ^= 0xFF
        self.assertIn("CRC", str(self.bad(bytes(d))))

    def test_bad_version(self):
        d = bytearray(self.data)
        struct.pack_into("<H", d, 8, 2)
        self.bad(bytes(d))

    def test_bad_resolution_and_format(self):
        for off, val in ((12, 720), (16, 2)):
            d = bytearray(self.data)
            if off == 16:
                d[16] = val
            else:
                struct.pack_into("<HH", d, 12, 720, 720)
            with self.subTest(off=off):
                self.bad(bytes(d))

    def test_section_out_of_bounds(self):
        d = bytearray(self.data)
        i, off, size = C.section_entry(self.data, S.MESH)
        struct.pack_into("<I", d, 128 + i * 16 + 8, len(self.data))
        self.assertIn("out of bounds", str(self.bad(C.fix_crc(bytes(d)))))

    def test_section_overlaps_table(self):
        d = bytearray(self.data)
        struct.pack_into("<I", d, 128 + 4, 64)
        self.assertIn("overlaps", str(self.bad(C.fix_crc(bytes(d)))))

    def test_truncated_table(self):
        d = bytearray(self.data[:160])
        d[17] = 9
        self.bad(C.fix_crc(bytes(d)))

    def test_duplicate_section(self):
        cart = container.parse(self.data)
        cart.sections.append(copy.copy(cart.sections[1]))
        with self.assertRaises(ValidationError):
            container.build(cart)
        raw = container.build(cart, check=False)
        self.assertIn("twice", str(self.bad(raw)))

    def test_duplicate_section_legacy_type(self):
        """a type-6 MESH (first bm Studio files) next to a type-8 MESH is a duplicate"""
        cart = container.parse(self.data)
        body = cart.get(S.MESH)
        cart.sections.append(container.Section(S.MESH, body))
        raw = bytearray(container.build(cart, check=False))
        struct.pack_into("<I", raw, 128 + (len(cart.sections) - 1) * 16, S.AUDIO)
        self.bad(C.fix_crc(bytes(raw)))

    def test_legacy_types_read_written_as_new(self):
        raw = bytearray(self.data)
        for i, t in ((1, S.AUDIO), (2, S.OLD_ANIM)):
            struct.pack_into("<I", raw, 128 + i * 16, t)
        cart = container.parse(C.fix_crc(bytes(raw)))
        self.assertEqual([s.type for s in cart.sections], [S.LUA, S.MESH, S.ANIM])
        self.assertEqual([s.stored_type for s in cart.sections], [S.LUA, S.AUDIO, S.OLD_ANIM])
        self.assertEqual(container.build(cart), self.data)

    def test_no_lua(self):
        cart = container.parse(self.data)
        cart.drop(S.LUA)
        with self.assertRaises(ValidationError):
            container.build(cart)

    def test_two_sheets_and_bad_simple_sections(self):
        cases = {
            "two sheets": [(S.SHEET, struct.pack("<HH", 1, 1) + b"\0" * 4),
                           (S.SHEET8, struct.pack("<HHHH", 1, 1, 1, 0) + b"\0" * 4 + b"\x00\x00")],
            "bad sheet size": [(S.SHEET, struct.pack("<HH", 2, 2) + b"\0" * 4)],
            "bad sheet8 runs": [(S.SHEET8, struct.pack("<HHHH", 2, 1, 1, 0) + b"\0" * 4 + b"\x00\x00")],
            "INFO too big": [(S.INFO, b"a" * (16 * 1024 + 1))],
            "INFO not UTF-8": [(S.INFO, b"\xff\xfe")],
            "SPRITES size": [(S.SPRITES, struct.pack("<HH", 1, 0))],
            "zone out of sheet": [(S.SHEET, struct.pack("<HH", 1, 1) + b"\0" * 4),
                                  (S.SPRITES, struct.pack("<HH", 1, 0) + b"z".ljust(16, b"\0") +
                                   struct.pack("<HHHHBBH", 0, 0, 8, 8, 1, 0, 0))],
            "box of no zone": [(S.BOXES, struct.pack("<HH", 1, 0) + b"z".ljust(16, b"\0") +
                                struct.pack("<BBhhHHH", 0, 0, 0, 0, 1, 1, 0))],
            "layers without map": [(S.LAYERS, struct.pack("<HHHH", 1, 1, 2, 0) + b"a".ljust(16, b"\0") +
                                    b"b".ljust(16, b"\0") + b"\0\0")],
            "FLAGS size": [(S.FLAGS, struct.pack("<HH", 0, 1))],
            "MAP size": [(S.MAP, struct.pack("<HH", 2, 2))],
            "COVER size": [(S.COVER, struct.pack("<HH", 600, 1))],
        }
        for what, extra in cases.items():
            with self.subTest(what):
                cart = container.parse(self.data)
                for t, b in extra:
                    cart.put(t, b)
                with self.assertRaises(ValidationError):
                    container.build(cart)
                self.bad(container.build(cart, check=False))

    def test_header_text_too_long(self):
        cart = container.new_cart(b"x", title="t" * 48)
        with self.assertRaises(ValidationError):
            container.build(cart)

    def test_new_cart_layout(self):
        """a fresh cart is laid out like scripts/mkbm.py:pack (same bytes)"""
        mkbm = C.import_script("mkbm")
        lua = b"function _draw() cls(0) end"
        mesh_body = container.parse(self.data).get(S.MESH)
        cart = container.new_cart(lua, 320, 180, "abc", "me")
        cart.put(S.MESH, mesh_body)
        ref = mkbm.pack(lua, title="abc", author="me", res=(320, 180), mesh=mesh_body)
        self.assertEqual(container.build(cart), ref)


class Editions(unittest.TestCase):
    def setUp(self):
        self.cart = container.parse(C.load("anim_figure.bm"))      # 640x360 game

    def test_bm_resolution(self):
        self.cart.header.width = self.cart.header.height = 360
        container.check_path("x.bm", self.cart)
        self.cart.header.width, self.cart.header.height = 300, 200
        with self.assertRaises(ValidationError):
            container.check_path("x.bm", self.cart)

    def test_unknown_extension(self):
        with self.assertRaises(ValidationError):
            container.check_path("x.xyz", self.cart)

    def test_compat_block(self):
        self.cart.header.compat = container.compat_block("pi,rgb30")
        data = container.build(self.cart)
        self.assertEqual(data[120], 3)
        self.assertTrue(data[18] & container.FLAG_COMPAT)
        again = container.parse(data)
        self.assertEqual(container.build(again), data)
        with self.assertRaises(ValidationError):
            container.compat_block("psp")
        with self.assertRaises(ValidationError):
            container.compat_block([])

    def test_project_bit(self):
        with self.assertRaises(ValidationError):
            container.check_path("x.bme", self.cart)
        self.cart.header.flags |= container.FLAG_PROJECT
        container.check_path("x.bme", self.cart)
        with self.assertRaises(ValidationError):
            container.check_path("x.bm", self.cart)

    def test_write_file(self):
        with tempfile.TemporaryDirectory() as d:
            container.write_file(os.path.join(d, "a.bm"), self.cart)
            self.assertTrue(os.path.exists(os.path.join(d, "a.bm")))
            with self.assertRaises(ValidationError):
                container.write_file(os.path.join(d, "a.xyz"), self.cart)
            self.assertFalse(os.path.exists(os.path.join(d, "a.xyz")))

    def test_res_kind(self):
        raw = bytearray(C.load("anim_figure.bm"))
        raw[0:8] = container.MAGIC_RES
        struct.pack_into("<HH", raw, 12, 9, 0)
        with self.assertRaises(ValidationError):               # unknown kind
            container.parse(bytes(raw))
        struct.pack_into("<HH", raw, 12, container.RES_MODEL, 0)
        with self.assertRaises(ValidationError):               # a resource file never holds LUA
            container.parse(bytes(raw))
        cart = container.parse(bytes(raw), deep=False)
        cart.drop(S.LUA)
        res = container.parse(container.build(cart))
        self.assertTrue(res.header.is_res)
        container.check_path("x.bmm", res)
        with self.assertRaises(ValidationError):
            container.check_path("x.bmi", res)
        with self.assertRaises(ValidationError):
            container.check_path("x.bm", res)
        cart.drop(S.MESH)
        cart.drop(S.ANIM)
        cart.put(S.INFO, b"name: x\n")
        with self.assertRaises(ValidationError):               # models file without MESH
            container.build(cart)


class MeshNegative(unittest.TestCase):
    def setUp(self):
        self.body = container.parse(C.load("anim_figure.bm")).get(S.MESH)

    def bad_body(self, body, **kw):
        with self.assertRaises(ValidationError):
            mesh.decode(body, **kw)

    def bad_model(self, m, **kw):
        with self.assertRaises(ValidationError):
            mesh.encode(Mesh([m]), **kw)

    def test_truncated(self):
        for n in (0, 4, 8, 20, len(self.body) - 1):
            with self.subTest(n=n):
                self.bad_body(self.body[:n])

    def test_trailing_bytes(self):
        self.bad_body(self.body + b"\0\0\0\0")

    def test_zero_models(self):
        self.bad_body(struct.pack("<HHI", 0, 64, 0))
        with self.assertRaises(ValidationError):
            mesh.encode(Mesh([]))

    def test_reserved_head(self):
        b = bytearray(self.body)
        b[4] = 1
        self.bad_body(bytes(b))

    def test_index_out_of_range(self):
        b = bytearray(self.body)
        nv = struct.unpack_from("<H", b, 8 + 16)[0]
        struct.pack_into("<H", b, 8 + 24 + nv * 12, nv)
        self.bad_body(bytes(b))
        m = tri_model()
        m.faces[0].c = 3
        self.bad_model(m)

    def test_nan_and_huge_vertex(self):
        for v in (float("nan"), 1e6, -1e7, float("inf")):
            with self.subTest(v=v):
                b = bytearray(self.body)
                struct.pack_into("<f", b, 8 + 24, v)
                self.bad_body(bytes(b))
                m = tri_model()
                m.verts[0][0] = v
                self.bad_model(m)

    def test_empty_and_duplicate_names(self):
        b = bytearray(self.body)
        b[8] = 0
        self.bad_body(bytes(b))
        self.bad_model(tri_model(""))
        with self.assertRaises(ValidationError):
            mesh.encode(Mesh([tri_model("a"), tri_model("a")]))

    def test_long_names(self):
        self.bad_model(tri_model("x" * 16))
        self.bad_model(tri_model("é" * 8))                     # 16 bytes in UTF-8
        mesh.encode(Mesh([tri_model("x" * 15)]))
        body = mesh.encode(Mesh([tri_model("x" * 16)]), keep_long_names=True)
        self.assertEqual(mesh.decode(body).models[0].name, "x" * 16)   # what bm3d.lua writes is read

    def test_oversized_model(self):
        big = tri_model("big", 4097)
        self.bad_model(big)                                     # tool limit 4096 (bm3d.lua:34)
        body = mesh.encode(Mesh([big]), kernel_limits=True)
        self.bad_body(body)                                     # also when read with the tool limits
        self.assertEqual(len(mesh.decode(body, kernel_limits=True).models[0].verts), 4097)
        m = tri_model()
        m.faces = [Face(0, 1, 2)] * 16385
        self.bad_model(m)
        mesh.encode(Mesh([m]), kernel_limits=True)
        self.bad_model(tri_model("huge", 65536), kernel_limits=True)
        with self.assertRaises(ValidationError):
            mesh.encode(Mesh([tri_model(f"m{i}") for i in range(257)]))

    def test_field_ranges(self):
        for attr, val in (("colour", 1 << 32), ("extra", 65536), ("uv8", [0] * 5), ("uv8", [70000] + [0] * 5)):
            with self.subTest(attr=attr):
                m = tri_model()
                setattr(m.faces[0], attr, val)
                self.bad_model(m)
        with self.assertRaises(ValidationError):
            Face.from_px(0, 1, 2, 0, (9000, 0, 0, 0, 0, 0))


class AnimNegative(unittest.TestCase):
    def setUp(self):
        cart = container.parse(C.load("anim_figure.bm"))
        self.cart = cart
        self.body = cart.get(S.ANIM)
        self.mesh = cart.mesh()

    def bad_body(self, body):
        with self.assertRaises(ValidationError):
            anim.decode(body)

    def bad_rig(self, rig):
        with self.assertRaises(ValidationError):
            anim.encode(Anim([rig]))

    def test_good_synthetic(self):
        body = anim.encode(Anim([rig_for(nb=64, nclips=255, nkeys=1)]))
        self.assertEqual(anim.encode(anim.decode(body)), body)
        anim.encode(Anim([rig_for(nkeys=1024)]))

    def test_truncated(self):
        for n in (0, 6, 8, 30, len(self.body) - 1):
            with self.subTest(n=n):
                self.bad_body(self.body[:n])
        self.bad_body(self.body + b"\0" * 4)

    def test_bone_count_65(self):
        self.bad_rig(rig_for(nb=65))
        b = bytearray(self.body)
        struct.pack_into("<H", b, 8 + 16, 65)
        self.bad_body(bytes(b))
        struct.pack_into("<H", b, 8 + 16, 0)
        self.bad_body(bytes(b))

    def test_clips_256(self):
        self.bad_rig(rig_for(nclips=256))
        b = bytearray(self.body)
        struct.pack_into("<H", b, 8 + 18, 256)
        self.bad_body(bytes(b))

    def test_keys(self):
        self.bad_rig(rig_for(nkeys=1025))
        r = rig_for(nkeys=0)
        self.bad_rig(r)
        r = rig_for(nkeys=2)
        r.clips[0].keys[1].time = -0.5                    # decreasing
        self.bad_rig(r)
        r = rig_for(nkeys=1)
        r.clips[0].keys[0].time = 1.01                    # past the length
        self.bad_rig(r)
        r = rig_for()
        r.clips[0].keys[0].pose.append(Pose())            # poses != bones
        self.bad_rig(r)
        r = rig_for()
        r.clips[0].keys[0].pose[0].q[0] = float("nan")
        self.bad_rig(r)

    def test_clip_fields(self):
        for attr, val in (("mode", 5), ("length", 0.0), ("length", 1e6), ("flags", 256)):
            with self.subTest(attr=attr, val=val):
                r = rig_for()
                setattr(r.clips[0], attr, val)
                self.bad_rig(r)

    def test_parent(self):
        r = rig_for(nb=2)
        r.bones[0].parent = 1                             # a later bone
        self.bad_rig(r)
        r = rig_for(nb=2)
        r.bones[1].parent = -2
        self.bad_rig(r)
        b = bytearray(self.body)
        struct.pack_into("<h", b, 8 + 24 + 16, 0)        # bone 0 parent of itself
        self.bad_body(bytes(b))

    def test_vertex_bone_out_of_range(self):
        r = rig_for(nb=2)
        r.vbones[0] = 2
        self.bad_rig(r)

    def test_duplicate_names(self):
        r = rig_for(nb=2)
        r.bones[1].name = "b0"
        self.bad_rig(r)
        r = rig_for(nclips=2)
        r.clips[1].name = "c0"
        self.bad_rig(r)
        with self.assertRaises(ValidationError):
            anim.encode(Anim([rig_for("m"), rig_for("m")]))
        r = rig_for()
        r.bones[0].name = ""
        self.bad_rig(r)

    def test_vertex_mismatch(self):
        a = anim.decode(self.body)
        a.rigs[0].vbones.append(0)
        with self.assertRaises(ValidationError):
            anim.validate_links(a, self.mesh)
        self.cart.put(S.ANIM, anim.encode(a))
        with self.assertRaises(ValidationError):
            container.build(self.cart)
        with self.assertRaises(ValidationError):
            container.parse(container.build(self.cart, check=False))

    def test_rig_without_model(self):
        a = anim.decode(self.body)
        a.rigs[0].model = "nobody"
        with self.assertRaises(ValidationError):
            anim.validate_links(a, self.mesh)
        with self.assertRaises(ValidationError):
            anim.validate_links(a, None)

    def test_zero_rigs(self):
        """an ANIM of 0 rigs is valid on read (src/bm/format.c:306-320); the writer omits it"""
        self.assertEqual(anim.decode(struct.pack("<HHI", 0, 0, 0)).rigs, [])
        self.cart.set_anim(Anim([]))
        self.assertIsNone(self.cart.get(S.ANIM))
        container.parse(container.build(self.cart))

    def test_reserved(self):
        b = bytearray(self.body)
        b[2] = 1
        self.bad_body(bytes(b))
        b = bytearray(self.body)
        b[8 + 22] = 1
        self.bad_body(bytes(b))


if __name__ == "__main__":
    unittest.main()
