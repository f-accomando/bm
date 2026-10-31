"""Skins for the console's GPU: what keeps a skinned model on the V3D's mesh path, and two ways to get there.

The V3D vertex shader places a skinned model (bones) only when every face is on one or two bones
(src/gpu/gpu3d.c, the mesh build: "a face on two bones"). On two bones it takes the textured faces
(vs_lit_tex2, bm3d 3.4) and, since bm3d 7.33, the plain colour faces that are not glossy (vs_lit2).
One face it cannot take sends the whole model to the ARM, one triangle at a time.

  gpu_report(model, rig)          -> dict: faces on 1, 2, 3 bones; plain, glossy, textured on 2; why it falls back
  two_bones_max(model, rig)       -> (model, rig): a face on three bones gets its third corner copied onto the
                                     bone of its second corner (bm3d 7.33 then takes the whole skin)
  palette_joints(model, rig, x, y, width, colours=None)
                                  -> (model, colours): the plain faces on two bones become textured, each on the
                                     texel of its colour in a strip of the sheet at (x, y), `width` texels a row;
                                     for kernels before bm3d 7.33 (vs_lit_tex2 takes them). A textured face lit
                                     by the sun gets a grey light and no fog (as the ARM draws it), so keep the
                                     colour faces where the kernel has vs_lit2.
  palette_image(colours, width)   -> imageio.Image of the strip (put it in the cart's sheet at x, y)
"""
import dataclasses

from . import anim as A
from . import imageio
from .mesh import TEXTURED

GLOSSY = 0x20000000             # src/bm/r3d.h: bit 29


def _bones(face, vb):
    return {vb[face.a], vb[face.b], vb[face.c]}


def gpu_report(model, rig):
    """the faces of a skinned model by their bones, and what the V3D does with the model (src/gpu/gpu3d.c)"""
    vb = rig.vbones
    r = {"one": 0, "two_plain": 0, "two_glossy": 0, "two_textured": 0, "three": 0}
    for f in model.faces:
        n = len(_bones(f, vb))
        if n == 1:
            r["one"] += 1
        elif n == 3:
            r["three"] += 1
        elif f.colour & TEXTURED:
            r["two_textured"] += 1
        elif f.colour & GLOSSY:
            r["two_glossy"] += 1
        else:
            r["two_plain"] += 1
    why = []
    if r["three"]:
        why.append("%d faces on three bones (two_bones_max)" % r["three"])
    if r["two_glossy"]:
        why.append("%d glossy faces on two bones (no highlight on two bones)" % r["two_glossy"])
    r["gpu"] = not why
    r["gpu_before_7_33"] = not why and r["two_plain"] == 0
    if r["two_plain"]:
        r["note"] = "%d plain faces on two bones: the GPU from bm3d 7.33 (vs_lit2); palette_joints for older kernels" \
                    % r["two_plain"]
    r["why"] = why
    return r


def two_bones_max(model, rig):
    """every face on one or two bones: the corner of a face's third bone becomes a copy of that vertex on the bone of
    the face's second corner (one copy for each vertex and bone). The copy opens a thin seam where the bones part."""
    vb = list(rig.vbones)
    verts, faces, seen = [list(v) for v in model.verts], [], {}
    for f in model.faces:
        c = [f.a, f.b, f.c]
        if len(_bones(f, vb)) == 3:
            key = (c[2], vb[c[1]])
            if key not in seen:
                seen[key] = len(verts)
                verts.append(list(model.verts[c[2]]))
                vb.append(vb[c[1]])
            c[2] = seen[key]
        faces.append(dataclasses.replace(f, a=c[0], b=c[1], c=c[2]))
    return dataclasses.replace(model, verts=verts, faces=faces), A.Rig(rig.model, list(rig.bones), vb, list(rig.clips))


def palette_joints(model, rig, x, y, width=64, colours=None):
    """the plain faces on two bones become textured: each colour is one texel of a strip of the sheet, at
    (x + i % width, y + i // width) for the i-th colour; the face's three corners sample its centre. Glossy and
    emissive bits are kept (a glossy face is drawn without its highlight by vs_lit_tex2's grey light).
    `colours`: the strip so far (several models share one strip; it is extended in place).
    Returns (model, colours): the list of 0xRRGGBB, in texel order."""
    if width < 1:
        raise ValueError("width: at least 1")
    vb = rig.vbones
    colours = [] if colours is None else colours
    index, faces = {c: i for i, c in enumerate(colours)}, []
    for f in model.faces:
        if len(_bones(f, vb)) == 2 and not f.colour & TEXTURED:
            rgb = f.colour & 0xFFFFFF
            if rgb not in index:
                index[rgb] = len(colours)
                colours.append(rgb)
            i = index[rgb]
            u, v = (x + i % width) * 8 + 4, (y + i // width) * 8 + 4      # the texel's centre, pixels x 8
            f = dataclasses.replace(f, colour=(f.colour & 0x7F000000) | TEXTURED, uv8=[u, v, u, v, u, v])
        faces.append(f)
    return dataclasses.replace(model, faces=faces), colours


def palette_image(colours, width=64):
    """the strip of palette_joints: `width` texels a row, the rest of the last row black"""
    if not colours:
        raise ValueError("no colours")
    w = min(width, len(colours))
    h = (len(colours) + width - 1) // width
    img = imageio.Image(w, h)
    for i, c in enumerate(colours):
        img.pixels[(i // width) * w + i % width] = c
    return img
