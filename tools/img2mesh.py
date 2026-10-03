#!/usr/bin/env python3
"""
img2mesh: a picture becomes a low-poly 3D model for bm Studio and bm
Animator (M30). A vision model (Claude, through the Anthropic API) looks at
the image, breaks it into parts and writes them in the part language of
the assistant's 3D recipes (src/ai/mesh_script.c: boxes, tubes,
ellipsoids, prisms, mirrored sides, bones and animations). The script is
built and drawn on the PC by build/host/meshview; the renders go back to
the model, which corrects the script, for a few rounds. The result is a
.bm with the model in its MESH and ANIM sections, ready for the console.

  tools/img2mesh.py IMAGE -o OUT.bm [-d "a pink battle mech"] [--name mech]
                    [--rounds 2] [--model claude-opus-5-5] [--effort high]
                    [--record DIR | --replay DIR] [--work DIR]

OUT.bm: a new cartridge with bm Studio's viewer, or an existing one whose
model of that name is replaced (the others stay). The script and the
renders of every round stay in --work (build/img2mesh/NAME).
--record keeps the model's replies in DIR; --replay reads them from there
instead of calling the API (make test-img2mesh).

Credentials: ANTHROPIC_API_KEY in the environment, or an `ant auth login`
profile; `pip install anthropic`. Needs build/host/meshview (make
build/host/meshview).
"""
import argparse
import base64
import os
import re
import struct
import subprocess
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
sys.path.insert(0, os.path.join(ROOT, "scripts"))
import bmmesh  # noqa: E402
import mkbm  # noqa: E402

DEFAULT_MODEL = "claude-opus-5-5"

# ------------------------------------------------------------------ the prompt

SPEC = """You write 3D models for a retro console in a small part language. One statement per line, numbers are plain decimals, # starts a comment.

Units and frame: one unit is one block (a person is about 2 units tall); the model FACES -z (the viewer is at -z looking towards +z), stands on the ground y = 0, y is up, the model's own left is +x. Keep it low-poly: 100-900 faces, boxes and a few tubes/ellipsoids; this is the style of blocky console games, not a sculpture.

Statements:
  mat M RRGGBB [flat]              material M (1-15) and its colour (hex); flat = one shade (lights, glass, eyes)
  box x0 y0 z0 x1 y1 z1 M          a box from corner to corner
  bx x y z w h d M                 a box centred on x and z, standing on y, width w, height h, depth d
  tube ax ay az bx by bz ra rb n M [nocap]   a tube from point a to point b with radii ra and rb (0 = a point: a cone), n sides (4 = a square bar, 6-8 round); caps unless nocap
  cyl x y z r h n M                a vertical cylinder standing on y
  ell x y z rx ry rz n M           an ellipsoid (n = 6 or 8)
  prism x|y|z w0 w1 M u v u v ...  a convex polygon (3-16 corners, pairs u v in the two other axes in order: y z for x, x z for y, x y for z) extruded along the axis from w0 to w1: roofs, fins, wings, blades, chamfered bodies
  wedge x0 y0 z0 x1 z1 h0 h1 M     a box whose top slopes from height h0 at z0 to h1 at z1
  tf x y z rx ry rz                what follows is turned by rx ry rz degrees (around x, then y, then z) and moved to x y z
  tfoff                            back to the model's frame
  bone NAME PARENT hx hy hz tx ty tz   a bone from head to tail; PARENT is a bone name or - for the root; a bone turns around its head
  use NAME                         the faces that follow belong to that bone (rigid parts, as on the PS1)
  side                             the left side (+x) starts here
  mirror [NAME ...]                mirrors the bones named (NAME.L becomes NAME.R, with their children) and every face since `side`: write only the left side of symmetric things
  clip NAME length loop|once       an animation of that length in seconds
  key t                            a keyframe at t seconds; bones not mentioned stay at rest
  turn NAME rx ry rz               in the last key: the bone turned by degrees (for a leg or arm hanging down, +x swings it forward)
  shift NAME x y z                 in the last key: the bone moved

Rules:
- Declare all materials first, then the bones (root first, parents before children), then the faces grouped by bone with `use`, then the animations.
- Build only the left half of symmetric parts after `side` and finish with `mirror` naming the left bones (NAME.L); the right side comes for free.
- Every part that should move in an animation gets a bone; a static object gets no bones at all.
- Animations when the thing can move: idle (2 s, loop, subtle), walk (1 s, loop: legs alternate, +/-25 to 30 degrees, body bobs 0.04-0.08 at the mid keys) or fly/swim; one more that fits (attack, fire, wave). Keys: idle 0 and 1; walk 0, 0.25, 0.5, 0.75.
- Colours: 3-8 materials, saturated main colour plus a darker variant for plates and a dark 30323A for joints; flat materials for lights, eyes and glass.
- Proportions matter more than detail: get the big masses right first (body, head, legs), then add the recognisable parts (fins, guns, ears, tail).
- Reply with ONE fenced code block containing the whole script, nothing else in the block; a short sentence before it is fine."""

