"""Headless preview of MESH models and ANIM clips: a software rasterizer in
pure Python (no numpy), PNG / GIF out (imageio.py), playback maths from
skin.py (port of src/bm/runtime.c pose_at / skin).

What follows the console (r3d, src/bm/r3d.c) and what is approximated:

- Camera: world -> camera rows of view_setup (r3d.c:975-985), screen
  sx = w/2 + x f / z, sy = h/2 - y f / z (r3d.c:1022-1023), focal
  f = (w/2) / tan(fov/2) (r3d.c:857), near plane 0.1 (r3d.h:93), triangles
  clipped there. Orthographic is a preview extra (not in r3d).
- Object turn: yaw/pitch turn the MODEL about y then x (a game's draw3d
  turn, r3d.c:926-929, approximated), the camera stays looking along +z
  (cam yaw = pitch = 0); the light is fixed in the world.
- Back faces: culled when (b-a) x (c-a) <= 0 on the screen, y down
  (r3d.c:2097-2101): the visible side is the one where a, b, c are
  clockwise (bm.h:46-47, CLAUDE.md invariant).
- Light: one sun, default direction (-0.4, 0.7, -0.6) and ambient 0.25,
  white sun/sky/ground (r3d_init, r3d.c:792-793, r3d_light 893-899);
  light = ambient + (1 - ambient) * max(0, n . sun) (light_fast,
  r3d.c:1336-1340; with white sky/ground the sky term is the ambient).
  Face normal (b-a) x (c-a) (r3d_mesh_normals, r3d.c:2626-2629), turned by
  the bone of the face's first vertex (r3d.c:2171-2175).
  Colour = rgb * light, clamped (vertex_rgb / shade_rgb, r3d.c:938-950,
  1104-1120). Lamps, fog, rim light, specular (GLOSSY) are NOT drawn.
- FLAT only: every face is lit once at its normal. The console draws smooth
  (Gouraud, R3D_SMOOTH) when the game asks for it (r3d.c:1906, 2136): the
  preview does not (approximation).
- Material bits (r3d.h:74-90): EMISSIVE = full colour, no light
  (r3d.c:2103, 2118-2119); SCREEN = every other pixel ((x + y) & 1, the
  phase is NON VERIFICATO); FLAT is what the preview always does; GLOSSY
  ignored; LOD: a face shows when R3D_LOD_SHOWS(colour, detail)
  (r3d.h:90), detail 3 by default (NON VERIFICATO which level a game uses).
- Lit models (flags bit 0): an untextured face gets the corner lights
  /128 (bm_model_face_light, r3d.c:2109-2133) averaged over its 3 corners
  (the console blends them: approximation) and no sun; a textured face of a
  lit model gets its one 5-6-5 light, no sun (r3d.c:2170, NON VERIFICATO
  past that line).
- Textures: decoded from the cartridge's SHEET or SHEET8 (texture_of),
  nearest texel, perspective-correct, corners pulled in by the MESH inset
  (uv_inset, runtime.c:2080-2098, 2134, 2146), texels with alpha < 128 not
  drawn (bm.h:23, "skip transparent texels" r3d.c:442), lit by the grey of
  the face light (lit_grey, r3d.c:1122-1126, 2199). Without a sheet a
  textured face is drawn flat grey 0xA0A0A0 (its RGB bits are dropped by
  the runtime, runtime.c:2140). Texel filtering/dither of r3d: NOT copied.
"""
import math
import struct

from . import imageio
from . import mesh as meshmod
from . import skin as skinmod
from .errors import ValidationError, need
from .imageio import Image, contact_sheet, gif_bytes, png_bytes, save_gif  # noqa: F401  (re-exported)

NEAR = 0.1                                  # R3D_NEAR, r3d.h:93
SUN = (-0.4, 0.7, -0.6)                     # r3d_init, r3d.c:792
AMBIENT = 0.25                              # r3d.c:792
NO_TEX_GREY = 0xA0A0A0
BACKGROUND = 0x1C1C24
BONE_COLOUR, HEAD_COLOUR = 0xFFE040, 0xFF4040
EMISSIVE, SCREEN = 0x40000000, 0x10000000   # r3d.h:86-88
SHEET, SHEET8 = 2, 5                        # bm.h:143-156


