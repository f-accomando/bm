"""Shared paths and helpers of the golden tests (not a test module)."""
import importlib.util
import os
import struct
import sys
import zlib

GOLDEN = os.path.dirname(os.path.abspath(__file__))
SKILL = os.path.abspath(os.path.join(GOLDEN, "..", ".."))
REPO = os.path.abspath(os.path.join(SKILL, "..", "..", ".."))
if SKILL not in sys.path:
    sys.path.insert(0, SKILL)

import bm_core  # noqa: E402,F401

# every committed golden; README.md says which command wrote each one
FILES = ["village_models.bm", "anim_figure.bm", "bmmesh_lit.bm", "studio_blocks.bme", "studio_copy3d.bme",
         "mesh_village.bme", "mesh_astrowin.bme"]


def path(name):
    return os.path.join(GOLDEN, name)


def load(name):
    with open(path(name), "rb") as f:
        return f.read()


def fix_crc(data):
    """the same bytes with the header CRC recomputed (to test one broken field at a time)"""
    out = bytearray(data)
    struct.pack_into("<I", out, 20, zlib.crc32(bytes(out[128:])) & 0xFFFFFFFF)
    return bytes(out)


def section_entry(data, typ):
    """(index, offset, size) of the first table entry of `typ`"""
    for i in range(data[17]):
        t, off, size, _ = struct.unpack_from("<IIII", data, 128 + i * 16)
        if t == typ:
            return i, off, size
    raise KeyError(typ)


def import_script(name):
    """a repo script (scripts/<name>.py) imported by path, for cross-checks only;
    bm_core itself never imports them"""
    spec = importlib.util.spec_from_file_location("ref_" + name, os.path.join(REPO, "scripts", name + ".py"))
    mod = importlib.util.module_from_spec(spec)
    sys.path.insert(0, os.path.join(REPO, "scripts"))
    try:
        spec.loader.exec_module(mod)
    finally:
        sys.path.remove(os.path.join(REPO, "scripts"))
    return mod