ROUND1 = """Build this as a model in the part language. {desc}Name it "{name}". Look carefully at the picture: list to yourself the main masses and their proportions, the distinctive parts, the colours, what moves; then write the script."""

ROUND_FIX = """Here is your script built and drawn: the top row shows it from the front, from the front-left (3/4), from its left side and from behind; the rows below are the poses of each animation at four moments. Compare with the original picture and improve the script: fix the proportions and the placement of the parts, add what is missing, remove what looks wrong, check that nothing floats or sinks into the ground, that animations move the right bones in the right direction. Reply with the complete corrected script in one fenced block."""

ROUND_ERROR = """The script did not build: {error}
Fix it and reply with the complete script in one fenced block."""


# ------------------------------------------------------------------ images

def read_image(path):
    """an image block for the API; a PPM (meshview's renders) becomes a PNG"""
    ext = os.path.splitext(path)[1].lower()
    media = {".png": "image/png", ".jpg": "image/jpeg", ".jpeg": "image/jpeg", ".gif": "image/gif",
             ".webp": "image/webp", ".ppm": "image/png"}.get(ext)
    if not media:
        raise SystemExit(f"{path}: an image (png, jpg, gif, webp, ppm)")
    with open(path, "rb") as f:
        data = f.read()
    if ext == ".ppm":
        data = png_of_ppm(data)
    return {"type": "image", "source": {"type": "base64", "media_type": media,
                                        "data": base64.standard_b64encode(data).decode()}}


def png_of_ppm(d):
    parts = d.split(b"\n", 3)
    w, h = map(int, parts[1].split())
    px = parts[3]
    raw = b"".join(b"\0" + px[y * w * 3:(y + 1) * w * 3] for y in range(h))

    def chunk(t, b):
        return struct.pack(">I", len(b)) + t + b + struct.pack(">I", zlib.crc32(t + b) & 0xFFFFFFFF)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) \
        + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b"")


def ppm_to_png(src, dst):
    with open(src, "rb") as f:
        d = f.read()
    with open(dst, "wb") as f:
        f.write(png_of_ppm(d))


# ------------------------------------------------------------------ the script

def extract_script(text):
    """the last fenced block of a reply"""
    blocks = re.findall(r"```[a-zA-Z0-9_-]*\n(.*?)```", text, re.S)
    if not blocks:
        return None
    return blocks[-1].strip("\n") + "\n"


def build(meshview, script_path, ppm_path, json_path):
    """-> (error or None); the renders and the JSON of the model"""
    r = subprocess.run([meshview, "script", script_path, ppm_path], capture_output=True, text=True)
    if r.returncode != 0:
        return (r.stderr.strip() or r.stdout.strip() or "meshview failed").split("\n")[-1]
    r = subprocess.run([meshview, "json", script_path, json_path], capture_output=True, text=True)
    if r.returncode != 0:
        return r.stderr.strip()
    return None


# ------------------------------------------------------------------ the .bm

def model_sections(name, model_json, secs):
    """the MESH and ANIM bodies with this model added to (or replacing the
    one of that name in) the sections of a cartridge"""
    import json
    m = json.load(open(model_json))
    entry, vb = bmmesh.encode_faces(name, m["faces"], m["bones"])
    models, inset = ([], 0.25)
    if bmmesh.SEC_MESH in secs:
        models, inset = bmmesh.decode(secs[bmmesh.SEC_MESH])
    models = [x for x in models if x["name"] != name] + [entry]
    rigs = bmmesh.decode_anim(secs[bmmesh.SEC_ANIM]) if bmmesh.SEC_ANIM in secs else []
    rigs = [r for r in rigs if r[0] != name]
    if m["bones"]:
        rigs.append((name, m["bones"], vb, m["clips"]))
    return bmmesh.encode(models, inset), (bmmesh.encode_anim(rigs) if rigs else None)


def viewer_lua():
    """bm Studio's viewer: the Lua of a new 3D project"""
    src = open(os.path.join(ROOT, "carts", "studio", "main.lua"), encoding="utf-8").read()
    m = re.search(r"local VIEWER = \[\[\n(.*?)\n\]\]", src, re.S)
    return (m.group(1) + "\n").encode() if m else b"function _draw() cls(0) print('no viewer', 8, 8, 0xFFFFFF) end\n"


def write_cart(out, name, model_json, title):
    if os.path.exists(out):
        data = open(out, "rb").read()
        secs = dict(bmmesh.cart_sections(data))
        mesh, anim = model_sections(name, model_json, secs)
        secs[bmmesh.SEC_MESH] = mesh
        if anim:
            secs[bmmesh.SEC_ANIM] = anim
        else:
            secs.pop(bmmesh.SEC_ANIM, None)
        order = [t for t, _ in bmmesh.cart_sections(data)]
        for t in (bmmesh.SEC_MESH, bmmesh.SEC_ANIM):
            if t in secs and t not in order:
                order.append(t)
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
    mesh, anim = model_sections(name, model_json, {})
    extra = [(bmmesh.SEC_ANIM, anim)] if anim else []
    with open(out, "wb") as f:
        f.write(mkbm.pack(viewer_lua(), title=title, author="img2mesh", mesh=mesh, extra=extra))
    return "written"


