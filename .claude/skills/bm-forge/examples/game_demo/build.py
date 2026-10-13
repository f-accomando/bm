#!/usr/bin/env python3
"""Build forgedemo assets, previews, and cartridges.

    python examples/game_demo/build.py [--out DIR]

Outputs .bm/.b16 cartridges, .bmm model file, preview images, and repo/Market folders.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SKILL = os.path.abspath(os.path.join(HERE, "..", ".."))
if SKILL not in sys.path:
    sys.path.insert(0, SKILL)

import bm_core as B  # noqa: E402
from bm_core import animgen as AG, export, preview as P, shapes, skelgen as SG  # noqa: E402

GAME = "forgedemo"
TITLE = "Forge Demo"
AUTHOR = "bm-forge"

EX = os.path.join(SKILL, "examples")


def build_assets():
    """Build humanoid + rig + clips + ground.

    Returns:
        (Mesh, Anim) with humanoid and ground models, and one rig with idle/walk/jump clips.
    """
    # Build humanoid with skeleton and bindings
    rig = SG.skeleton(os.path.join(EX, "humanoid.json"))
    humanoid, _ = shapes.load_boxes(os.path.join(EX, "humanoid_boxes.json"))
    rig = SG.bind(humanoid, rig, strategy="nearest_bone_part")
    rig = AG.add_clips(rig, [os.path.join(EX, n + ".json") for n in ("idle", "walk", "jump")])

    # Load ground box
    ground, _ = shapes.load_boxes(os.path.join(HERE, "ground_boxes.json"))

    # Return mesh with humanoid first, then ground (as expected by main.lua check 3)
    return B.Mesh([humanoid, ground]), B.Anim([rig])


def make_cover(mesh):
    """Render a cover image of the humanoid model.

    Args:
        mesh: Mesh object.

    Returns:
        Image for the cover section.
    """
    humanoid = mesh.get("humanoid")
    return P.render_mesh(humanoid, size=(88, 88), yaw=0.6, pitch=0.2, background=0x8CC8F0)


def main(out):
    """Build and export all assets.

    Args:
        out: output directory path.

    Returns:
        dict of output file paths.
    """
    # Ensure output directory exists
    os.makedirs(out, exist_ok=True)

    # Build assets
    mesh, anim = build_assets()
    cover = make_cover(mesh)
    draws = {"humanoid": 1, "ground": 1}

    # Read Lua source files
    with open(os.path.join(HERE, "bm", "main.lua"), "rb") as f:
        bm_lua = f.read()
    with open(os.path.join(HERE, "b16", "main.lua"), "rb") as f:
        b16_lua = f.read()

    # Build cartridges
    bm_path = os.path.join(out, f"{GAME}.bm")
    export.export_cart(bm_path, bm_lua, mesh, anim, TITLE, AUTHOR, res=(320, 180), cover=cover, draws=draws)

    b16_path = os.path.join(out, f"{GAME}.b16")
    export.export_cart(b16_path, b16_lua, mesh, anim, TITLE, AUTHOR, res=(360, 360), cover=cover, draws=draws)

    # Export HUMANOID.BMM
    humanoid_model = mesh.get("humanoid")
    bmm_path = os.path.join(out, "HUMANOID.BMM")
    export.export_bmm(bmm_path, B.Mesh([humanoid_model]), anim, TITLE, AUTHOR)

    # Export repo folder
    repo_dir = os.path.join(out, "repo")
    os.makedirs(repo_dir, exist_ok=True)
    repo_result = export.export_folder(repo_dir, GAME, bm_lua, mesh, anim, TITLE, AUTHOR, res=(320, 180), cover=cover, overwrite=True)

    # Export market folder
    market_dir = os.path.join(out, "market")
    os.makedirs(market_dir, exist_ok=True)
    market_result = export.export_market(market_dir, GAME, "1.0", "MIT",
                                         "bm-forge example: a generated humanoid with idle, walk and jump clips.",
                                         bm_path, b16_path)

    # Render animation previews
    rig = anim.rigs[0]
    preview_files = {}
    for clip_name in ["idle", "walk", "jump"]:
        imgs, _ = P.render_anim(humanoid_model, rig, clip_name, fps=12, size=(96, 96), yaw=0.9, pitch=0.2)
        sheet = P.contact_sheet(imgs, cols=6)
        preview_path = os.path.join(out, f"preview_{clip_name}.png")
        sheet.save_png(preview_path)
        preview_files[f"preview_{clip_name}.png"] = preview_path

    # Save cover image
    cover_path = os.path.join(out, "cover.png")
    cover.save_png(cover_path)

    # Assemble return dict
    result = {
        f"{GAME}.bm": bm_path,
        f"{GAME}.b16": b16_path,
        "HUMANOID.BMM": bmm_path,
        "cover.png": cover_path,
    }
    result.update(preview_files)
    result.update(repo_result)
    result.update(market_result)

    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Build forgedemo assets")
    parser.add_argument("--out", default=os.path.join(HERE, "out"), help="output directory (default: examples/game_demo/out)")
    args = parser.parse_args()

    out = os.path.abspath(args.out)
    result = main(out)
    print(f"{GAME} built: {out}")