# ---- the sprite sheet as a texture


class Texture:
    """w x h texels, each 0xRRGGBB or -1 (alpha < 128, transparent, bm.h:23)"""

    def __init__(self, w, h, texels):
        self.w, self.h, self.texels = w, h, texels


def _rgba(r, g, b, a):
    return (r << 16 | g << 8 | b) if a >= 128 else -1


def decode_sheet(body):
    """SHEET (2) body: u16 w, h, then RGBA8888 (bm.h:23)"""
    from .sections import check_sheet
    info = check_sheet(body)
    w, h = info["w"], info["h"]
    t = [_rgba(*body[4 + i * 4:8 + i * 4]) for i in range(w * h)]
    return Texture(w, h, t)


def decode_sheet8(body):
    """SHEET8 (5) body: palette + runs, port of sheet8_walk
    (src/bm/format.c:37-63); checked first by sections.check_sheet8"""
    from .sections import check_sheet8
    info = check_sheet8(body)
    w, h, ncol = info["w"], info["h"], info["ncol"]
    pal = [_rgba(*body[8 + i * 4:12 + i * 4]) for i in range(ncol)]
    q, n, out = 8 + ncol * 4, w * h, []
    while len(out) < n:
        t = body[q]
        q += 1
        if t < 128:
            out += [pal[i] for i in body[q:q + t + 1]]
            q += t + 1
        else:
            out += [pal[body[q]]] * (t - 126)
            q += 1
    return Texture(w, h, out)


def texture_of(cart):
    """the cartridge's sheet as a Texture (SHEET8 or SHEET), or None"""
    b = cart.get(SHEET8)
    if b is not None:
        return decode_sheet8(b)
    b = cart.get(SHEET)
    return decode_sheet(b) if b is not None else None


def _uv_inset(uv, inset):
    """port of uv_inset (runtime.c:2080-2098)"""
    uv = list(uv)
    for axis in range(2):
        vals = [uv[k * 2 + axis] for k in range(3)]
        lo, hi = min(vals), max(vals)
        if hi - lo <= 2 * inset:
            continue
        mid = (lo + hi) * 0.5
        for k in range(3):
            t = uv[k * 2 + axis]
            if t < mid - inset:
                uv[k * 2 + axis] = t + inset
            elif t > mid + inset:
                uv[k * 2 + axis] = t - inset
    return uv


# ---- maths


def _rot(yaw, pitch):
    """model turn: about y by yaw, then about x by pitch, pitch > 0 tipping
    the top towards the camera (an approximation of a game's draw3d turn,
    whose order is R = Rz Ry Rx, r3d.c:926-929)"""
    cy, sy, cp, sp = math.cos(yaw), math.sin(yaw), math.cos(pitch), math.sin(pitch)
    ry = (cy, 0.0, sy, 0.0, 1.0, 0.0, -sy, 0.0, cy)
    rx = (1.0, 0.0, 0.0, 0.0, cp, sp, 0.0, -sp, cp)
    return tuple(sum(rx[r * 3 + k] * ry[k * 3 + c] for k in range(3)) for r in range(3) for c in range(3))


def _mul3(m, v):
    return (m[0] * v[0] + m[1] * v[1] + m[2] * v[2], m[3] * v[0] + m[4] * v[1] + m[5] * v[2],
            m[6] * v[0] + m[7] * v[1] + m[8] * v[2])


def _norm(v):
    l = math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2])
    return (v[0] / l, v[1] / l, v[2] / l) if l > 1e-12 else (0.0, 0.0, 0.0)


def _lod_shows(c, d):
    """R3D_LOD_SHOWS (r3d.h:90)"""
    k = c >> 24 & 3
    return d < k if (c >> 26 & 1) else d >= k


