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
  tools/meshy2mesh.py --task ID -o OUT.bm ...         (a Meshy task already made)

OUT.bm: a new cartridge with bm Studio's viewer (textured, its own sheet)
or an existing one with the model added or replaced (flat colours: the
cartridge keeps its sheet). The key: MESHY_API_KEY in the environment.
The .glb Meshy made stays next to the output (OUT.glb) to redo the
conversion offline with --glb; a new task's id goes to OUT.task.txt (for
tools/meshy_rig.py --task).
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

def api(method, path, key, body=None, tries=6):
    """one call to Meshy; connection errors, timeouts, 429 and 5xx are
    retried with a backoff (tries=1 for the POST: a lost reply must not
    make a second paid task)"""
    req = urllib.request.Request(API + path, method=method,
                                 headers={"Authorization": "Bearer " + key, "Content-Type": "application/json"},
                                 data=json.dumps(body).encode() if body is not None else None)
    for n in range(tries):
        try:
            with urllib.request.urlopen(req, timeout=60) as r:
                return json.loads(r.read().decode())
        except urllib.error.HTTPError as e:
            if (e.code != 429 and e.code < 500) or n == tries - 1:
                raise SystemExit(f"meshy {method} {path}: HTTP {e.code}: {e.read().decode(errors='replace')[:300]}")
            wait = float(e.headers.get("Retry-After") or 0) or 5 * 2 ** n
        except (urllib.error.URLError, TimeoutError) as e:
            if n == tries - 1:
                raise SystemExit(f"meshy {method} {path}: {e}")
            wait = 5 * 2 ** n
        print(f"  meshy: retry in {wait:.0f} s", flush=True)
        time.sleep(wait)


def image_data_uri(path):
    ext = os.path.splitext(path)[1].lower()
    media = {".png": "image/png", ".jpg": "image/jpeg", ".jpeg": "image/jpeg"}.get(ext)
    data = None
    if not media:
        # anything else (webp, gif, ppm...) goes as a PNG, through Pillow
        try:
            from PIL import Image
            import io
            im = Image.open(path).convert("RGBA")
            buf = io.BytesIO()
            im.save(buf, format="PNG")
            data, media = buf.getvalue(), "image/png"
        except ImportError:
            raise SystemExit(f"{path}: a png or jpg (or pip install pillow for other formats)")
    if data is None:
        data = open(path, "rb").read()
    return f"data:{media};base64," + base64.standard_b64encode(data).decode()


def meshy_wait(task, key, minutes=40):
    """polls the task until it is done: its result (model_urls...)"""
    deadline = time.time() + minutes * 60
    while True:
        t = api("GET", f"/image-to-3d/{task}", key)
        status = t.get("status")
        print(f"  {status} {t.get('progress', 0)}%", flush=True)
        if status == "SUCCEEDED":
            return t
        if status in ("FAILED", "CANCELED", "EXPIRED"):
            raise SystemExit(f"meshy: {status}: {(t.get('task_error') or {}).get('message', '')}")
        if time.time() > deadline:
            raise SystemExit(f"meshy: still {status} after {minutes} minutes: later, --task {task}")
        time.sleep(5)


def fetch_image(url, tries=3):
    """the picture at an https URL, fetched from here as a browser would
    (sites that refuse Meshy's fetcher, or hot links): (bytes, media type).
    A wiki page with ?file=NAME (fandom, MediaWiki) is resolved to the
    file through the wiki's API; an HTML page to its og:image."""
    import html
    import urllib.parse

    def get(u):
        req = urllib.request.Request(u, headers={"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                                                 "AppleWebKit/537.36 Chrome/120 Safari/537.36",
                                                 "Accept": "image/*,text/html;q=0.9,*/*;q=0.8"})
        with urllib.request.urlopen(req, timeout=60) as r:
            return r.read(), (r.headers.get("Content-Type") or "").split(";")[0].strip().lower()

    parts = urllib.parse.urlsplit(url)
    q = urllib.parse.parse_qs(parts.query)
    if q.get("file"):
        # a MediaWiki page showing a file: the file's own URL from the API
        api_url = f"{parts.scheme}://{parts.netloc}/api.php?" + urllib.parse.urlencode(
            {"action": "query", "titles": "File:" + q["file"][0], "prop": "imageinfo", "iiprop": "url",
             "format": "json"})
        data, _ = get(api_url)
        for page in json.loads(data).get("query", {}).get("pages", {}).values():
            for info in page.get("imageinfo", []):
                if info.get("url"):
                    url = info["url"]
    last = None
    for n in range(tries):
        try:
            data, media = get(url)
            if media.startswith("text/html"):
                m = re.search(r'property="og:image"\s+content="([^"]+)"', data.decode(errors="replace")) or \
                    re.search(r'content="([^"]+)"\s+property="og:image"', data.decode(errors="replace"))
                if not m:
                    raise ValueError("an HTML page without a picture")
                data, media = get(html.unescape(m.group(1)))
            if not media.startswith("image/"):
                raise ValueError(f"not a picture: {media or 'no type'}")
            return data, media
        except (urllib.error.URLError, ValueError, OSError) as e:
            last = e
            time.sleep(2 * (n + 1))
    print(f"meshy2mesh: {url}: {last}; Meshy will fetch it itself", file=sys.stderr)
    return None


