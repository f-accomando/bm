"""bm_core.preview / skin / imageio: headless renders and playback data.

PNG and GIF are read back with small readers written here (not bm_core's),
so the writers are checked by an independent decoder.
"""
import hashlib
import math
import struct
import time
import unittest
import zlib

from _common import load

import bm_core as B
from bm_core import imageio, preview as P, skin as S


# ---- independent readers


def read_png(data):
    """-> (w, h, rows of RGB bytes); only 8-bit RGB, any filter"""
    assert data[:8] == b"\x89PNG\r\n\x1a\n", "PNG signature"
    pos, idat, w = 8, b"", None
    while pos < len(data):
        n, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        crc = struct.unpack(">I", data[pos + 8 + n:pos + 12 + n])[0]
        assert zlib.crc32(kind + body) & 0xFFFFFFFF == crc, f"CRC of {kind}"
        if kind == b"IHDR":
            w, h, depth, ctype = struct.unpack(">IIBB", body[:10])
            assert (depth, ctype) == (8, 2)
        elif kind == b"IDAT":
            idat += body
        elif kind == b"IEND":
            break
        pos += 12 + n
    raw = zlib.decompress(idat)
    stride, rows, prev, p = w * 3, [], bytearray(w * 3), 0
    for _ in range(h):
        f, line = raw[p], bytearray(raw[p + 1:p + 1 + stride])
        p += 1 + stride
        for i in range(stride):
            a = line[i - 3] if i >= 3 else 0
            b = prev[i]
            c = prev[i - 3] if i >= 3 else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(bytes(line))
        prev = line
    return w, h, rows


def lzw_decode(data, min_code):
    clear, eoi = 1 << min_code, (1 << min_code) + 1
    size, pos, out = min_code + 1, 0, []
    table = [[i] for i in range(clear)] + [None, None]
    prev = None
    nbits = len(data) * 8
    while pos + size <= nbits:
        code = 0
        for k in range(size):
            code |= (data[(pos + k) >> 3] >> ((pos + k) & 7) & 1) << k
        pos += size
        if code == clear:
            table = table[:eoi + 1]
            size, prev = min_code + 1, None
            continue
        if code == eoi:
            break
        if prev is None:
            entry = table[code]
        elif code < len(table):
            entry = table[code]
            if len(table) < 4096:
                table.append(table[prev] + [entry[0]])
        else:
            entry = table[prev] + [table[prev][0]]
            table.append(entry)
        out += entry
        if len(table) == (1 << size) and size < 12:
            size += 1
        prev = code
    return out


def read_gif(data):
    """-> (w, h, palette, [frame index lists], delays)"""
    assert data[:6] == b"GIF89a"
    w, h, packed = struct.unpack("<HHB", data[6:11])
    pos, pal = 13, []
    if packed & 0x80:
        n = 2 << (packed & 7)
        pal = [tuple(data[pos + i * 3:pos + i * 3 + 3]) for i in range(n)]
        pos += n * 3
    frames, delays = [], []
    while True:
        b = data[pos]
        if b == 0x3B:
            break
        if b == 0x21:
            label = data[pos + 1]
            pos += 2
            if label == 0xF9:
                delays.append(struct.unpack("<H", data[pos + 2:pos + 4])[0])
            while data[pos]:
                pos += data[pos] + 1
            pos += 1
        elif b == 0x2C:
            fw, fh, fp = struct.unpack("<4xHHB", data[pos + 1:pos + 10])
            assert (fw, fh) == (w, h) and fp == 0
            pos += 10
            mc = data[pos]
            pos += 1
            blob = bytearray()
            while data[pos]:
                blob += data[pos + 1:pos + 1 + data[pos]]
                pos += data[pos] + 1
            pos += 1
            frames.append(lzw_decode(bytes(blob), mc))
        else:
            raise AssertionError(f"GIF block {b:#x}")
    return w, h, pal, frames, delays


def village():
    cart = B.parse(load("village_models.bm"))
    tex, inset = P.model_texture(cart)
    return cart.mesh(), cart.anim(), tex, inset


def sha(img):
    return hashlib.sha256(img.rgb_bytes()).hexdigest()