class View:
    """a camera fitted to a sphere: perspective (fov in degrees, r3d.c:857)
    or orthographic"""

    def __init__(self, w, h, centre, radius, fov=60.0, ortho=False, margin=1.08):
        if not (isinstance(fov, (int, float)) and 10 <= fov <= 150):
            raise ValueError("fov must be 10..150 degrees (r3d_camera clamps there, r3d.c:855-856)")
        self.w, self.h, self.ortho = w, h, ortho
        self.hw, self.hh = w * 0.5, h * 0.5
        self.f = self.hw / math.tan(fov * math.pi / 360.0)
        r = max(radius, 1e-6) * margin
        hmin = min(self.hw, self.hh)
        self.centre = centre
        if ortho:
            self.scale = hmin / r
            self.dist = r * 2 + 1.0
        else:
            self.dist = r * math.sqrt(1 + (self.f / hmin) ** 2)
            self.scale = None

    def cam(self, p):
        """world -> camera: the camera at centre - dist along +z, looking +z"""
        return (p[0] - self.centre[0], p[1] - self.centre[1], p[2] - self.centre[2] + self.dist)

    def project(self, c):
        """camera -> (sx, sy, inverse w, depth key); larger key = nearer"""
        if self.ortho:
            return (self.hw + c[0] * self.scale, self.hh - c[1] * self.scale, 1.0, 1e9 - c[2])
        iz = 1.0 / c[2]
        return (self.hw + c[0] * self.f * iz, self.hh - c[1] * self.f * iz, iz, iz)


def _sphere(points):
    lo = [min(p[k] for p in points) for k in range(3)]
    hi = [max(p[k] for p in points) for k in range(3)]
    c = tuple((lo[k] + hi[k]) * 0.5 for k in range(3))
    r = math.sqrt(max((p[0] - c[0]) ** 2 + (p[1] - c[1]) ** 2 + (p[2] - c[2]) ** 2 for p in points))
    return c, r


# ---- rasterizer


def _clip_near(poly, near):
    """Sutherland-Hodgman against z >= near; each vertex (x, y, z, u, v)"""
    out = []
    n = len(poly)
    for i in range(n):
        a, b = poly[i], poly[(i + 1) % n]
        ina, inb = a[2] >= near, b[2] >= near
        if ina:
            out.append(a)
        if ina != inb:
            t = (near - a[2]) / (b[2] - a[2])
            out.append(tuple(a[k] + (b[k] - a[k]) * t for k in range(5)))
    return out


def _tri(cbuf, zbuf, w, h, P, colour, tex, screen):
    """one screen triangle P = 3 x (sx, sy, iw, key, u*iw, v*iw).
    colour: 0xRRGGBB (flat) or, with tex, the light 0..1 (float) on texels"""
    (x0, y0, w0, k0, u0, v0), (x1, y1, w1, k1, u1, v1), (x2, y2, w2, k2, u2, v2) = P
    area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)
    if area == 0:
        return 0
    ia = 1.0 / area
    ex1, ey1, ex2, ey2 = x1 - x0, y1 - y0, x2 - x0, y2 - y0

    def grad(a0, a1, a2):
        return ((a1 - a0) * ey2 - (a2 - a0) * ey1) * ia, ((a2 - a0) * ex1 - (a1 - a0) * ex2) * ia

    kdx, kdy = grad(k0, k1, k2)
    if tex is not None:
        wdx, wdy = grad(w0, w1, w2)
        udx, udy = grad(u0, u1, u2)
        vdx, vdy = grad(v0, v1, v2)
        tw, th, texels = tex.w, tex.h, tex.texels
        light = colour
        cache = {}
    pts = sorted(((x0, y0), (x1, y1), (x2, y2)), key=lambda p: p[1])
    (ax, ay), (bx, by), (cx, cy) = pts
    ys, ye = max(0, math.ceil(ay - 0.5)), min(h, math.ceil(cy - 0.5))
    drawn = 0
    for y in range(ys, ye):
        yc = y + 0.5
        xl = ax + (cx - ax) * (yc - ay) / (cy - ay)
        if yc < by:
            xr = ax + (bx - ax) * (yc - ay) / (by - ay)
        else:
            xr = bx + (cx - bx) * (yc - by) / (cy - by) if cy > by else bx
        if xl > xr:
            xl, xr = xr, xl
        xs, xe = max(0, math.ceil(xl - 0.5)), min(w, math.ceil(xr - 0.5))
        if xs >= xe:
            continue
        dx, dy = xs + 0.5 - x0, yc - y0
        k = k0 + kdx * dx + kdy * dy
        idx = y * w + xs
        if tex is None:
            if screen:
                for x in range(xs, xe):
                    if (x + y) & 1 == 0 and k > zbuf[idx]:
                        zbuf[idx] = k
                        cbuf[idx] = colour
                        drawn += 1
                    k += kdx
                    idx += 1
            else:
                for _ in range(xs, xe):
                    if k > zbuf[idx]:
                        zbuf[idx] = k
                        cbuf[idx] = colour
                        drawn += 1
                    k += kdx
                    idx += 1
            continue
        wi = w0 + wdx * dx + wdy * dy
        uu = u0 + udx * dx + udy * dy
        vv = v0 + vdx * dx + vdy * dy
        for x in range(xs, xe):
            if k > zbuf[idx] and not (screen and (x + y) & 1):
                tu = int(uu / wi)
                tv = int(vv / wi)
                tu = 0 if tu < 0 else tw - 1 if tu >= tw else tu
                tv = 0 if tv < 0 else th - 1 if tv >= th else tv
                c = texels[tv * tw + tu]
                if c >= 0:
                    s = cache.get(c)
                    if s is None:
                        r = min(255, int((c >> 16 & 255) * light))
                        g = min(255, int((c >> 8 & 255) * light))
                        b = min(255, int((c & 255) * light))
                        s = cache[c] = r << 16 | g << 8 | b
                    zbuf[idx] = k
                    cbuf[idx] = s
                    drawn += 1
            k += kdx
            wi += wdx
            uu += udx
            vv += vdx
            idx += 1
    return drawn