def meshy_image_to_3d(image, key, polycount, texture=True, symmetry="auto", minutes=40, log=None):
    """-> the task's result: model_urls, texture_urls... (SUCCEEDED).
    image: a file, or an https URL (fetched from here when it can be, so
    the picture goes to Meshy as data; else Meshy fetches it itself).
    log: a file for the task's id (to rig the model: tools/meshy_rig.py)"""
    if re.match(r"^https?://", image):
        got = fetch_image(image)
        if got:
            data, media = got
            if media not in ("image/png", "image/jpeg"):
                import tempfile
                with tempfile.NamedTemporaryFile(suffix="." + media.split("/")[1], delete=False) as f:
                    f.write(data)
                url = image_data_uri(f.name)
                os.remove(f.name)
            else:
                url = f"data:{media};base64," + base64.standard_b64encode(data).decode()
        else:
            url = image
    else:
        url = image_data_uri(image)
    body = {"image_url": url, "ai_model": "meshy-5", "topology": "triangle",
            "target_polycount": polycount, "should_remesh": True, "should_texture": texture,
            "enable_pbr": False, "symmetry_mode": symmetry}
    r = api("POST", "/image-to-3d", key, body, tries=1)
    task = r.get("result")
    if not task:
        raise SystemExit(f"meshy: no task id in {r}")
    print(f"meshy: task {task} (to pick it up again: --task {task})", flush=True)
    if log:
        with open(log, "w") as f:
            f.write(f"image-to-3d {task}\n")
    return meshy_wait(task, key, minutes)


def download(url, path, tries=6):
    for n in range(tries):
        try:
            with urllib.request.urlopen(url, timeout=120) as r, open(path, "wb") as f:
                f.write(r.read())
            return
        except (urllib.error.URLError, TimeoutError) as e:
            if n == tries - 1 or (isinstance(e, urllib.error.HTTPError) and e.code != 429 and e.code < 500):
                raise SystemExit(f"meshy: download {url}: {e}")
            time.sleep(5 * 2 ** n)


# ------------------------------------------------------------------ the glTF

def glb_scene(data):
    """every triangle of a .glb with the node transforms applied:
    (positions, uvs or None, colours or None, triangles [(a, b, c, image)])
    where image is the index of the base colour image or None"""
    js, bin_ = bmmesh.read_glb(data)
    positions, uvs, colours, tris = [], [], [], []
    have_uv = have_col = False
    mirrored = []               # per triangle: the node's transform mirrors (det < 0)

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
        det = (m[0] * (m[5] * m[10] - m[9] * m[6]) - m[4] * (m[1] * m[10] - m[9] * m[2])
               + m[8] * (m[1] * m[6] - m[5] * m[2]))
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
                    mirrored.append(det < 0)
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
    return positions, (uvs if have_uv else None), (colours if have_col else None), tris, images, mirrored


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
    positions, uvs, colours, tris, images, mirrored = glb_scene(data)
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
    # the console keeps the texture corners on the triangles, not on the
    # vertices: the vertices a texture seam split can be one again
    pts, remap = merge_positions(pts)
    orig_tris = tris
    tris = [(remap[a], remap[b], remap[c], image) for a, b, c, image in tris]
    faces = []
    for ((a, b, c, image), mirror), (oa, ob, oc, _) in zip(zip(tris, mirrored), orig_tris):
        img = decoded.get(image)
        fuv = (uvs[oa], uvs[ob], uvs[oc]) if uvs and uvs[oa] and uvs[ob] and uvs[oc] else None
        if textured and img and fuv:
            colour = None
        elif img and fuv:
            colour = sample(img, sum(q[0] for q in fuv) / 3, sum(q[1] for q in fuv) / 3)
        elif colours:
            colour = tuple(int(bmmesh.linear_to_srgb((colours[oa][k] + colours[ob][k] + colours[oc][k]) / 3))
                           for k in range(3))
        else:
            colour = (138, 138, 154)
        # the winding flips with z (glTF: counter-clockwise in front, the
        # console: clockwise) unless the node's own transform mirrored it
        if mirror:
            faces.append((a, b, c, colour, image, fuv))
        else:
            faces.append((a, c, b, colour, image, (fuv[0], fuv[2], fuv[1]) if fuv else None))
    # past what a model can hold at all: the vertices snap to a grid (flat
    # colours only: the texture would tear); above --max-tris, below, the
    # reducer keeps the texture
    if len(faces) > bmmesh.MAX_FACES or len(pts) > bmmesh.MAX_VERTS:
        if textured:
            print(f"meshy2mesh: {len(faces)} triangles and {len(pts)} vertices: more than {bmmesh.MAX_FACES}"
                  f" triangles or {bmmesh.MAX_VERTS} vertices, painted flat instead", file=sys.stderr)
            textured = False
            faces = [(a, b, c, col or (sample(decoded[im], sum(q[0] for q in fuv) / 3, sum(q[1] for q in fuv) / 3)
                                       if decoded.get(im) and fuv else (138, 138, 154)), im, fuv)
                     for a, b, c, col, im, fuv in faces]
        pts, faces = cluster(pts, faces, grid)
        if len(faces) > bmmesh.MAX_FACES or len(pts) > bmmesh.MAX_VERTS:
            raise SystemExit(f"still {len(faces)} triangles and {len(pts)} vertices: a smaller --grid")
    sheet = None
    out_faces = []
    if textured:
        # one image on the sheet: the first base colour image used
        used = [im for _, _, _, _, im, _ in faces if decoded.get(im)]
        first = used[0]
        sheet = resize(decoded[first], SHEET)
        for a, b, c, col, im, fuv in faces:
            if im == first and fuv:
                uv = []
                for q in fuv:
                    uv += [min(1.0, max(0.0, q[0])) * SHEET, min(1.0, max(0.0, q[1])) * SHEET]
                out_faces.append((a, b, c, bmmesh.TEXTURED, tuple(uv)))
            else:
                colour = sample(decoded[im], sum(q[0] for q in fuv) / 3, sum(q[1] for q in fuv) / 3) \
                    if decoded.get(im) and fuv else (col or (138, 138, 154))
                out_faces.append((a, b, c, colour[0] << 16 | colour[1] << 8 | colour[2], None))
    else:
        for a, b, c, col, _, _ in faces:
            out_faces.append((a, b, c, col[0] << 16 | col[1] << 8 | col[2], None))
    model = {"name": name, "verts": pts, "faces": out_faces}
    if len(out_faces) > max_tris:
        # the console's reducer (src/bm/decimate.c): seams and colour lines stay
        import bmdecimate
        model, _ = bmdecimate.reduce_model(model, max_tris)
        print(f"meshy2mesh: {len(out_faces)} triangles reduced to {len(model['faces'])} (--max-tris {max_tris})",
              file=sys.stderr)
    return model, sheet


