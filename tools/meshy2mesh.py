#!/usr/bin/env python3
"""
meshy2mesh: a picture becomes a 3D model for the console through Meshy
(meshy.ai, image-to-3D) and lands in a .bm as bm Studio and bm Animator
keep models. Meshy returns a real mesh (thousands of triangles, a
texture); here it is scaled to the console's units, flipped to its frame,
cut down to what the Pi draws at 60 fps and either textured (its texture
in the cartridge's sprite sheet) or painted flat (one colour per face,
sampled from the texture) - the blocky style of the other models.

  tools/meshy2mesh.py IMAGE -o OUT.bm [--name NAME] [--polycount 2000]
                      [--height 2] [--flat] [--max-tris 3000] [--grid 32]
                      (IMAGE: a file, or an https URL Meshy fetches itself)
  tools/meshy2mesh.py --glb MODEL.glb -o OUT.bm ...   (a mesh already made)

OUT.bm: a new cartridge with bm Studio's viewer (textured, its own sheet)
or an existing one with the model added or replaced (flat colours: the
cartridge keeps its sheet). The key: MESHY_API_KEY in the environment.
The .glb Meshy made stays next to the output (OUT.glb) to redo the
conversion offline with --glb.
"""
import argparse
import base64
import json
import os
import re
import struct
import sys
import time
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
sys.path.insert(0, os.path.join(ROOT, "scripts"))
sys.path.insert(0, HERE)
import bmmesh  # noqa: E402
import mkbm  # noqa: E402

API = "https://api.meshy.ai/openapi/v1"
SHEET = 256


# ------------------------------------------------------------------ Meshy

def api(method, path, key, body=None):
    req = urllib.request.Request(API + path, method=method,
                                 headers={"Authorization": "Bearer " + key, "Content-Type": "application/json"},
                                 data=json.dumps(body).encode() if body is not None else None)
    try:
        with urllib.request.urlopen(req, timeout=60) as r:
            return json.loads(r.read().decode())
    except urllib.error.HTTPError as e:
        raise SystemExit(f"meshy {method} {path}: HTTP {e.code}: {e.read().decode(errors='replace')[:300]}")


def image_data_uri(path):
    ext = os.path.splitext(path)[1].lower()
    media = {".png": "image/png", ".jpg": "image/jpeg", ".jpeg": "image/jpeg", ".webp": "image/webp"}.get(ext)
    if not media:
        raise SystemExit(f"{path}: an image (png, jpg, webp)")
    return f"data:{media};base64," + base64.standard_b64encode(open(path, "rb").read()).decode()


def meshy_image_to_3d(image, key, polycount, texture=True, symmetry="auto"):
    """-> the task's result: model_urls, texture_urls... (SUCCEEDED).
    image: a file, or an https URL Meshy fetches itself"""
    url = image if re.match(r"^https?://", image) else image_data_uri(image)
    body = {"image_url": url, "ai_model": "meshy-5", "topology": "triangle",
            "target_polycount": polycount, "should_remesh": True, "should_texture": texture,
            "enable_pbr": False, "symmetry_mode": symmetry}
    r = api("POST", "/image-to-3d", key, body)
    task = r.get("result")
    if not task:
        raise SystemExit(f"meshy: no task id in {r}")
    print(f"meshy: task {task}", flush=True)
    while True:
        t = api("GET", f"/image-to-3d/{task}", key)
        status = t.get("status")
        print(f"  {status} {t.get('progress', 0)}%", flush=True)
        if status == "SUCCEEDED":
            return t
        if status in ("FAILED", "CANCELED", "EXPIRED"):
            raise SystemExit(f"meshy: {status}: {t.get('task_error', {}).get('message', '')}")
        time.sleep(5)


def download(url, path):
    with urllib.request.urlopen(url, timeout=120) as r, open(path, "wb") as f:
        f.write(r.read())


# ------------------------------------------------------------------ the glTF