def _shade(rgb, k):
    """rgb * k per channel, clamped (vertex_rgb, r3d.c:1104-1120); k = (r, g, b)"""
    return (min(255, int((rgb >> 16 & 255) * k[0])) << 16 | min(255, int((rgb >> 8 & 255) * k[1])) << 8 |
            min(255, int((rgb & 255) * k[2])))


def _line(cbuf, w, h, a, b, colour):
    (x0, y0), (x1, y1) = a, b
    n = int(max(abs(x1 - x0), abs(y1 - y0))) + 1
    for i in range(n + 1):
        t = i / n
        x, y = int(x0 + (x1 - x0) * t), int(y0 + (y1 - y0) * t)
        if 0 <= x < w and 0 <= y < h:
            cbuf[y * w + x] = colour


def _check_size(size):
    if not (isinstance(size, (tuple, list)) and len(size) == 2 and
            all(isinstance(s, int) and not isinstance(s, bool) and 1 <= s <= 4096 for s in size)):
        raise ValueError(f"size must be (w, h) integers 1..4096, not {size!r}")
    return size


def _check_model(model):
    if not isinstance(model, meshmod.Model):
        raise ValidationError(f"not a Model: {type(model).__name__}", "preview")
    meshmod.validate_model(model, kernel_limits=True, keep_long_names=True)


