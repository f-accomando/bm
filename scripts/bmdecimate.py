#!/usr/bin/env python3
"""
bmdecimate: the console's polygon reducer (src/bm/decimate.c, quadric edge
collapse) for the PC tools, through ctypes on build/host/libbmdecimate.so
(`make build/host/libbmdecimate.so`; made here when it is missing and the
repository is at hand).

  reduce_record(rec, vb, target) -> (rec, vb, triangles)   one MESH model record
  reduce_cart(data, target_of)   -> (data, report)         every model of a .bm
"""
import ctypes
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
sys.path.insert(0, HERE)
import bmmesh  # noqa: E402

_lib = None


def library():
    global _lib
    if _lib is None:
        path = os.environ.get("BMDECIMATE", os.path.join(ROOT, "build", "host", "libbmdecimate.so"))
        if not os.path.exists(path) and os.path.exists(os.path.join(ROOT, "Makefile")):
            subprocess.run(["make", "-s", "build/host/libbmdecimate.so"], cwd=ROOT, check=False)
        if not os.path.exists(path):
            raise SystemExit(f"{path}: not built (make build/host/libbmdecimate.so)")
        _lib = ctypes.CDLL(path)
        _lib.bm_model_reduce.restype = ctypes.c_int
        _lib.bm_model_reduce.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_char_p, ctypes.c_int,
                                         ctypes.c_float, ctypes.c_char_p, ctypes.POINTER(ctypes.c_size_t),
                                         ctypes.c_char_p]
    return _lib


def reduce_record(rec, vb, target, max_err=0.0):
    """one model record of the MESH section (name16, counts, vertices,
    faces) and the bone of each vertex (bytes, or None) -> the reduced
    record, its bones and its number of triangles"""
    nv = struct.unpack_from("<H", rec, 16)[0]
    out = ctypes.create_string_buffer(len(rec))
    vb_out = ctypes.create_string_buffer(nv) if vb is not None else None
    outlen = ctypes.c_size_t(0)
    n = library().bm_model_reduce(rec, len(rec), vb, target, max_err, out, ctypes.byref(outlen), vb_out)
    if n < 0:
        raise ValueError("broken model record" if n == -1 else "not enough memory")
    nv2 = struct.unpack_from("<H", out.raw, 16)[0]
    return out.raw[:outlen.value], (vb_out.raw[:nv2] if vb is not None else None), n


def reduce_model(model, target, vb=None, max_err=0.0):
    """a model as bmmesh.decode gives it -> the reduced model (and bones)"""
    rec = bmmesh.encode([model], 0)[8:]
    rec2, vb2, _ = reduce_record(rec, bytes(vb) if vb is not None else None, target, max_err)
    models, _ = bmmesh.decode(struct.pack("<HHI", 1, 0, 0) + rec2)
    return models[0], (list(vb2) if vb2 is not None else None)


def reduce_cart(data, target_of, max_err=0.0):
    """every model of a .bm: target_of(name, triangles) -> the triangles
    wanted (None: leave it). The skeletons follow. Returns the new file and
    [(name, before, after)]."""
    secs = dict(bmmesh.cart_sections(data))
    if bmmesh.SEC_MESH not in secs:
        raise ValueError("no 3D models in the cartridge")
    models, inset = bmmesh.decode(secs[bmmesh.SEC_MESH])
    rigs = bmmesh.decode_anim(secs[bmmesh.SEC_ANIM]) if bmmesh.SEC_ANIM in secs else []
    rig_of = {r[0]: r for r in rigs}
    report = []
    for i, m in enumerate(models):
        target = target_of(m["name"], len(m["faces"]))
        if target is None:
            continue
        rig = rig_of.get(m["name"])
        vb = rig[2] if rig and len(rig[2]) == len(m["verts"]) else None
        m2, vb2 = reduce_model(m, target, vb, max_err)
        models[i] = m2
        if rig and vb2 is not None:
            rig_of[m["name"]] = (rig[0], rig[1], vb2, rig[3])
        report.append((m["name"], len(m["faces"]), len(m2["faces"])))
    secs[bmmesh.SEC_MESH] = bmmesh.encode(models, inset)
    if rigs:
        secs[bmmesh.SEC_ANIM] = bmmesh.encode_anim([rig_of[r[0]] for r in rigs])
    return bmmesh.rewrite_cart(data, secs), report
