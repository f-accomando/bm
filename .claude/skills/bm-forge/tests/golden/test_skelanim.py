"""bm_core.skelgen / animgen / shapes (phase 2): skeletons and clips from
descriptions, checked through the same playback maths as the runtime
(bm_core.skin, port of src/bm/runtime.c pose_at / skin).

Nothing here runs the kernel or bm Animator: the generated cartridge is only
checked by bm_core (parse / validate / check_path).
"""
import copy
import json
import math
import os
import struct
import tempfile
import unittest

from _common import SKILL, load

import bm_core as B
from bm_core import anim as A, animgen as AG, preview as P, shapes, skelgen as SG, skin as S
from bm_core.binary import f32

EX = os.path.join(SKILL, "examples")


def ex(name):
    with open(os.path.join(EX, name + ".json"), encoding="utf-8") as f:
        return json.load(f)


def build():
    rig = SG.skeleton(ex("humanoid"))
    model, owners = shapes.load_boxes(ex("humanoid_boxes"))
    rig = SG.bind(model, rig, strategy="nearest_bone_part")
    reports = []
    rig = AG.add_clips(rig, [ex("idle"), ex("walk"), ex("jump")], reports)
    cart = B.new_cart(b"function _draw() cls(0) end\n", 320, 180, "forge test")
    AG.write_anim(cart, [rig], B.Mesh([model]))
    return cart, model, rig, owners, reports


def two_bone():
    return SG.skeleton({"model": "m", "bones": [
        {"name": "root", "parent": None, "head": [0, 0, 0], "tail": [0, 1, 0]},
        {"name": "a", "parent": "root", "head": [0, 1, 0], "tail": [0, 2, 0]},
        {"name": "b", "parent": "root", "head": [0, 1, 0], "tail": [1, 1, 0]}]})


def bad(test, fn, *words):
    with test.assertRaises(B.ValidationError) as cm:
        fn()
    for w in words:
        test.assertIn(w, str(cm.exception))
    return cm.exception