class _Scene:
    """the fixed parts of one or more renders of one model: camera, light,
    turn, texture"""

    def __init__(self, model, size, yaw, pitch, fov, ortho, texture, inset, light, ambient, detail, background,
                 points):
        self.model, (self.w, self.h) = model, size
        self.rot = _rot(yaw, pitch)
        self.texture, self.inset = texture, inset
        self.light = _norm(light)
        need(self.light != (0.0, 0.0, 0.0), "light direction is zero", "preview")
        if not (isinstance(ambient, (int, float)) and 0 <= ambient <= 1):
            raise ValueError("ambient must be 0..1")
        self.ambient, self.detail, self.background = float(ambient), detail, background
        c, r = _sphere(points)                      # in model space: the same for every turn
        self.view = View(self.w, self.h, _mul3(self.rot, c), r, fov, ortho)
        self.uv = []
        for f in model.faces:
            self.uv.append(_uv_inset(f.uv_px(), inset) if f.textured else None)
        self.normals = []
        vs = model.verts
        for f in model.faces:
            a, b, cc = vs[f.a], vs[f.b], vs[f.c]
            u = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
            v = (cc[0] - a[0], cc[1] - a[1], cc[2] - a[2])
            self.normals.append(_norm((u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2],
                                       u[0] * v[1] - u[1] * v[0])))

    def render(self, verts, mats=None, vbones=None, bones=None):
        """verts: the (posed) model vertices; mats/vbones: bone matrices to turn
        the face normals; bones: [(head, tail)] to draw on top"""
        w, h, view, rot, m = self.w, self.h, self.view, self.rot, self.model
        img = Image(w, h, self.background)
        cbuf, zbuf = img.pixels, [-1.0] * (w * h)
        cam = [view.cam(_mul3(rot, p)) for p in verts]
        lit = m.lit
        L, A = self.light, self.ambient
        drawn = 0
        for fi, f in enumerate(m.faces):
            col = f.colour
            if not _lod_shows(col, self.detail):
                continue
            ca, cb, cc = cam[f.a], cam[f.b], cam[f.c]
            uv = self.uv[fi]
            if uv is None:
                uv = (0, 0, 0, 0, 0, 0)
            poly = [(ca[0], ca[1], ca[2], uv[0], uv[1]), (cb[0], cb[1], cb[2], uv[2], uv[3]),
                    (cc[0], cc[1], cc[2], uv[4], uv[5])]
            if not view.ortho and min(ca[2], cb[2], cc[2]) < NEAR:
                poly = _clip_near(poly, NEAR)
                if len(poly) < 3:
                    continue
            scr = []
            for (x, y, z, u, v) in poly:
                sx, sy, iw, key = view.project((x, y, z))
                scr.append((sx, sy, iw, key, u * iw, v * iw))
            # back faces (r3d.c:2097-2101), y down on the screen
            (ax, ay), (bx, by), (qx, qy) = scr[0][:2], scr[1][:2], scr[2][:2]
            if (bx - ax) * (qy - ay) - (by - ay) * (qx - ax) <= 0:
                continue
            textured = f.textured and self.texture is not None
            emissive = bool(col & EMISSIVE)
            screen = bool(col & SCREEN)
            # the light of the face
            if emissive:
                k = (1.0, 1.0, 1.0)
            elif lit:
                l9 = f.light(True)
                k = tuple(sum(l9[c * 3 + ch] for c in range(3)) / (3 * 128.0) for ch in range(3))
            else:
                n = self.normals[fi]
                if mats is not None:
                    n = skinmod.rotate(mats[vbones[f.a]], n)
                n = _mul3(rot, n)
                d = n[0] * L[0] + n[1] * L[1] + n[2] * L[2]
                kk = A + (1.0 - A) * (d if d > 0 else 0.0)
                k = (kk, kk, kk)
            if textured:
                grey = min(1.0, (k[0] + k[1] + k[2]) / 3.0)      # lit_grey, r3d.c:1122-1126
                tex, colour = self.texture, grey
            else:
                tex = None
                colour = _shade(NO_TEX_GREY if f.textured else col & 0xFFFFFF, k)
            for j in range(1, len(scr) - 1):
                drawn += _tri(cbuf, zbuf, w, h, (scr[0], scr[j], scr[j + 1]), colour, tex, screen)
        img.covered = sum(1 for z in zbuf if z != -1.0)
        if bones:
            for head, tail in bones:
                pa = view.project(view.cam(_mul3(rot, head)))
                pb = view.project(view.cam(_mul3(rot, tail)))
                _line(cbuf, w, h, pa[:2], pb[:2], BONE_COLOUR)
                hx, hy = int(pa[0]), int(pa[1])
                for dy in (-1, 0, 1):
                    for dx in (-1, 0, 1):
                        if 0 <= hx + dx < w and 0 <= hy + dy < h:
                            cbuf[(hy + dy) * w + hx + dx] = HEAD_COLOUR
        return img