def glb_scene(data):
    """every triangle of a .glb with the node transforms applied:
    (positions, uvs or None, colours or None, triangles [(a, b, c, image)])
    where image is the index of the base colour image or None"""
    js, bin_ = bmmesh.read_glb(data)
    positions, uvs, colours, tris = [], [], [], []
    have_uv = have_col = False

    def material_image(prim):
        mi = prim.get("material")
        if mi is None:
            return None, (1, 1, 1, 1)
        mat = js["materials"][mi].get("pbrMetallicRoughness", {})
        factor = tuple(mat.get("baseColorFactor", (1, 1, 1, 1)))
        tex = mat.get("baseColorTexture")
        if tex is None:
            return None, factor
        return js["textures"][tex["index"]].get("source"), factor

    def visit(ni, parent):
        node = js["nodes"][ni]
        m = bmmesh._mat_mul(parent, bmmesh._node_matrix(node))
        if "mesh" in node:
            for prim in js["meshes"][node["mesh"]]["primitives"]:
                if prim.get("mode", 4) != 4:
                    continue
                nonlocal have_uv, have_col
                attrs = prim["attributes"]
                pos = bmmesh._accessor(js, bin_, attrs["POSITION"])
                uv = bmmesh._accessor(js, bin_, attrs["TEXCOORD_0"]) if "TEXCOORD_0" in attrs else None
                col = bmmesh._accessor(js, bin_, attrs["COLOR_0"]) if "COLOR_0" in attrs else None
                image, factor = material_image(prim)
                base = len(positions)
                for i, p in enumerate(pos):
                    x, y, z = p
                    positions.append((m[0] * x + m[4] * y + m[8] * z + m[12],
                                      m[1] * x + m[5] * y + m[9] * z + m[13],
                                      m[2] * x + m[6] * y + m[10] * z + m[14]))
                    uvs.append(uv[i][:2] if uv else None)
                    c = col[i] if col else (1, 1, 1)
                    colours.append((c[0] * factor[0], c[1] * factor[1], c[2] * factor[2]))
                have_uv = have_uv or uv is not None
                have_col = have_col or col is not None
                if "indices" in prim:
                    idx = [t[0] for t in bmmesh._accessor(js, bin_, prim["indices"])]
                else:
                    idx = list(range(len(pos)))
                for k in range(0, len(idx) - 2, 3):
                    tris.append((base + idx[k], base + idx[k + 1], base + idx[k + 2], image))
        for ch in node.get("children", []):
            visit(ch, m)

    ident = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
    scene = js.get("scenes", [{}])[js.get("scene", 0)]
    roots = scene.get("nodes", list(range(len(js.get("nodes", [])))))
    for ni in roots:
        visit(ni, ident)
    images = []
    for img in js.get("images", []):
        blob = None
        if "bufferView" in img:
            bv = js["bufferViews"][img["bufferView"]]
            blob = bin_[bv.get("byteOffset", 0):bv.get("byteOffset", 0) + bv["byteLength"]]
        images.append(blob)
    return positions, (uvs if have_uv else None), (colours if have_col else None), tris, images


def decode_image(blob):
    """-> (w, h, rgba) from a PNG (ours) or, with Pillow, anything else"""
    if blob is None:
        return None
    if blob[:8] == b"\x89PNG\r\n\x1a\n":
        try:
            return mkbm.read_png("texture", blob)
        except SystemExit:
            pass
    try:
        from PIL import Image
        import io
        im = Image.open(io.BytesIO(blob)).convert("RGBA")
        return im.width, im.height, im.tobytes()
    except ImportError:
        print("meshy2mesh: the texture is not an 8-bit PNG and Pillow is not installed (pip install pillow):"
              " flat grey", file=sys.stderr)
        return None


def sample(img, u, v):
    w, h, rgba = img
    x = min(w - 1, max(0, int((u % 1.0) * w)))
    y = min(h - 1, max(0, int((v % 1.0) * h)))
    i = (y * w + x) * 4
    return rgba[i], rgba[i + 1], rgba[i + 2]


def resize(img, size):
    """box filter down to size x size (the sheet)"""
    w, h, rgba = img
    out = bytearray(size * size * 4)
    for y in range(size):
        y0, y1 = y * h // size, max(y * h // size + 1, (y + 1) * h // size)
        for x in range(size):
            x0, x1 = x * w // size, max(x * w // size + 1, (x + 1) * w // size)
            r = g = b = n = 0
            for yy in range(y0, y1):
                row = yy * w
                for xx in range(x0, x1):
                    i = (row + xx) * 4
                    r += rgba[i]; g += rgba[i + 1]; b += rgba[i + 2]; n += 1
            o = (y * size + x) * 4
            out[o], out[o + 1], out[o + 2], out[o + 3] = r // n, g // n, b // n, 255
    return size, size, bytes(out)


# ------------------------------------------------------------------ to the console

