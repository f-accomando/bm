"""Export of models, animations and game cartridges.

Models.bm: a cartridge with Lua stub + MESH + ANIM (if rigs).
.bmm: a BMRES file with MESH + ANIM (if rigs).
Game carts (.bm / .b16): with optional cover, budget checks, section order from scripts/mkbm.py:364-415.

Python >= 3.8, standard library only.
"""
import os
import struct

from . import container as C
from . import mesh as meshmod
from . import anim as animmod
from . import sections as S
from . import imageio
from .errors import ValidationError, need

TRI_BUDGET_PI = 1200                            # docs/GAME-GUIDE.md:321
TRI_BUDGET_B16 = 1200                           # NON VERIFICATO, docs/B16.md:28-49
B16_MAX_BYTES = 8 << 20                         # scripts/mkmarket.py:67
MODELS_LUA = b"-- models only (bm-forge export)\nfunction _draw() cls(0) end\n"


def frame_triangles(mesh, draws):
    """Sum of triangles across all drawn models per frame.

    Args:
        mesh: Mesh object with models.
        draws: dict {model_name: times_drawn_per_frame}

    Returns:
        Total triangle count per frame.

    Raises:
        ValidationError if a name in draws has no model in mesh, or count is not >= 0.
    """
    total = 0
    for name, count in draws.items():
        m = mesh.get(name)
        need(m is not None, f"no model {name!r}", "draws")
        need(isinstance(count, int) and count >= 0, f"draw count for {name!r} must be int >= 0", "draws")
        total += len(m.faces) * count
    return total


def check_budget(mesh, draws, budget):
    """Check that frame triangles do not exceed budget.

    Args:
        mesh: Mesh object.
        draws: dict {model_name: times_drawn_per_frame}.
        budget: int, max triangles per frame.

    Returns:
        Total triangles (if within budget).

    Raises:
        ValidationError if total > budget.
    """
    total = frame_triangles(mesh, draws)
    need(total <= budget, f"{total} triangles a frame, budget {budget}", "budget")
    return total


def cover_section(image):
    """Encode an Image into COVER section body.

    Args:
        image: imageio.Image object.

    Returns:
        COVER section body (bytes): <HH w h> then RGBA for each pixel.

    Raises:
        ValidationError if image is out of bounds (1..512 x 1..512).
    """
    body = bytearray()
    body += struct.pack("<HH", image.w, image.h)
    for c in image.pixels:
        r = (c >> 16) & 255
        g = (c >> 8) & 255
        b = c & 255
        body += bytes([r, g, b, 255])
    # validate w/h after building body so we can get exact size for check
    S.check_cover(bytes(body), "COVER")
    return bytes(body)


def models_cart(mesh, anim=None, title="", author="", width=320, height=180):
    """Build a models cartridge (MESH + optional ANIM, no game code).

    Args:
        mesh: Mesh object.
        anim: optional Anim object with rigs.
        title: str, cartridge title.
        author: str, cartridge author.
        width: int, resolution width (default 320).
        height: int, resolution height (default 180).

    Returns:
        Cart object, validated.
    """
    cart = C.new_cart(MODELS_LUA, width, height, title, author)
    cart.set_mesh(mesh)
    if anim is not None and anim.rigs:
        cart.set_anim(anim)
    C.validate(cart)
    return cart


def export_models(path, mesh, anim=None, title="", author=""):
    """Export a models cartridge to .bm file.

    Args:
        path: str, must end with .bm (case-insensitive).
        mesh: Mesh object.
        anim: optional Anim object.
        title: str.
        author: str.

    Returns:
        bytes written to file.

    Raises:
        ValidationError if path does not end .bm.
    """
    need(path.lower().endswith(".bm"), "path must end .bm", "path")
    cart = models_cart(mesh, anim, title, author)
    return C.write_file(path, cart)


def export_bmm(path, mesh, anim=None, title="", author=""):
    """Export a BMRES model file (.bmm).

    Args:
        path: str, must end with .bmm (case-insensitive).
        mesh: Mesh object.
        anim: optional Anim object.
        title: str.
        author: str.

    Returns:
        bytes written to file.

    Raises:
        ValidationError if path does not end .bmm.
    """
    ext = os.path.splitext(path)[1].lower()
    need(ext == ".bmm", "path must end .bmm", "path")

    # Build sections: MESH + optional ANIM
    sections = [C.Section(S.MESH, meshmod.encode(mesh))]
    if anim is not None and anim.rigs:
        sections.append(C.Section(S.ANIM, animmod.encode(anim)))

    # Build BMRES cart with kind = RES_MODEL
    hd = C.Header(C.MAGIC_RES, 0, 0, C.RES_MODEL, 0, 0, title, author)
    cart = C.Cart(hd, sections)
    C.validate(cart)
    return C.write_file(path, cart)


def export_cart(path, lua, mesh, anim=None, title="", author="", res=(320, 180), cover=None, draws=None, budget=None):
    """Export a game cartridge (.bm or .b16).

    Args:
        path: str, must end .bm or .b16 (case-insensitive), not .bme.
        lua: str (UTF-8 encoded to bytes) or bytes, the Lua code.
        mesh: Mesh object.
        anim: optional Anim object.
        title: str, non-empty (required for Market).
        author: str.
        res: tuple (width, height), default (320, 180).
        cover: optional imageio.Image for the COVER section.
        draws: dict {model_name: times_drawn_per_frame}, default {name: 1 for every model}.
        budget: int, triangle budget; default TRI_BUDGET_B16 for .b16, TRI_BUDGET_PI otherwise.

    Returns:
        bytes written to file.

    Raises:
        ValidationError for invalid path/title/cover/budget.
    """
    # Validate title
    need(title, "title must be non-empty (Market requires it)", "title")

    # Determine edition and check path
    ed = C.edition_of(path)
    need(ed in ("bm", "b16"), "path must end .bm or .b16, not .bme", "path")

    # Encode Lua
    if isinstance(lua, str):
        lua_bytes = lua.encode("utf-8")
    else:
        lua_bytes = bytes(lua)

    # Build sections with COVER first if present (mkbm.py:372-374 order)
    if cover is not None:
        cover_body = cover_section(cover)
        sections = [C.Section(S.COVER, cover_body), C.Section(S.LUA, lua_bytes)]
    else:
        sections = [C.Section(S.LUA, lua_bytes)]

    # Mesh and Anim
    cart = C.Cart(C.Header(C.MAGIC_CART, res[0], res[1], 0, C.FMT_RGB565, 0, title, author), sections)
    cart.set_mesh(mesh)
    if anim is not None and anim.rigs:
        cart.set_anim(anim)

    # Budget check
    if draws is None:
        draws = {m.name: 1 for m in mesh.models}
    if budget is None:
        budget = TRI_BUDGET_B16 if ed == "b16" else TRI_BUDGET_PI
    check_budget(mesh, draws, budget)

    # Check path constraints (resolution, edition bit)
    C.check_path(path, cart)

    # Build and size check for .b16
    data = C.build(cart)
    if ed == "b16":
        need(len(data) <= B16_MAX_BYTES, f"cart {len(data)} bytes, .b16 max {B16_MAX_BYTES}", "size")

    # Write file
    return C.write_file(path, cart)
