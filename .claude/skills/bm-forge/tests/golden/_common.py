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


def forge_assets():
    """(Mesh, Anim) from examples: humanoid + boxes, with idle/walk/jump clips, plus a ground box.

    Same as make_forge_samples.py build() but returns (Mesh, Anim) instead of (cart, model, rig).
    """
    import bm_core as B  # noqa: F401
    from bm_core import animgen as AG, shapes, skelgen as SG  # noqa: F401

    EX = os.path.join(SKILL, "examples")
    rig = SG.skeleton(os.path.join(EX, "humanoid.json"))
    model, _ = shapes.load_boxes(os.path.join(EX, "humanoid_boxes.json"))
    rig = SG.bind(model, rig, strategy="nearest_bone_part")
    rig = AG.add_clips(rig, [os.path.join(EX, n + ".json") for n in ("idle", "walk", "jump")])

    # Add ground box
    ground = shapes.boxes_model("ground", [{"lo": [-3, -0.05, -3], "hi": [3, 0, 3], "colour": 0x3C6E3C}])[0]

    return B.Mesh([model, ground]), B.Anim([rig])


# CLI testing utilities
CLI = os.path.join(SKILL, "scripts", "bm_forge.py")
_cli_module = None


def cli():
    """Lazily import and cache the CLI module."""
    global _cli_module
    if _cli_module is None:
        spec = importlib.util.spec_from_file_location("bm_forge_cli", CLI)
        _cli_module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(_cli_module)
    return _cli_module


def run_cli(argv):
    """Run the CLI with given arguments, capturing stdout/stderr.

    Returns:
        (code, stdout, stderr) tuple.
    """
    import contextlib
    import io

    mod = cli()
    stdout_buf = io.StringIO()
    stderr_buf = io.StringIO()

    with contextlib.redirect_stdout(stdout_buf), contextlib.redirect_stderr(stderr_buf):
        try:
            code = mod.main(argv)
        except SystemExit as e:
            code = e.code if isinstance(e.code, int) else (0 if e.code is None else 1)

    return code, stdout_buf.getvalue(), stderr_buf.getvalue()