def convert(data, name, height, flat, max_tris, grid):
    """a .glb -> (model dict for bmmesh.encode, sheet (w, h, rgba) or None)"""
    positions, uvs, colours, tris, images = glb_scene(data)
    if not tris:
        raise SystemExit("the .glb has no triangles")
    decoded = {}
    for i, blob in enumerate(images):
        decoded[i] = decode_image(blob)
    textured = not flat and uvs is not None and any(decoded.get(t[3]) for t in tris)
    # the console's frame: glTF's z flips (and the winding with it), the
    # model stands on y = 0 centred on x and z, scaled to `height`
    lo = [min(p[k] for p in positions) for k in range(3)]
    hi = [max(p[k] for p in positions) for k in range(3)]
    s = height / max(hi[1] - lo[1], 1e-6)
    cx, cz = (lo[0] + hi[0]) / 2, (lo[2] + hi[2]) / 2
    pts = [((p[0] - cx) * s, (p[1] - lo[1]) * s, -(p[2] - cz) * s) for p in positions]
    faces = []
    for a, b, c, image in tris:
        img = decoded.get(image)
        if textured and img:
            colour = None
        elif img and uvs and uvs[a] and uvs[b] and uvs[c]:
            u = (uvs[a][0] + uvs[b][0] + uvs[c][0]) / 3
            v = (uvs[a][1] + uvs[b][1] + uvs[c][1]) / 3
            colour = sample(img, u, v)
        elif colours:
            colour = tuple(int(bmmesh.linear_to_srgb((colours[a][k] + colours[b][k] + colours[c][k]) / 3))
                           for k in range(3))
        else:
            colour = (138, 138, 154)
        faces.append((a, c, b, colour, image))               # the winding flipped with z
    # too many triangles for the console: the vertices snap to a grid
    # (flat colours only: the texture would tear)
    if len(faces) > max_tris or len(pts) > bmmesh.MAX_VERTS:
        if textured:
            print(f"meshy2mesh: {len(faces)} triangles: more than --max-tris {max_tris}, painted flat instead",
                  file=sys.stderr)
            textured = False
            faces = [(a, b, c, col or sample(decoded[im], *(tuple((uvs[a][k] + uvs[b][k] + uvs[c][k]) / 3 for k in range(2)))), im)
                     for a, b, c, col, im in faces]
        pts, faces = cluster(pts, faces, grid)
        if len(faces) > bmmesh.MAX_FACES or len(pts) > bmmesh.MAX_VERTS:
            raise SystemExit(f"still {len(faces)} triangles and {len(pts)} vertices: a smaller --grid")
    sheet = None
    out_faces = []
    if textured:
        # one image on the sheet: the first base colour image used
        used = [im for _, _, _, _, im in faces if decoded.get(im)]
        first = used[0]
        sheet = resize(decoded[first], SHEET)
        for a, b, c, col, im in faces:
            if im == first and uvs[a] and uvs[b] and uvs[c]:
                uv = []
                for i in (a, b, c):
                    uv += [min(1.0, max(0.0, uvs[i][0])) * SHEET, min(1.0, max(0.0, uvs[i][1])) * SHEET]
                out_faces.append((a, b, c, bmmesh.TEXTURED, tuple(uv)))
            else:
                colour = sample(decoded[im], *(tuple((uvs[a][k] + uvs[b][k] + uvs[c][k]) / 3 for k in range(2)))) \
                    if decoded.get(im) and uvs[a] and uvs[b] and uvs[c] else (col or (138, 138, 154))
                out_faces.append((a, b, c, colour[0] << 16 | colour[1] << 8 | colour[2], None))
    else:
        for a, b, c, col, _ in faces:
            out_faces.append((a, b, c, col[0] << 16 | col[1] << 8 | col[2], None))
    return {"name": name, "verts": pts, "faces": out_faces}, sheet


