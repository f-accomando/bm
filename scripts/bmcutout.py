#!/usr/bin/env python3
"""
bmcutout: the console's outline maker (src/bm/cutout.c: a picture's
outline becomes a cutout with thickness or a lathe, the picture on the
sheet) for the PC tools, through ctypes on build/host/libbmcutout.so
(`make build/host/libbmcutout.so`; made here when the repository is at hand).

  cutout(data, name, lathe=False, height=2, depth=0.2, segments=12, faces=1200)
      -> (model dict as bmmesh.decode gives, flat model dict, (256, 256, rgba))
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
        path = os.environ.get("BMCUTOUT", os.path.join(ROOT, "build", "host", "libbmcutout.so"))
        if not os.path.exists(path) and os.path.exists(os.path.join(ROOT, "Makefile")):
            subprocess.run(["make", "-s", "build/host/libbmcutout.so"], cwd=ROOT, check=False)
        if not os.path.exists(path):
            raise SystemExit(f"{path}: not built (make build/host/libbmcutout.so)")
        _lib = ctypes.CDLL(path)
        P = ctypes.POINTER
        _lib.cutout_pack.restype = ctypes.c_int
        _lib.cutout_pack.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_char_p, ctypes.c_int, ctypes.c_float,
                                     ctypes.c_float, ctypes.c_int, ctypes.c_int, P(ctypes.c_void_p), P(ctypes.c_size_t),
                                     P(ctypes.c_void_p), P(ctypes.c_size_t), P(ctypes.c_void_p), ctypes.c_char_p,
                                     ctypes.c_size_t]
        _lib.cutout_release.argtypes = [ctypes.c_void_p]
    return _lib


def cutout(data, name, lathe=False, height=2.0, depth=0.2, segments=12, faces=1200):
    lib = library()
    rec, reclen = ctypes.c_void_p(), ctypes.c_size_t()
    flat, flatlen = ctypes.c_void_p(), ctypes.c_size_t()
    tex = ctypes.c_void_p()
    err = ctypes.create_string_buffer(128)
    r = lib.cutout_pack(data, len(data), name.encode()[:15], 1 if lathe else 0, height, depth, segments, faces,
                        ctypes.byref(rec), ctypes.byref(reclen), ctypes.byref(flat), ctypes.byref(flatlen),
                        ctypes.byref(tex), err, 128)
    if r < 0:
        raise ValueError(err.value.decode(errors="replace"))
    head = struct.pack("<HHI", 1, 64, 0)
    model = bmmesh.decode(head + ctypes.string_at(rec, reclen.value))[0][0]
    flat_model = bmmesh.decode(head + ctypes.string_at(flat, flatlen.value))[0][0]
    sheet = (256, 256, ctypes.string_at(tex, 256 * 256 * 4))
    for p in (rec, flat, tex):
        lib.cutout_release(p)
    return model, flat_model, sheet