def _scene(model, size, yaw, pitch, fov, ortho, texture, inset, light, ambient, detail, background, points):
    _check_model(model)
    _check_size(size)
    if texture is not None and not isinstance(texture, Texture):
        raise ValueError("texture must be a preview.Texture (texture_of(cart)) or None")
    for name, val in (("yaw", yaw), ("pitch", pitch)):
        if not (isinstance(val, (int, float)) and math.isfinite(val)):
            raise ValueError(f"{name} must be a finite number")
    return _Scene(model, tuple(size), yaw, pitch, fov, ortho, texture, inset, light, ambient, detail, background,
                  points)


def render_mesh(model, size=(256, 256), yaw=0.6, pitch=0.35, fov=60.0, ortho=False, texture=None, inset=0.25,
                light=SUN, ambient=AMBIENT, detail=3, background=BACKGROUND, rig=None, show_bones=False):
    """one image of a model at rest. yaw/pitch (radians) turn the model
    (pitch > 0 tips its top towards the camera: seen from above). texture =
    texture_of(cart) or None (flat grey for textured faces). inset = the
    MESH inset in sheet px (Mesh.inset / 256). With a rig and show_bones the
    rest bones are drawn on top. -> Image (img.covered = pixels drawn)"""
    _check_model(model)
    sc = _scene(model, size, yaw, pitch, fov, ortho, texture, inset, light, ambient, detail, background,
                model.verts)
    bones = None
    if rig is not None and show_bones:
        skinmod.check_link(model, rig)
        bones = [(b.head, b.tail) for b in rig.bones]
    return sc.render(model.verts, bones=bones)


def render_turntable(model, frames=12, size=(128, 128), pitch=0.35, **kw):
    """`frames` images, the model turned a full circle about y -> [Image]
    (gif_bytes(images) for a GIF)"""
    if not (isinstance(frames, int) and 1 <= frames <= 360):
        raise ValueError("frames must be an integer 1..360")
    _check_model(model)
    # the camera is fitted to the model's sphere, which does not move with the turn
    out = []
    for i in range(frames):
        out.append(render_mesh(model, size, yaw=2 * math.pi * i / frames, pitch=pitch, **kw))
    return out


def render_anim(model, rig, clip, fps=12, size=(128, 128), yaw=0.6, pitch=0.35, include_end=False,
                show_bones=False, fov=60.0, ortho=False, texture=None, inset=0.25, light=SUN, ambient=AMBIENT,
                detail=3, background=BACKGROUND):
    """the frames of a clip (name or number from 1) at `fps`, with the pose
    and skin maths of runtime.c (skin.py). One camera for all frames (fitted
    to every skinned frame), so frames compare pixel by pixel.
    -> (images, frames) with frames = skin.playback(...) data"""
    _check_model(model)
    frs = skinmod.playback(rig, clip, fps, model, include_end)
    pts = [v for fr in frs for v in fr.verts] + [p for fr in frs for p in fr.heads + fr.tails]
    sc = _scene(model, size, yaw, pitch, fov, ortho, texture, inset, light, ambient, detail, background, pts)
    imgs = []
    for fr in frs:
        bones = list(zip(fr.heads, fr.tails)) if show_bones else None
        imgs.append(sc.render(fr.verts, fr.mats, rig.vbones, bones))
    return imgs, frs


def anim_gif(model, rig, clip, fps=12, **kw):
    """render_anim -> GIF bytes (delay round(100 / fps) cs, loops forever)"""
    imgs, _ = render_anim(model, rig, clip, fps, **kw)
    return gif_bytes(imgs, delay_cs=max(1, int(round(100 / fps))))


def model_texture(cart):
    """(Texture or None, inset in sheet px) of a cartridge (inset = MESH u16 / 256,
    bm_mesh_inset src/bm/format.c:192-195)"""
    b = cart.get(8)
    inset = struct.unpack_from("<H", b, 2)[0] / 256.0 if b is not None and len(b) >= 4 else 0.25
    return texture_of(cart), inset


__all__ = ["Texture", "texture_of", "decode_sheet", "decode_sheet8", "model_texture", "View", "render_mesh",
           "render_turntable", "render_anim", "anim_gif", "Image", "contact_sheet", "png_bytes", "gif_bytes",
           "save_gif", "imageio"]