def cluster(pts, faces, grid):
    """vertex clustering on a grid of `grid` cells along the largest side:
    the low-poly, blocky model of the console"""
    lo = [min(p[k] for p in pts) for k in range(3)]
    hi = [max(p[k] for p in pts) for k in range(3)]
    cell = max(hi[k] - lo[k] for k in range(3)) / max(1, grid)
    cells, sums, counts, index = {}, [], [], []
    for p in pts:
        key = tuple(int((p[k] - lo[k]) / cell) for k in range(3))
        i = cells.get(key)
        if i is None:
            i = len(sums)
            cells[key] = i
            sums.append([0.0, 0.0, 0.0])
            counts.append(0)
        for k in range(3):
            sums[i][k] += p[k]
        counts[i] += 1
        index.append(i)
    new_pts = [tuple(sums[i][k] / counts[i] for k in range(3)) for i in range(len(sums))]
    merged = {}
    for a, b, c, col, im in faces:
        ia, ib, ic = index[a], index[b], index[c]
        if len({ia, ib, ic}) < 3:
            continue
        key = (ia, ib, ic)
        if key in merged:
            m = merged[key]
            m[3] = tuple((m[3][k] * m[5] + col[k]) // (m[5] + 1) for k in range(3))
            m[5] += 1
        else:
            merged[key] = [ia, ib, ic, col, im, 1]
    # the vertices used, renumbered
    used = sorted({i for k in merged for i in k})
    renum = {i: n for n, i in enumerate(used)}
    out = [(renum[m[0]], renum[m[1]], renum[m[2]], m[3], m[4]) for m in merged.values()]
    return [new_pts[i] for i in used], out


def write_cart(out, model, sheet, title):
    """as img2mesh: a new cartridge with the viewer, or the model added to
    an existing one (whose sheet stays: flat colours only there)"""
    import img2mesh
    if os.path.exists(out):
        data = open(out, "rb").read()
        secs = dict(bmmesh.cart_sections(data))
        models, inset = bmmesh.decode(secs[bmmesh.SEC_MESH]) if bmmesh.SEC_MESH in secs else ([], 0.25)
        if any(f[3] == bmmesh.TEXTURED for f in model["faces"]):
            raise SystemExit("a textured model cannot go into an existing cartridge (its sheet is the game's): --flat")
        models = [m for m in models if m["name"] != model["name"]] + [model]
        secs[bmmesh.SEC_MESH] = bmmesh.encode(models, inset)
        if bmmesh.SEC_ANIM in secs:
            rigs = [r for r in bmmesh.decode_anim(secs[bmmesh.SEC_ANIM]) if r[0] != model["name"]]
            if rigs:
                secs[bmmesh.SEC_ANIM] = bmmesh.encode_anim(rigs)
            else:
                del secs[bmmesh.SEC_ANIM]
        order = [t for t, _ in bmmesh.cart_sections(data)]
        if bmmesh.SEC_MESH not in order:
            order.append(bmmesh.SEC_MESH)
        sections = [(t, secs[t]) for t in order if t in secs]
        table_size = 16 * len(sections)
        offset = 128 + table_size
        table, bodies = b"", b""
        for typ, body in sections:
            table += struct.pack("<IIII", typ, offset + len(bodies), len(body), 0)
            bodies += body + b"\0" * ((-len(body)) % 4)
        after = table + bodies
        header = bytearray(data[:128])
        header[17] = len(sections)
        struct.pack_into("<I", header, 20, mkbm.crc32(after))
        with open(out, "wb") as f:
            f.write(bytes(header) + after)
        return "added to"
    mesh = bmmesh.encode([model], 0.25)
    with open(out, "wb") as f:
        f.write(mkbm.pack(img2mesh.viewer_lua(), sheet=sheet, title=title, author="meshy2mesh", mesh=mesh))
    return "written"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("image", nargs="?", help="the picture (png, jpg, webp), or its https URL")
    ap.add_argument("--glb", help="a .glb already made: no call to Meshy")
    ap.add_argument("-o", "--out", required=True, help="the .bm to write or to add the model to")
    ap.add_argument("--name", help="the model's name (up to 15 letters; default: from the file name)")
    ap.add_argument("--polycount", type=int, default=2000, help="triangles asked of Meshy (default 2000)")
    ap.add_argument("--height", type=float, default=2.0, help="the model's height in blocks (default 2)")
    ap.add_argument("--flat", action="store_true", help="one colour per face instead of the texture")
    ap.add_argument("--max-tris", type=int, default=3000, help="over this, the vertices snap to a grid (default 3000)")
    ap.add_argument("--grid", type=int, default=32, help="cells along the largest side when snapping (default 32)")
    ap.add_argument("--no-texture", action="store_true", help="ask Meshy for the shape only (cheaper)")
    ap.add_argument("--symmetry", default="auto", choices=["auto", "on", "off"])
    a = ap.parse_args()
    src = a.glb or a.image
    if not src:
        ap.error("an image, or --glb")
    stem = os.path.splitext(os.path.basename(src.split("?")[0]))[0]
    name = (a.name or re.sub(r"[^A-Za-z0-9_]", "", stem) or "model")[:15]
    if a.glb:
        data = open(a.glb, "rb").read()
    else:
        key = os.environ.get("MESHY_API_KEY")
        if not key:
            raise SystemExit("MESHY_API_KEY: the key from meshy.ai, in the environment")
        task = meshy_image_to_3d(a.image, key, a.polycount, texture=not a.no_texture, symmetry=a.symmetry)
        url = (task.get("model_urls") or {}).get("glb")
        if not url:
            raise SystemExit(f"meshy: no glb in {task.get('model_urls')}")
        glb = os.path.splitext(a.out)[0] + ".glb"
        download(url, glb)
        print(f"{glb}: Meshy's model")
        data = open(glb, "rb").read()
    model, sheet = convert(data, name, a.height, a.flat or os.path.exists(a.out), a.max_tris, a.grid)
    what = write_cart(a.out, model, sheet, name)
    textured = sum(1 for f in model["faces"] if f[3] == bmmesh.TEXTURED)
    print(f"{a.out}: {what} the model {name}: {len(model['verts'])} vertices, {len(model['faces'])} triangles"
          + (f", {textured} textured on a {SHEET}x{SHEET} sheet" if textured else ", flat colours"))


if __name__ == "__main__":
    main()