def merge_positions(pts):
    """the same position once: (the vertices, old index -> new index)"""
    index, out, remap = {}, [], []
    for p in pts:
        key = (round(p[0], 5), round(p[1], 5), round(p[2], 5))
        i = index.get(key)
        if i is None:
            i = len(out)
            index[key] = i
            out.append(p)
        remap.append(i)
    return out, remap


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
    for a, b, c, col, im, fuv in faces:
        ia, ib, ic = index[a], index[b], index[c]
        if len({ia, ib, ic}) < 3:
            continue
        key = (ia, ib, ic)
        if key in merged:
            m = merged[key]
            m[3] = tuple((m[3][k] * m[6] + col[k]) // (m[6] + 1) for k in range(3))
            m[6] += 1
        else:
            merged[key] = [ia, ib, ic, col, im, fuv, 1]
    # the vertices used, renumbered
    used = sorted({i for k in merged for i in k})
    renum = {i: n for n, i in enumerate(used)}
    out = [(renum[m[0]], renum[m[1]], renum[m[2]], m[3], m[4], m[5]) for m in merged.values()]
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
        with open(out, "wb") as f:
            f.write(bmmesh.rewrite_cart(data, secs))
        return "added to"
    mesh = bmmesh.encode([model], 0.25)
    with open(out, "wb") as f:
        f.write(mkbm.pack(img2mesh.viewer_lua(), sheet=sheet, title=title, author="meshy2mesh", mesh=mesh))
    return "written"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("image", nargs="?", help="the picture (png, jpg, webp), or its https URL")
    ap.add_argument("--glb", help="a .glb already made: no call to Meshy")
    ap.add_argument("--task", help="a Meshy task already made (its id): wait for it and download it, no new task")
    ap.add_argument("--wait", type=int, default=40, help="minutes to wait for Meshy (default 40)")
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
    src = a.glb or a.image or a.task
    if not src:
        ap.error("an image, or --glb, or --task")
    stem = os.path.splitext(os.path.basename(src.split("?")[0]))[0]
    name = (a.name or re.sub(r"[^A-Za-z0-9_]", "", stem) or "model")[:15]
    if a.glb:
        data = open(a.glb, "rb").read()
    else:
        key = os.environ.get("MESHY_API_KEY")
        if not key:
            raise SystemExit("MESHY_API_KEY: the key from meshy.ai, in the environment")
        if a.task:
            task = meshy_wait(a.task, key, a.wait)
        else:
            task = meshy_image_to_3d(a.image, key, a.polycount, texture=not a.no_texture, symmetry=a.symmetry,
                                     minutes=a.wait, log=os.path.splitext(a.out)[0] + ".task.txt")
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