class Skeletons(unittest.TestCase):
    def test_village_layout(self):
        """examples/humanoid.json = the villager rig of the village golden (names, parents, rest)"""
        vil = B.parse(load("village_models.bm")).anim().get("villager")
        rig = SG.skeleton(ex("humanoid"))
        self.assertEqual([b.name for b in rig.bones], [b.name for b in vil.bones])
        self.assertEqual([b.parent for b in rig.bones], [b.parent for b in vil.bones])
        for a, b in zip(rig.bones, vil.bones):
            for x, y in zip(a.head + a.tail, b.head + b.tail):
                self.assertEqual(f32(x), y)

    def test_describe_roundtrip(self):
        vil = B.parse(load("village_models.bm")).anim().get("villager")
        again = SG.skeleton(SG.describe(vil))
        self.assertEqual([b.to_dict() for b in again.bones], [b.to_dict() for b in vil.bones])

    def test_parents_first(self):
        rig = SG.skeleton({"model": "m", "bones": [
            {"name": "hand", "parent": "arm", "head": [0, 2, 0], "tail": [0, 3, 0]},
            {"name": "arm", "parent": "root", "head": [0, 1, 0], "tail": [0, 2, 0]},
            {"name": "root", "parent": None, "head": [0, 0, 0], "tail": [0, 1, 0]}]})
        self.assertEqual([b.name for b in rig.bones], ["root", "arm", "hand"])
        self.assertEqual([b.parent for b in rig.bones], [-1, 0, 1])

    def test_rejected(self):
        root = {"name": "root", "parent": None, "head": [0, 0, 0], "tail": [0, 1, 0]}

        def sk(*bones, **top):
            d = {"model": "m", "bones": [root] + list(bones)}
            d.update(top)
            return lambda: SG.skeleton(d)
        bad(self, sk({"name": "x", "parent": "ghost", "head": [0, 0, 0], "tail": [0, 1, 0]}), "orphan")
        bad(self, sk({"name": "x", "parent": "y", "head": [0, 0, 0], "tail": [0, 1, 0]},
                     {"name": "y", "parent": "x", "head": [0, 0, 0], "tail": [0, 1, 0]}), "cycle")
        bad(self, sk({"name": "x", "parent": "x", "head": [0, 0, 0], "tail": [0, 1, 0]}), "own parent")
        bad(self, sk({"name": "x", "parent": None, "head": [0, 0, 0], "tail": [0, 1, 0]}), "root")
        bad(self, sk(dict(root)), "twice")
        bad(self, sk({"name": "x", "parent": "root", "head": [0, 0, 0], "tail": [0, 0, 0]}), "length")
        bad(self, sk({"name": "x", "parent": "root", "head": [0, float("nan"), 0], "tail": [0, 1, 0]}), "finite")
        bad(self, sk({"name": "x", "parent": "root", "head": [0, 2e6, 0], "tail": [0, 1, 0]}), "1e6")
        bad(self, sk({"name": "x" * 16, "parent": "root", "head": [0, 0, 0], "tail": [0, 1, 0]}), "at most 15")
        bad(self, sk({"name": "x", "parent": "root", "head": [0, 0, 0], "tail": [0, 1, 0], "lenght": 1}),
            "unknown keys")
        bad(self, sk({"name": "x", "parent": "root", "head": [0, 0, 0], "tail": [0, 1, 0], "length": 1,
                      "direction": [0, 1, 0]}), "either")
        bad(self, sk({"name": "x", "parent": "root", "head": [0, 0, 0], "length": 1, "direction": [0, 0, 0]}),
            "zero")
        many = [{"name": f"b{i}", "parent": "root", "head": [0, 0, 0], "tail": [0, 1, 0]} for i in range(64)]
        bad(self, sk(*many), "65 bones", "64")
        bad(self, lambda: SG.skeleton({"bones": [root]}), "model")

    def test_assign_and_bind(self):
        rig = SG.skeleton(ex("humanoid"))
        model, owners = shapes.load_boxes(ex("humanoid_boxes"))
        self.assertLessEqual(len(model.faces), 300)
        names = [b.name for b in rig.bones]
        part = SG.assign_vertices(model, rig, "nearest_bone_part")
        self.assertEqual([names[i] for i in part], owners)
        per = SG.assign_vertices(model, rig, "nearest_bone")
        self.assertEqual(len(per), len(model.verts))
        self.assertEqual(per, SG.assign_vertices(model, rig, "nearest_bone"))     # deterministic
        self.assertNotEqual(per, part)      # per vertex splits boxes at the joints: why "part" exists
        bad(self, lambda: SG.assign_vertices(model, rig, "weights"), "strategy")
        bad(self, lambda: SG.bind(model, rig, vbones=part[:-1]), "vertices")
        other = B.Model("other", model.verts, model.faces)
        bad(self, lambda: SG.bind(other, rig), "by name")
        bound = SG.bind(model, rig, part)
        self.assertEqual(bound.vbones, part)
        self.assertEqual(rig.vbones, [])     # the input rig is not changed