class TestImageFiles(unittest.TestCase):
    def test_png_reads_back(self):
        img = imageio.Image(7, 5, 0x102030)
        img.pixels[3] = 0xFF8001
        w, h, rows = read_png(img.to_png())
        self.assertEqual((w, h), (7, 5))
        self.assertEqual(rows[0][9:12], bytes((0xFF, 0x80, 0x01)))
        self.assertEqual(b"".join(rows), img.rgb_bytes())

    def test_render_png_size_and_pixels(self):
        m, _, tex, inset = village()
        img = P.render_mesh(m.get("house"), size=(96, 64), texture=tex, inset=inset)
        w, h, rows = read_png(P.png_bytes(img))
        self.assertEqual((w, h), (96, 64))
        self.assertEqual(b"".join(rows), img.rgb_bytes())

    def test_lzw_round_trip(self):
        # long and repetitive enough to fill the 4096-code table and clear it
        seq = [(i * 7 + (i >> 5)) % 256 for i in range(30000)] + [3] * 5000
        self.assertEqual(lzw_decode(imageio.lzw_encode(seq), 8), seq)
        self.assertEqual(lzw_decode(imageio.lzw_encode([5]), 8), [5])

    def test_gif_frames_and_pixels(self):
        m, _, tex, inset = village()
        imgs = P.render_turntable(m.get("well"), frames=6, size=(48, 40), texture=tex, inset=inset)
        data = P.gif_bytes(imgs, delay_cs=10)
        w, h, pal, frames, delays = read_gif(data)
        self.assertEqual((w, h, len(frames)), (48, 40, 6))
        self.assertEqual(delays, [10] * 6)
        for img, idx in zip(imgs, frames):
            self.assertEqual(len(idx), 48 * 40)
            got = [pal[i] for i in idx]
            if len({c for i in imgs for c in i.pixels}) <= 256:
                self.assertEqual(got, [img.get(k % 48, k // 48) for k in range(48 * 40)])

    def test_gif_many_colours_quantized(self):
        img = imageio.Image(64, 64, 0, [(x * 4) << 16 | (y * 4) << 8 | (x ^ y) for y in range(64) for x in range(64)])
        _, _, pal, frames, _ = read_gif(P.gif_bytes([img]))
        self.assertEqual(len(frames[0]), 64 * 64)
        err = max(abs(pal[i][0] - img.get(k % 64, k // 64)[0]) for k, i in enumerate(frames[0]))
        self.assertLess(err, 40)

    def test_contact_sheet(self):
        a, b = imageio.Image(4, 3, 0x111111), imageio.Image(4, 3, 0x222222)
        cs = P.contact_sheet([a, b, a], cols=2, gap=1)
        self.assertEqual((cs.w, cs.h), (2 * 4 + 3, 2 * 3 + 3))
        self.assertEqual(cs.get(1, 1), (0x11,) * 3)
        self.assertEqual(cs.get(6, 1), (0x22,) * 3)


class TestRender(unittest.TestCase):
    def test_village_models_cover_pixels(self):
        m, _, tex, inset = village()
        for model in m.models:
            img = P.render_mesh(model, size=(128, 128), texture=tex, inset=inset)
            self.assertGreater(img.covered, 500, model.name)
            img2 = P.render_mesh(model, size=(128, 128))           # no sheet: flat grey
            self.assertGreater(img2.covered, 500, model.name)

    def test_deterministic(self):
        m, _, tex, inset = village()
        a = P.render_mesh(m.get("house"), texture=tex, inset=inset)
        b = P.render_mesh(m.get("house"), texture=tex, inset=inset)
        self.assertEqual(sha(a), sha(b))
        self.assertEqual(a.to_png(), b.to_png())
        ga = P.gif_bytes(P.render_turntable(m.get("tree"), 4, size=(32, 32)))
        gb = P.gif_bytes(P.render_turntable(m.get("tree"), 4, size=(32, 32)))
        self.assertEqual(hashlib.sha256(ga).digest(), hashlib.sha256(gb).digest())

    def test_back_face_culling_follows_winding(self):
        """one triangle, clockwise seen from -z (the camera at yaw 0 looks along +z,
        bm.h:46-47, r3d.c:2097-2101): seen at yaw 0, gone at yaw pi"""
        tri = B.Model("tri", [[-1, -1, 0], [0, 1, 0], [1, -1, 0]], [B.Face(0, 1, 2, 0xFF0000)])
        front = P.render_mesh(tri, size=(32, 32), yaw=0.0, pitch=0.0)
        back = P.render_mesh(tri, size=(32, 32), yaw=math.pi, pitch=0.0)
        self.assertGreater(front.covered, 100)
        self.assertEqual(back.covered, 0)
        flipped = B.Model("tri", tri.verts, [B.Face(0, 2, 1, 0xFF0000)])
        self.assertEqual(P.render_mesh(flipped, size=(32, 32), yaw=0.0, pitch=0.0).covered, 0)

    def test_light_and_emissive(self):
        """facing the camera (normal -z): light = 0.25 + 0.75 * max(0, n.sun),
        sun = norm(-0.4, 0.7, -0.6) (r3d.c:792, 1336-1340); emissive = full"""
        tri = B.Model("tri", [[-1, -1, 0], [0, 1, 0], [1, -1, 0]], [B.Face(0, 1, 2, 0xC8C8C8)])
        img = P.render_mesh(tri, size=(16, 16), yaw=0.0, pitch=0.0)
        sun = 0.6 / math.sqrt(0.16 + 0.49 + 0.36)
        expect = int(0xC8 * (0.25 + 0.75 * sun))
        self.assertEqual(img.get(8, 9), (expect,) * 3)
        glow = B.Model("tri", tri.verts, [B.Face(0, 1, 2, 0x40C8C8C8)])
        self.assertEqual(P.render_mesh(glow, size=(16, 16), yaw=0.0, pitch=0.0).get(8, 9), (0xC8,) * 3)

    def test_near_plane_and_ortho(self):
        m, _, _, _ = village()
        img = P.render_mesh(m.get("crates"), size=(64, 64), ortho=True)
        self.assertGreater(img.covered, 300)

    def test_bad_inputs(self):
        m, a, _, _ = village()
        v, r = m.get("villager"), a.get("villager")
        with self.assertRaises(ValueError):
            P.render_mesh(v, size=(0, 10))
        with self.assertRaises(ValueError):
            P.render_mesh(v, size=(10,))
        with self.assertRaises(ValueError):
            P.render_mesh(v, fov=5)
        with self.assertRaises(ValueError):
            P.render_mesh(v, yaw=float("nan"))
        with self.assertRaises(B.ValidationError):
            P.render_mesh("villager")
        broken = B.Model("x", [[0, 0, 0]], [B.Face(0, 1, 2)])
        with self.assertRaises(B.ValidationError):
            P.render_mesh(broken)
        with self.assertRaises(B.ValidationError):
            P.render_anim(v, r, "dance")
        with self.assertRaises(B.ValidationError):
            P.render_anim(v, r, 9)
        with self.assertRaises(B.ValidationError):
            P.render_anim(m.get("house"), r, "walk")
        with self.assertRaises(ValueError):
            P.render_anim(v, r, "walk", fps=0)
        with self.assertRaises(ValueError):
            P.render_turntable(v, frames=0)
        with self.assertRaises(ValueError):
            P.gif_bytes([])
        with self.assertRaises(ValueError):
            P.contact_sheet([imageio.Image(2, 2), imageio.Image(3, 2)])


class TestPlayback(unittest.TestCase):
    def test_walk_frames_differ_and_loop_closes(self):
        m, a, tex, inset = village()
        v, r = m.get("villager"), a.get("villager")
        imgs, frs = P.render_anim(v, r, "walk", fps=12, size=(64, 64), include_end=True, texture=tex,
                                  inset=inset)
        clip = S.find_clip(r, "walk")
        self.assertTrue(clip.loop)
        self.assertEqual(len(imgs), round(clip.length * 12) + 1)
        self.assertGreater(len({sha(i) for i in imgs[:-1]}), len(imgs) // 2)
        self.assertNotEqual(sha(imgs[0]), sha(imgs[len(imgs) // 2]))
        self.assertEqual(sha(imgs[-1]), sha(imgs[0]))
        self.assertEqual(frs[-1].mats, frs[0].mats)

    def test_playback_data(self):
        m, a, _, _ = village()
        v, r = m.get("villager"), a.get("villager")
        frs = S.playback(r, "walk", fps=12, model=v)
        self.assertEqual(len(frs), round(S.find_clip(r, "walk").length * 12))
        for fr in frs:
            self.assertEqual(len(fr.mats), len(r.bones))
            self.assertEqual(len(fr.verts), len(v.verts))
            lo, hi = fr.bounds
            self.assertTrue(all(lo[k] < hi[k] for k in range(3)))
            for mat in fr.mats:     # rigid: the 3x3 part stays a rotation
                rows = [mat[0:3], mat[4:7], mat[8:11]]
                for i in range(3):
                    self.assertAlmostEqual(sum(x * x for x in rows[i]), 1.0, places=5)
        self.assertNotEqual(frs[0].mats, frs[3].mats)
        d = frs[0].to_dict()
        self.assertEqual(set(d), {"t", "q", "tr", "mats", "heads", "tails", "bounds"})

    def test_skin_formula(self):
        """M = M_parent * T(head + t) * R(q) * T(-head) (runtime.c:2273): a child
        turned 90 deg about z at its head, its parent moved by (0, 0, 1)"""
        rig = B.Rig("m", [B.Bone("root", -1, [0, 0, 0], [0, 1, 0]), B.Bone("arm", 0, [0, 1, 0], [1, 1, 0])],
                    [0, 1], [])
        s = math.sqrt(0.5)
        mats = S.skin(rig, [[0, 0, 0, 1], [0, 0, s, s]], [[0, 0, 1], [0, 0, 0]])
        x, y, z = S.apply(mats[1], (1, 1, 0))       # the arm's tail: (1,1,0) -> turned to (0,2,0), +z
        self.assertAlmostEqual(x, 0, places=6)
        self.assertAlmostEqual(y, 2, places=6)
        self.assertAlmostEqual(z, 1, places=6)

    def test_pose_at_rules(self):
        """loop wrap blends last -> first; step holds; not looping clamps
        (runtime.c:2230-2271)"""
        k0 = B.Key(0.0, [B.Pose([0, 0, 0, 1], [0, 0, 0])])
        k1 = B.Key(0.5, [B.Pose([0, 0, 0, 1], [2, 0, 0])])
        lin = B.Clip("a", 1.0, [k0, k1], mode=0, flags=1)
        self.assertAlmostEqual(S.pose_at(lin, 0.25, 1)[1][0][0], 1.0)
        self.assertAlmostEqual(S.pose_at(lin, 0.75, 1)[1][0][0], 1.0)     # half way back to key 0
        self.assertAlmostEqual(S.pose_at(lin, 1.25, 1)[1][0][0], 1.0)     # t mod length
        self.assertAlmostEqual(S.pose_at(lin, -0.75, 1)[1][0][0], 1.0)
        smooth = B.Clip("s", 1.0, [k0, k1], mode=1, flags=1)
        self.assertAlmostEqual(S.pose_at(smooth, 0.125, 1)[1][0][0], 2 * (0.0625 * 2.5))
        step = B.Clip("p", 1.0, [k0, k1], mode=2, flags=0)
        self.assertEqual(S.pose_at(step, 0.4, 1)[1][0][0], 0)
        self.assertEqual(S.pose_at(step, 0.9, 1)[1][0][0], 2)             # past the last key: held
        self.assertEqual(S.pose_at(step, 9.0, 1)[1][0][0], 2)

    def test_still_clip_not_looping(self):
        cart = B.parse(load("anim_figure.bm"))
        m, a = cart.mesh().models[0], cart.anim().rigs[0]
        imgs, frs = P.render_anim(m, a, "still", fps=12, size=(48, 48), include_end=True)
        self.assertEqual(len({sha(i) for i in imgs}), 1)               # one key: always the same pose
        imgs, _ = P.render_anim(m, a, "wave", fps=12, size=(48, 48), include_end=True, show_bones=True)
        self.assertEqual(sha(imgs[-1]), sha(imgs[0]))

    def test_speed(self):
        """~1500 triangles at 256x256, 24 frames: a few seconds at most"""
        verts, faces, n, k = [], [], 26, 30
        for i in range(n + 1):
            th = math.pi * i / n
            for j in range(k):
                ph = 2 * math.pi * j / k
                verts.append([math.sin(th) * math.cos(ph), math.cos(th), math.sin(th) * math.sin(ph)])
        for i in range(n):
            for j in range(k):
                a, b = i * k + j, i * k + (j + 1) % k
                faces += [B.Face(a, b, a + k, 0xC08040), B.Face(b, b + k, a + k, 0x40A0C0)]
        ball = B.Model("ball", verts, faces)
        t = time.perf_counter()
        imgs = P.render_turntable(ball, frames=24, size=(256, 256))
        dt = time.perf_counter() - t
        self.assertGreater(imgs[0].covered, 30000)
        self.assertLess(dt, 60.0)
        print(f"\n[preview] {len(faces)} triangles, 24 frames 256x256: {dt:.2f} s", end=" ")


if __name__ == "__main__":
    unittest.main()
