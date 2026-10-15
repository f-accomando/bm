"""Export of models, animations and game cartridges.

Models.bm: a cartridge with Lua stub + MESH + ANIM (if rigs).
.bmm: a BMRES file with MESH + ANIM (if rigs).
Game carts (.bm / .b16): with optional cover, budget checks, section order from scripts/mkbm.py:364-415.
Game folders and Market layouts: repo structure and market catalog folders.

Python >= 3.8, standard library only.
"""
import os
import re
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


def models_cart(mesh, anim=None, title="", author="", width=320, height=180, kernel_limits=False):
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
    cart.set_mesh(mesh, kernel_limits=kernel_limits)
    if anim is not None and anim.rigs:
        cart.set_anim(anim, kernel_limits=kernel_limits)
    C.validate(cart, kernel_limits=kernel_limits)
    return cart


def export_models(path, mesh, anim=None, title="", author="", kernel_limits=False):
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
    cart = models_cart(mesh, anim, title, author, kernel_limits=kernel_limits)
    return C.write_file(path, cart, kernel_limits=kernel_limits)


def export_bmm(path, mesh, anim=None, title="", author="", kernel_limits=False):
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
    sections = [C.Section(S.MESH, meshmod.encode(mesh, kernel_limits))]
    if anim is not None and anim.rigs:
        sections.append(C.Section(S.ANIM, animmod.encode(anim)))

    # Build BMRES cart with kind = RES_MODEL
    hd = C.Header(C.MAGIC_RES, 0, 0, C.RES_MODEL, 0, 0, title, author)
    cart = C.Cart(hd, sections)
    C.validate(cart, kernel_limits=kernel_limits)
    return C.write_file(path, cart, kernel_limits=kernel_limits)


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


def export_folder(root, game, lua, mesh, anim=None, title="", author="", res=(320, 180), cover=None, overwrite=False):
    """Export a game folder with main.lua, models.bm, optional cover.png, and make.txt.

    Args:
        root: str, an existing directory.
        game: str, game id matching ^[a-z][a-z0-9_]{0,14}$.
        lua: str (UTF-8 encoded to bytes) or bytes, the Lua code.
        mesh: Mesh object.
        anim: optional Anim object.
        title: str, non-empty cartridge title.
        author: str, cartridge author.
        res: tuple (width, height), default (320, 180).
        cover: optional imageio.Image for cover.png (will be saved as PNG).
        overwrite: bool, if False raise if root/game/main.lua exists.

    Returns:
        dict with keys: "main.lua", "models.bm", "cover.png" (if given), "make.txt".
        Values are absolute paths to created files.

    Raises:
        ValidationError for invalid game id, missing root, overwrite conflict, or invalid title.
    """
    # Validate game id
    need(re.match(r"^[a-z][a-z0-9_]{0,14}$", game), "game id must match ^[a-z][a-z0-9_]{0,14}$", "game")

    # Validate root exists
    need(os.path.isdir(root), f"root {root!r} is not a directory", "root")

    # Check overwrite protection
    game_dir = os.path.join(root, game)
    main_lua = os.path.join(game_dir, "main.lua")
    if os.path.exists(main_lua) and not overwrite:
        raise ValidationError(f"{main_lua!r} exists; set overwrite=True to replace", "overwrite")

    # Create game directory
    os.makedirs(game_dir, exist_ok=True)

    # Write main.lua
    if isinstance(lua, str):
        lua_bytes = lua.encode("utf-8")
    else:
        lua_bytes = bytes(lua)
    with open(main_lua, "wb") as f:
        f.write(lua_bytes)

    # Write models.bm (using export_models)
    models_path = os.path.join(game_dir, "models.bm")
    export_models(models_path, mesh, anim, title, author)

    # Write cover.png if given
    cover_path = None
    if cover is not None:
        cover_path = os.path.join(game_dir, "cover.png")
        cover.save_png(cover_path)

    # Write make.txt
    make_path = os.path.join(game_dir, "make.txt")
    make_lines = [
        "# add to <repo>/Makefile by hand (bm-forge never edits it)",
        f"#   append {game} to GAMES (Makefile:195)",
        f"title_{game} := {title}",
    ]
    if res != (640, 360):
        make_lines.append(f"res_{game} := {res[0]}x{res[1]}")
    make_lines.append("")  # Ensure trailing newline
    with open(make_path, "w", encoding="utf-8") as f:
        f.write("\n".join(make_lines))

    # Return paths
    result = {
        "main.lua": main_lua,
        "models.bm": models_path,
        "make.txt": make_path,
    }
    if cover_path:
        result["cover.png"] = cover_path
    return result