class Clips(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cart, cls.model, cls.rig, cls.owners, cls.reports = build()
        cls.data = B.build(cls.cart)

    def frames(self, name, **kw):
        return S.playback(self.rig, name, fps=12, model=self.model, **kw)

    def test_reports(self):
        idle, walk, jump = self.reports
        self.assertEqual((idle["mode"], idle["baked"], idle["keys"]), (1, False, 2))
        self.assertEqual((walk["mode"], walk["baked"], walk["keys"]), (1, False, 4))
        self.assertEqual((jump["mode"], jump["baked"], jump["keys"]), (0, True, 18))
        self.assertIn("mixed curves", jump["reason"])
        c = {c.name: c for c in self.rig.clips}
        self.assertTrue(c["idle"].loop and c["walk"].loop and not c["jump"].loop)

    def test_write_read_equal(self):
        """write -> read -> equal (values as f32), and parse -> build is byte-identical"""
        back = B.parse(self.data)
        self.assertEqual(B.build(back), self.data)
        rig = back.anim().get("humanoid")
        self.assertEqual(rig.vbones, self.rig.vbones)
        self.assertEqual([b.name for b in rig.bones], [b.name for b in self.rig.bones])
        for a, b in zip(self.rig.clips, rig.clips):
            self.assertEqual((a.name, a.mode, a.flags, f32(a.length)), (b.name, b.mode, b.flags, b.length))
            self.assertEqual(len(a.keys), len(b.keys))
            for ka, kb in zip(a.keys, b.keys):
                self.assertEqual(f32(ka.time), kb.time)
                for pa, pb in zip(ka.pose, kb.pose):
                    self.assertEqual([f32(x) for x in pa.q + pa.t], pb.q + pb.t)
        self.assertEqual(back.mesh().get("humanoid").to_dict()["faces"], self.model.to_dict()["faces"])

    def test_bm_file_passes_check(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "forge.bm")
            B.write_file(p, self.cart)
            cart = B.read_file(p)                 # parse (deep) + check_path
            info = B.validate(cart)
            self.assertIn(9, info)
            A.validate_links(info[9], info[8])
            self.assertEqual(B.edition_of_cart(cart), "bm")

    def test_duration(self):
        for name, nf in (("idle", 24), ("walk", 12), ("jump", 18)):
            c = S.find_clip(self.rig, name)
            self.assertEqual(c.length, nf / 12)
            self.assertEqual(len(self.frames(name)), nf)
            for k in c.keys:
                self.assertAlmostEqual(k.time * 12, round(k.time * 12), places=9)   # on the 12 fps grid

    def test_loop_closure(self):
        nb = len(self.rig.bones)
        for name in ("idle", "walk"):
            c = S.find_clip(self.rig, name)
            q0, t0 = S.pose_at(c, 0.0, nb)
            q1, t1 = S.pose_at(c, c.length - 1e-5, nb)
            for i in range(nb):
                self.assertAlmostEqual(abs(sum(a * b for a, b in zip(q0[i], q1[i]))), 1.0, places=6)
                for a, b in zip(t0[i], t1[i]):
                    self.assertAlmostEqual(a, b, places=5)
            fr = self.frames(name, include_end=True)
            self.assertEqual(fr[-1].verts, fr[0].verts)

    def test_jump_up_then_back(self):
        hy = [fr.heads[0][1] for fr in self.frames("jump")]
        base = hy[0]
        self.assertAlmostEqual(base, 0.9, places=6)
        top = max(range(len(hy)), key=lambda i: hy[i])
        self.assertEqual(top, 8)
        self.assertAlmostEqual(hy[8] - base, 0.6, places=5)
        self.assertTrue(all(hy[i] < hy[i + 1] for i in range(4, 8)))           # up
        self.assertTrue(all(hy[i] > hy[i + 1] for i in range(8, 13)))          # back down
        self.assertAlmostEqual(hy[-1], base, places=6)
        self.assertAlmostEqual(S.pose_at(S.find_clip(self.rig, "jump"), 99.0, 7)[1][0][1], 0.0)  # clamps
        # the baked curves: ease-out 4 -> 8 at u = 0.5, ease-in 8 -> 13 at u = 0.4
        self.assertAlmostEqual(hy[6] - base, 0.6 * 0.75, places=5)
        self.assertAlmostEqual(hy[10] - base, 0.6 * (1 - 0.16), places=5)

    def test_walk_alternates_legs(self):
        fr = self.frames("walk")
        L, R = 5, 6
        dz = [f.tails[L][2] - f.tails[R][2] for f in fr]
        self.assertLess(dz[0], -0.5)       # left foot forward (-z, the way the eyes look)
        self.assertGreater(dz[6], 0.5)     # right foot forward
        self.assertAlmostEqual(dz[3], 0.0, places=5)
        self.assertAlmostEqual(dz[9], 0.0, places=5)
        for i in range(12):
            self.assertAlmostEqual(dz[i], -dz[(i + 6) % 12], places=5)      # a mirror half a stride later
        arm = [f.tails[3][2] - f.tails[4][2] for f in fr]
        self.assertGreater(arm[0], 0.2)    # left arm back while left leg forward
        hy = [f.heads[0][1] for f in fr]
        self.assertGreater(hy[3], hy[0])

    def test_determinism(self):
        again = build()[0]
        self.assertEqual(B.build(again), self.data)
        g1 = P.anim_gif(self.model, self.rig, "walk", size=(48, 48))
        g2 = P.anim_gif(self.model, self.rig, "walk", size=(48, 48))
        self.assertEqual(g1, g2)

    def test_preview_frames(self):
        imgs, frs = P.render_anim(self.model, self.rig, "walk", size=(64, 64), yaw=0.9, pitch=0.2)
        self.assertEqual(len(imgs), 12)
        self.assertTrue(all(i.covered > 200 for i in imgs))
        self.assertNotEqual(imgs[0].pixels, imgs[6].pixels)
        self.assertEqual(P.contact_sheet(imgs, cols=6).to_png()[:8], b"\x89PNG\r\n\x1a\n")


class ClipRules(unittest.TestCase):
    rig = two_bone()

    def clip(self, d, rep=None):
        base = {"name": "c", "loop": False, "keys": []}
        base.update(d)
        return AG.clip(base, self.rig, rep)

    def test_euler(self):
        q = AG.euler_quat(90, 0, 0)
        m = S.skin(A.Rig("m", [A.Bone("r", -1, [0, 0, 0], [0, 1, 0])], [0]), [q], [[0, 0, 0]])[0]
        self.assertEqual([round(x, 6) + 0 for x in S.apply(m, (0, 1, 0))], [0, 0, 1])
        q = AG.euler_quat(0, 90, 0)
        m = S.skin(A.Rig("m", [A.Bone("r", -1, [0, 0, 0], [0, 1, 0])], [0]), [q], [[0, 0, 0]])[0]
        self.assertEqual([round(x, 6) + 0 for x in S.apply(m, (0, 0, 1))], [1, 0, 0])

    def test_fill_native_linear_is_exact(self):
        """a bone missing from a key is filled from its own track; linear stays native and the
        runtime's playback equals the intended track on every frame"""
        d = {"frames": 6, "curve": "linear", "keys": [
            {"frame": 0, "pose": {"a": {"rot": [0, 0, 0]}, "b": {"move": [0, 0, 0]}}},
            {"frame": 2, "pose": {"b": {"move": [0, 1, 0]}}},
            {"frame": 6, "pose": {"a": {"rot": [90, 0, 0]}, "b": {"move": [0, 0, 0]}}}]}
        rep = {}
        c = self.clip(d, rep)
        self.assertEqual((rep["mode"], rep["baked"], rep["keys"]), (0, False, 3))
        for f in range(7):
            q, t = S.pose_at(c, f / 12, 3)
            ang = 2 * math.degrees(math.atan2(q[1][0], q[1][3]))
            self.assertAlmostEqual(ang, 15 * f, places=4)
        smooth = dict(d, curve="smooth")
        rep = {}
        self.clip(smooth, rep)
        self.assertTrue(rep["baked"])
        self.assertIn("filled", rep["reason"])

    def test_step_and_ease_baked(self):
        rep = {}
        c = self.clip({"frames": 8, "keys": [
            {"frame": 0, "curve": "step", "pose": {"b": {"move": [0, 0, 0]}}},
            {"frame": 4, "curve": "linear", "pose": {"b": {"move": [0, 1, 0]}}},
            {"frame": 8, "pose": {"b": {"move": [0, 3, 0]}}}]}, rep)
        self.assertEqual((rep["mode"], rep["baked"], rep["keys"]), (0, True, 9))
        ys = [S.pose_at(c, f / 12, 3)[1][2][1] for f in range(9)]
        self.assertEqual([round(y, 6) for y in ys], [0, 0, 0, 0, 1, 1.5, 2, 2.5, 3])
        rep = {}
        self.clip({"frames": 4, "curve": "ease-in", "keys": [
            {"frame": 0, "pose": {"b": {"move": [0, 0, 0]}}}, {"frame": 4, "pose": {"b": {"move": [0, 4, 0]}}}]},
            rep)
        self.assertIn("not a runtime mode", rep["reason"])
        rep = {}
        c = self.clip({"frames": 4, "curve": "step", "keys": [
            {"frame": 0, "pose": {"b": {"move": [0, 0, 0]}}}, {"frame": 4, "pose": {"b": {"move": [0, 4, 0]}}}]},
            rep)
        self.assertEqual((rep["mode"], rep["baked"]), (2, False))

    def test_snap(self):
        rep = {}
        c = self.clip({"length": 0.55, "keys": [{"time": 0.0, "pose": {}}, {"time": 0.1, "pose": {}}]}, rep)
        self.assertEqual(c.keys[1].time, 1 / 12)
        self.assertEqual(c.length, 7 / 12)
        self.assertEqual(len(rep["snapped"]), 2)
        bad(self, lambda: self.clip({"keys": [{"time": 0.1, "pose": {}}, {"frame": 1, "pose": {}}]}),
            "two keys on frame 1")

    def test_loop_rules(self):
        k0 = {"frame": 0, "pose": {"a": {"rot": [10, 0, 0]}}}
        bad(self, lambda: self.clip({"loop": True, "keys": [k0]}), "needs 'frames'")
        c = self.clip({"loop": True, "frames": 6, "keys": [k0, {"frame": 6, "pose": {"a": {"rot": [10, 0, 0]}}}]})
        self.assertEqual(len(c.keys), 2)
        bad(self, lambda: self.clip({"loop": True, "frames": 6, "keys": [
            k0, {"frame": 6, "pose": {"a": {"rot": [20, 0, 0]}}}]}), "must equal frame 0")
        bad(self, lambda: self.clip({"loop": True, "frames": 12, "keys": [
            {"frame": 0, "pose": {"a": {"rot": [100, 0, 0]}}},
            {"frame": 6, "pose": {"a": {"rot": [-90, 0, 0]}}}]}), "180", "short way")

    def test_out_of_range(self):
        def one(spec, **kw):
            return lambda: self.clip(dict({"keys": [{"frame": 0, "pose": {"a": spec}}]}, **kw))
        bad(self, one({"move": [0, 1e7, 0]}), "move")
        bad(self, one({"rot": [float("nan"), 0, 0]}), "rot")
        bad(self, one({"q": [0, 0, 0, 2]}), "length 2")
        bad(self, one({"rot": [0, 0, 0], "q": [0, 0, 0, 1]}), "one rotation")
        bad(self, one({"axis": [0, 0, 0], "angle": 10}), "axis is zero")
        bad(self, one({"spin": 1}), "unknown keys")
        bad(self, lambda: self.clip({"keys": [{"frame": 0, "pose": {"ghost": {}}}]}), "not in the rig")
        bad(self, lambda: self.clip({"keys": [{"frame": -1, "pose": {}}]}), "frame -1")
        bad(self, lambda: self.clip({"keys": [{"frame": 1.5, "pose": {}}]}), "integer")
        bad(self, lambda: self.clip({"frames": 4, "keys": [{"frame": 5, "pose": {}}]}), "past the length")
        bad(self, lambda: self.clip({"length": 0.01, "keys": [{"frame": 0, "pose": {}}]}), "one frame")
        bad(self, lambda: self.clip({"curve": "bounce", "keys": [{"frame": 0, "pose": {}}]}), "curve")
        bad(self, lambda: self.clip({"keys": [{"frame": 0, "pose": {"a": {"rot": [0, 0, 0]}}},
                                              {"frame": 3, "pose": {"a": {"rot": [180, 0, 0]}}}]}), "180")
        bad(self, lambda: self.clip({"keys": [{"frame": 0, "mirror": 0}]}), "mirror itself")
        bad(self, lambda: self.clip({"keys": [{"frame": 0}]}), "'pose' or 'mirror'")
        bad(self, lambda: self.clip({"name": "x" * 16, "keys": [{"frame": 0, "pose": {}}]}), "at most 15")

    def test_key_limits(self):
        lin = {"curve": "linear", "keys": [{"frame": f, "pose": {}} for f in range(1025)]}
        bad(self, lambda: self.clip(lin), "1025 keys", "1024")
        ok = self.clip({"curve": "linear", "keys": [{"frame": f, "pose": {}} for f in range(1024)]})
        self.assertEqual(len(ok.keys), 1024)
        baked = {"frames": 1100, "curve": "ease-in", "keys": [{"frame": 0, "pose": {}}, {"frame": 1100, "pose": {}}]}
        bad(self, lambda: self.clip(baked), "after baking", "1024")

    def test_clip_limit(self):
        rig = self.rig
        one = {"frames": 1, "keys": [{"frame": 0, "pose": {}}]}
        rig = AG.add_clips(rig, [dict(one, name=f"c{i}") for i in range(255)])
        self.assertEqual(len(rig.clips), 255)
        bad(self, lambda: AG.add_clips(rig, [dict(one, name="extra")]), "256 clips", "255")
        bad(self, lambda: AG.add_clips(self.rig, [dict(one, name="d"), dict(one, name="d")]), "twice")

    def test_write_needs_bound_rig(self):
        cart = B.new_cart(b"function _draw() end\n", 320, 180)
        model, _ = shapes.load_boxes(ex("humanoid_boxes"))
        rig = SG.skeleton(ex("humanoid"))
        with self.assertRaises(B.ValidationError):
            AG.write_anim(cart, [rig], B.Mesh([model]))       # no vertex bones yet


if __name__ == "__main__":
    unittest.main()
