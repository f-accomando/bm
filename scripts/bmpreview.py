#!/usr/bin/env python3
"""
bmpreview.py - a picture inside a resource file (.bmm, .bmk, .bmi, .bmt): the file shows what it holds.
Standard library only (it uses the renderer of the bm-forge skill, bm_core.preview, a software rasterizer).

  bmpreview.py add FILE [--model NAME] [--cover N] [--size N] [--frames N] [--fps N]
        renders the first model (or NAME) of FILE and writes into FILE
          COVER    the still picture, N x N RGBA (default 128, at most 512): one 3/4 view, clear background
          PREVIEW  a turntable of --frames pictures (default 12, at most 64), --size pixels square (default 96,
                   at most 256), 8-bit palette, colour 0 clear (layout: src/bm/bm.h, docs/RISORSE.md)
  bmpreview.py show FILE OUT.png|OUT.gif    saves what the file carries: .png the still, .gif the turntable
  bmpreview.py drop FILE                    takes both away

A file that has no model (a sound, a palette) gets nothing. Both sections are only a picture: a reader
that does not know them ignores them, and every tool that rewrites the file keeps them. After a change of
the model the picture is old: run `add` again.
"""
import argparse
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(os.path.dirname(HERE), ".claude", "skills", "bm-forge"))
import bmres  # noqa: E402
from bm_core import container, imageio, preview  # noqa: E402

CLEAR = 0x00FE01                                  # the background of the renderer; no model colour is this


def _scene_file(f):
    """the file without its pictures, as bm_core reads it"""
    g = bmres.File(f.kind, f.name, f.author, [s for s in f.sections if s[0] not in (bmres.SEC_COVER, bmres.SEC_PREVIEW)])
    return container.parse(bmres.dump(g))


def render(f, model=None, cover=128, size=96, frames=12, yaw=0.6, pitch=0.35):
    """-> (cover RGBA bytes, (fw, fh, palette, [frame palette numbers])) for the model of f"""
    cart = _scene_file(f)
    mesh = cart.mesh()
    if mesh is None or not mesh.models:
        raise bmres.ResError("no model: nothing to show")
    m = mesh.get(model) if model else mesh.models[0]
    if m is None:
        raise bmres.ResError(f"no model {model!r}")
    tex, inset = preview.model_texture(cart)
    still = preview.render_mesh(m, size=(cover, cover), yaw=yaw, pitch=pitch, texture=tex, inset=inset, background=CLEAR)
    rgba = bytearray()
    for c in still.pixels:
        rgba += bytes((c >> 16 & 255, c >> 8 & 255, c & 255, 0 if c == CLEAR else 255))
    turn = preview.render_turntable(m, frames, size=(size, size), pitch=pitch, texture=tex, inset=inset, background=CLEAR)
    return bytes(rgba), quantize(turn, size)


def quantize(images, size):
    """frames of RGB pixels -> (palette with colour 0 clear, [palette numbers]): the 255 colours that most
    pixels use (after 4 bits a channel), the others to the nearest of them"""
    count = {}
    for im in images:
        for c in im.pixels:
            if c != CLEAR:
                k = (c >> 20 & 15, c >> 12 & 15, c >> 4 & 15)
                count[k] = count.get(k, 0) + 1
    keys = sorted(count, key=lambda k: (-count[k], k))[:255]
    pal = [(0, 0, 0)] + [(k[0] * 17, k[1] * 17, k[2] * 17) for k in keys]
    near = {}

    def number(c):
        if c == CLEAR:
            return 0
        k = (c >> 20 & 15, c >> 12 & 15, c >> 4 & 15)
        if k not in near:
            if k in keys:
                near[k] = 1 + keys.index(k)
            else:
                near[k] = 1 + min(range(len(keys)), key=lambda i: sum((keys[i][j] - k[j]) ** 2 for j in range(3)))
        return near[k]
    frames = [bytes(number(c) for c in im.pixels) for im in images]
    return size, size, pal, frames


def embed(f, model=None, cover=128, size=96, frames=12, fps=8, source=None):
    """puts COVER (first) and PREVIEW into the File f. source: another File to render the model from (a scene
    that is not kept in f); default f"""
    rgba, (fw, fh, pal, fr) = render(source or f, model, cover, size, frames)
    f.drop(bmres.SEC_COVER)
    f.drop(bmres.SEC_PREVIEW)
    f.sections.insert(0, [bmres.SEC_COVER, struct.pack("<HH", cover, cover) + rgba])
    f.sections.append([bmres.SEC_PREVIEW, bmres.preview_body(fw, fh, fps, pal, fr)])
    bmres.check(f, "preview")
    return f


def preview_images(f):
    """the turntable of a File as imageio images (clear colour drawn as a dark grey)"""
    fw, fh, fps, pal, frames = bmres.preview_get(f)
    out = []
    for fr in frames:
        px = [(0x1C1C24 if i == 0 else pal[i][0] << 16 | pal[i][1] << 8 | pal[i][2]) for i in fr]
        out.append(imageio.Image(fw, fh, pixels=px))
    return out, fps


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    a = sub.add_parser("add")
    a.add_argument("file")
    a.add_argument("--model")
    a.add_argument("--cover", type=int, default=128)
    a.add_argument("--size", type=int, default=96)
    a.add_argument("--frames", type=int, default=12)
    a.add_argument("--fps", type=int, default=8)
    s = sub.add_parser("show")
    s.add_argument("file")
    s.add_argument("out")
    sub.add_parser("drop").add_argument("file")
    args = ap.parse_args()
    try:
        f = bmres.read(args.file)
        if args.cmd == "add":
            embed(f, args.model, args.cover, args.size, args.frames, args.fps)
            bmres.write(args.file, f)
            print(f"{args.file}: cover {args.cover}x{args.cover}, preview {args.frames} frames of {args.size}x{args.size}")
        elif args.cmd == "drop":
            f.drop(bmres.SEC_COVER)
            f.drop(bmres.SEC_PREVIEW)
            bmres.write(args.file, f)
        else:
            if args.out.lower().endswith(".gif"):
                imgs, fps = preview_images(f)
                imageio.save_gif(args.out, imgs, delay_cs=max(1, round(100 / max(fps, 1))))
            else:
                w, h, rgba = bmres.cover_get(f)
                px = [rgba[i * 4] << 16 | rgba[i * 4 + 1] << 8 | rgba[i * 4 + 2] if rgba[i * 4 + 3] else 0x1C1C24 for i in range(w * h)]
                imageio.Image(w, h, pixels=px).save_png(args.out)
            print("wrote", args.out)
    except (bmres.ResError, ValueError, OSError) as e:
        print(f"error: {e}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