def export_market(root, game_id, version, license, about, bm_path=None, b16_path=None):
    """Export game cartridge(s) to Market folder layout.

    Args:
        root: str, an existing directory where root/<id>/ will be created.
        game_id: str, game id matching ^[a-z0-9][a-z0-9-]{0,22}$.
        version: str, version matching ^[A-Za-z0-9][A-Za-z0-9._-]{0,14}$.
        license: str, 1-40 printable ASCII characters (32-126).
        about: str, 0-120 printable ASCII characters (32-126).
        bm_path: optional str, path to a .bm file (edition must be 'bm').
        b16_path: optional str, path to a .b16 file (edition must be 'b16').

    Returns:
        dict with keys: "info.txt", "<game_id>.bm" (if bm_path given), "<game_id>.b16" (if b16_path given).
        Values are absolute paths to created files.

    Raises:
        ValidationError for invalid inputs, edition mismatches, or missing at least one cart path.
    """
    # Validate game_id
    need(re.match(r"^[a-z0-9][a-z0-9-]{0,22}$", game_id), "game_id must match ^[a-z0-9][a-z0-9-]{0,22}$", "game_id")

    # Validate version
    need(re.match(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,14}$", version), "version must match ^[A-Za-z0-9][A-Za-z0-9._-]{0,14}$", "version")

    # Validate license: 1-40 printable ASCII
    need(1 <= len(license) <= 40, "license must be 1-40 characters", "license")
    need(all(32 <= ord(c) <= 126 for c in license), "license must be printable ASCII", "license")

    # Validate about: 0-120 printable ASCII
    need(0 <= len(about) <= 120, "about must be 0-120 characters", "about")
    need(all(32 <= ord(c) <= 126 for c in about), "about must be printable ASCII", "about")

    # At least one path
    need(bm_path is not None or b16_path is not None, "at least one of bm_path or b16_path must be given", "paths")

    # Check for edition conversions
    if bm_path is not None:
        ed = C.edition_of(bm_path)
        need(ed == "bm", "bm_path must end with .bm, not .b16 or other", "bm_path")

    if b16_path is not None:
        ed = C.edition_of(b16_path)
        need(ed == "b16", "b16_path must end with .b16, not .bm or other", "b16_path")

    # Create target directory and check for .bme project
    target_dir = os.path.join(root, game_id)
    if os.path.isdir(target_dir):
        bme_path = os.path.join(target_dir, f"{game_id}.bme")
        need(not os.path.exists(bme_path), f"target holds a .bme project {bme_path!r}", "target")

    os.makedirs(target_dir, exist_ok=True)

    # Read and validate source files
    result = {}

    if bm_path is not None:
        cart = C.read_file(bm_path)
        # Validate header title is non-empty
        need(cart.header.title, "cartridge title must be non-empty", "title")
        # Read source bytes
        with open(bm_path, "rb") as f:
            bm_data = f.read()
        # Check B16 size limit doesn't apply to .bm but let's be thorough
        # Write to target
        bm_target = os.path.join(target_dir, f"{game_id}.bm")
        with open(bm_target, "wb") as f:
            f.write(bm_data)
        result["bm"] = bm_target

    if b16_path is not None:
        cart = C.read_file(b16_path)
        # Validate header title is non-empty
        need(cart.header.title, "cartridge title must be non-empty", "title")
        # Check B16 size limit
        with open(b16_path, "rb") as f:
            b16_data = f.read()
        need(len(b16_data) <= B16_MAX_BYTES, f"b16 cartridge {len(b16_data)} bytes, max {B16_MAX_BYTES}", "b16_size")
        # Write to target
        b16_target = os.path.join(target_dir, f"{game_id}.b16")
        with open(b16_target, "wb") as f:
            f.write(b16_data)
        result["b16"] = b16_target

    # Write info.txt
    info_path = os.path.join(target_dir, "info.txt")
    info_text = f"version: {version}\nlicense: {license}\nabout: {about}\n"
    with open(info_path, "w", encoding="utf-8") as f:
        f.write(info_text)
    result["info.txt"] = info_path

    return result


def about_line(game_id, license, about):
    """Format a line for market/about.txt.

    Args:
        game_id: str, the game's id.
        license: str, the license.
        about: str, the about text.

    Returns:
        str formatted as "{game_id}: {license} | {about}".
    """
    return f"{game_id}: {license} | {about}"