# ------------------------------------------------------------------ the model

def ask(client, a, messages, system, record_path, replay_path):
    """one reply of the model: its content blocks (kept in the conversation
    as they are) and its text"""
    if replay_path:
        if not os.path.exists(replay_path):
            raise SystemExit(f"replay: no {replay_path}")
        text = open(replay_path, encoding="utf-8").read()
        return [{"type": "text", "text": text}], text
    with client.beta.messages.stream(
        model=a.model, max_tokens=32000, system=system, messages=messages,
        thinking={"type": "adaptive"}, output_config={"effort": a.effort},
        betas=["server-side-fallback-2026-07-01"], fallbacks="default",
    ) as stream:
        for _ in stream.text_stream:
            print(".", end="", flush=True)
        response = stream.get_final_message()
    print()
    if response.stop_reason == "refusal":
        raise SystemExit("the model declined the request")
    text = "".join(b.text for b in response.content if b.type == "text")
    u = response.usage
    print(f"  {response.model}: {u.input_tokens} in, {u.output_tokens} out")
    if record_path:
        with open(record_path, "w", encoding="utf-8") as f:
            f.write(text)
    return response.content, text


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("image")
    ap.add_argument("-o", "--out", required=True, help="the .bm to write or to add the model to")
    ap.add_argument("-d", "--describe", default="", help="a few words about the thing (what it is, what moves)")
    ap.add_argument("--name", help="the model's name (up to 15 letters; default: from the image's file name)")
    ap.add_argument("--rounds", type=int, default=2, help="rounds of render-and-correct after the first (default 2)")
    ap.add_argument("--model", default=DEFAULT_MODEL)
    ap.add_argument("--effort", default="high", choices=["low", "medium", "high", "xhigh", "max"])
    ap.add_argument("--record", help="keep the model's replies in this directory")
    ap.add_argument("--replay", help="read the replies from this directory instead of the API")
    ap.add_argument("--meshview", default=os.path.join(ROOT, "build", "host", "meshview"))
    ap.add_argument("--work", help="where the scripts and renders go (default build/img2mesh/NAME)")
    a = ap.parse_args()

    name = (a.name or re.sub(r"[^A-Za-z0-9_]", "", os.path.splitext(os.path.basename(a.image))[0]) or "model")[:15]
    work = a.work or os.path.join(ROOT, "build", "img2mesh", name)
    os.makedirs(work, exist_ok=True)
    if a.record:
        os.makedirs(a.record, exist_ok=True)
    if not os.path.exists(a.meshview):
        raise SystemExit(f"{a.meshview}: not built (make build/host/meshview)")

    client = None
    if not a.replay:
        try:
            import anthropic
        except ImportError:
            raise SystemExit("pip install anthropic")
        client = anthropic.Anthropic()

    example = os.path.join(ROOT, "tests", "ai", "img2mesh", "mech.txt")
    system = SPEC
    if os.path.exists(example):
        system += "\n\nAn example, a battle mech with a cockpit, two bent legs, gun pods and fins:\n```\n" \
            + open(example, encoding="utf-8").read() + "```"

    desc = f"It is: {a.describe.strip()}. " if a.describe.strip() else ""
    messages = [{"role": "user", "content": [read_image(a.image),
                                             {"type": "text", "text": ROUND1.format(desc=desc, name=name)}]}]
    script = None
    rounds_left = a.rounds
    n = 0
    while True:
        n += 1
        print(f"round {n}: " + ("the first script" if n == 1 else "corrections"), flush=True)
        rec = os.path.join(a.record, f"round{n}.txt") if a.record else None
        rep = os.path.join(a.replay, f"round{n}.txt") if a.replay else None
        content, text = ask(client, a, messages, system, rec, rep)
        messages.append({"role": "assistant", "content": content})
        got = extract_script(text)
        if not got:
            if n > a.rounds + 2:
                raise SystemExit("no script in the reply")
            messages.append({"role": "user", "content": "Reply with the complete script in one fenced code block."})
            continue
        script_path = os.path.join(work, f"round{n}.txt")
        with open(script_path, "w", encoding="utf-8") as f:
            f.write(got)
        ppm, png, js = (os.path.join(work, f"round{n}.{ext}") for ext in ("ppm", "png", "json"))
        err = build(a.meshview, script_path, ppm, js)
        if err:
            print(f"  does not build: {err}")
            if n > a.rounds + 2:
                raise SystemExit("the script keeps failing: " + err)
            messages.append({"role": "user", "content": ROUND_ERROR.format(error=err)})
            continue
        ppm_to_png(ppm, png)
        script = got
        with open(os.path.join(work, "model.txt"), "w", encoding="utf-8") as f:
            f.write(script)
        print(f"  {script_path}: built, {png}")
        if rounds_left <= 0:
            break
        rounds_left -= 1
        messages.append({"role": "user", "content": [read_image(png), {"type": "text", "text": ROUND_FIX}]})
    what = write_cart(a.out, name, js, title=name)
    print(f"{a.out}: {what} the model {name} (script: {os.path.join(work, 'model.txt')})")


if __name__ == "__main__":
    main()
