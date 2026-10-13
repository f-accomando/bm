#!/usr/bin/env python3
"""bm-forge: mesh, skeleton, animation, export CLI.

    python bm_forge.py mesh INPUT [INPUT ...] -o OUT [--title T] [--author A] [--max-tris N] [--kernel-limits]
    python bm_forge.py skel SKELETON.json --in FILE -o OUT [--strategy nearest_bone|nearest_bone_part]
    python bm_forge.py anim CLIP.json [CLIP.json ...] --in FILE -o OUT [--rig MODEL] [--replace]
    python bm_forge.py export models|bmm|cart|folder|market ...
    python bm_forge.py check ... (not implemented)
    python bm_forge.py preview ... (not implemented)
    python bm_forge.py doctor ... (not implemented)
"""
import argparse
import json
import os
import sys

SKILL = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
if SKILL not in sys.path:
    sys.path.insert(0, SKILL)

import bm_core as B
from bm_core import ValidationError, animgen, container, export, glb, mesh as meshmod, shapes, skelgen


def build_parser():
    """Build the argument parser."""
    parser = argparse.ArgumentParser(prog="bm-forge")
    subparsers = parser.add_subparsers(dest="command", help="subcommand")

    # mesh
    mesh_parser = subparsers.add_parser("mesh", help="Build a mesh from boxes or glb files")
    mesh_parser.add_argument("input", nargs="+", help="Input .json (boxes) or .glb files")
    mesh_parser.add_argument("-o", "--out", required=True, help="Output .bm or .bmm file")
    mesh_parser.add_argument("--title", default="", help="Cartridge title")
    mesh_parser.add_argument("--author", default="", help="Cartridge author")
    mesh_parser.add_argument("--max-tris", type=int, help="Max total triangles (budget check)")
    mesh_parser.add_argument("--kernel-limits", action="store_true", help="Use kernel limits (65535 verts/faces)")

    # skel
    skel_parser = subparsers.add_parser("skel", help="Bind a skeleton to a model")
    skel_parser.add_argument("skeleton", help="Skeleton description .json")
    skel_parser.add_argument("--in", dest="in_file", required=True, help="Input cartridge file (.bm/.b16/.bme)")
    skel_parser.add_argument("-o", "--out", required=True, help="Output cartridge file")
    skel_parser.add_argument("--strategy", default="nearest_bone_part",
                           choices=["nearest_bone", "nearest_bone_part"],
                           help="Vertex binding strategy")

    # anim
    anim_parser = subparsers.add_parser("anim", help="Add animation clips to a rig")
    anim_parser.add_argument("clips", nargs="+", help="Animation clip .json files")
    anim_parser.add_argument("--in", dest="in_file", required=True, help="Input cartridge file")
    anim_parser.add_argument("-o", "--out", required=True, help="Output cartridge file")
    anim_parser.add_argument("--rig", help="Model name (required if multiple rigs)")
    anim_parser.add_argument("--replace", action="store_true", help="Replace existing clips by name")

    # export
    export_parser = subparsers.add_parser("export", help="Export a cartridge")
    export_subs = export_parser.add_subparsers(dest="export_cmd", help="export subcommand")

    # export models
    export_models_p = export_subs.add_parser("models", help="Export a models cartridge")
    export_models_p.add_argument("from_file", metavar="FROM", help="Input cartridge (.bm/.b16/.bme)")
    export_models_p.add_argument("-o", "--out", required=True, help="Output .bm file")
    export_models_p.add_argument("--title", default="", help="Cartridge title")
    export_models_p.add_argument("--author", default="", help="Cartridge author")

    # export bmm
    export_bmm_p = export_subs.add_parser("bmm", help="Export a BMRES model file")
    export_bmm_p.add_argument("from_file", metavar="FROM", help="Input cartridge (.bm/.b16/.bme)")
    export_bmm_p.add_argument("-o", "--out", required=True, help="Output .bmm file")
    export_bmm_p.add_argument("--model", action="append", dest="models", help="Model name to include (repeatable)")

    # export cart
    export_cart_p = export_subs.add_parser("cart", help="Export a game cartridge")
    export_cart_p.add_argument("from_file", metavar="FROM", help="Input cartridge (.bm/.b16/.bme)")
    export_cart_p.add_argument("--lua", required=True, help="Main Lua file")
    export_cart_p.add_argument("-o", "--out", required=True, help="Output .bm or .b16 file")
    export_cart_p.add_argument("--title", required=True, help="Cartridge title")
    export_cart_p.add_argument("--author", default="", help="Cartridge author")
    export_cart_p.add_argument("--res", help="Resolution WxH (default 360x360 for .b16 else 320x180)")
    export_cart_p.add_argument("--cover-model", help="Model name to render for cover")
    export_cart_p.add_argument("--draw", action="append", dest="draws", help="Model draw count (repeatable, NAME=COUNT)")
    export_cart_p.add_argument("--budget", type=int, help="Triangle budget")

    # export folder
    export_folder_p = export_subs.add_parser("folder", help="Export a game folder")
    export_folder_p.add_argument("from_file", metavar="FROM", help="Input cartridge (.bm/.b16/.bme)")
    export_folder_p.add_argument("--lua", required=True, help="Main Lua file")
    export_folder_p.add_argument("--root", required=True, help="Root directory for game folder")
    export_folder_p.add_argument("--game", required=True, help="Game ID (lowercase, alphanumeric and underscores)")
    export_folder_p.add_argument("--title", required=True, help="Cartridge title")
    export_folder_p.add_argument("--author", default="", help="Cartridge author")
    export_folder_p.add_argument("--res", help="Resolution WxH (default 320x180)")
    export_folder_p.add_argument("--cover-model", help="Model name to render for cover")
    export_folder_p.add_argument("--overwrite", action="store_true", help="Overwrite existing game folder")

    # export market
    export_market_p = export_subs.add_parser("market", help="Export to Market layout")
    export_market_p.add_argument("--root", required=True, help="Root directory for Market")
    export_market_p.add_argument("--id", required=True, help="Game ID")
    export_market_p.add_argument("--version", required=True, help="Version string")
    export_market_p.add_argument("--license", required=True, help="License string")
    export_market_p.add_argument("--about", required=True, help="About text")
    export_market_p.add_argument("--bm", help="Path to .bm file to include")
    export_market_p.add_argument("--b16", help="Path to .b16 file to include")

    # check
    check_parser = subparsers.add_parser("check", help="Validate files; report limits, budget, drift, similarity")
    check_parser.add_argument("files", nargs="+", metavar="FILE", help="Cartridges (.bm/.b16/.bme) or BMRES files")
    check_parser.add_argument("--kernel-limits", action="store_true", help="Use kernel limits (65535 verts/faces)")
    check_parser.add_argument("--budget", type=int, help="Triangle budget a frame (enforced when given)")
    check_parser.add_argument("--draw", action="append", dest="draws", help="Model draw count (repeatable, NAME=COUNT); enforces the budget")
    check_parser.add_argument("--reference", help="Reference .png or cart/BMRES to compare the silhouette with (one FILE)")
    check_parser.add_argument("--model", help="Model of FILE to compare (default the first)")
    check_parser.add_argument("--ref-model", help="Model of the reference cart (default the same name, else the first)")
    check_parser.add_argument("--view", default="front", choices=["front", "side", "top"], help="View for a .png reference")
    check_parser.add_argument("--min-iou", type=float, help="Minimum IoU of the similarity (default 0.75)")
    check_parser.add_argument("--no-sync", action="store_true", help="Skip the drift check against the repo sources")
    check_parser.add_argument("--json", action="store_true", help="Print only a JSON object")

    # preview (stub)
    preview_parser = subparsers.add_parser("preview", help="Preview a cartridge")
    preview_parser.add_argument("input", nargs="*", help="(not implemented)")

    # doctor (stub)
    doctor_parser = subparsers.add_parser("doctor", help="Diagnose a cartridge")
    doctor_parser.add_argument("input", nargs="*", help="(not implemented)")

    return parser


def cmd_mesh(args):
    """mesh INPUT [...] -o OUT [--title T] [--author A] [--max-tris N] [--kernel-limits]"""
    try:
        # Check output extension
        out_ext = os.path.splitext(args.out)[1].lower()
        if out_ext not in (".bm", ".bmm"):
            print(f"error: output must end .bm or .bmm, got {out_ext!r}", file=sys.stderr)
            return 2

        # Check for same input/output path
        for inp in args.input:
            if os.path.abspath(inp) == os.path.abspath(args.out):
                print(f"error: input and output paths cannot be the same", file=sys.stderr)
                return 2

        # Load models from inputs
        models = []
        inset = None

        for inp in args.input:
            if not os.path.exists(inp):
                print(f"error: input file not found: {inp}", file=sys.stderr)
                return 2

            inp_ext = os.path.splitext(inp)[1].lower()

            if inp_ext == ".json":
                # Load boxes
                try:
                    model, _ = shapes.load_boxes(inp)
                    models.append(model)
                except Exception as e:
                    print(f"error: {e}", file=sys.stderr)
                    return 2
            elif inp_ext == ".glb":
                # Load glb
                try:
                    with open(inp, "rb") as f:
                        data = f.read()
                    m, _ = glb.glb_models(data)
                    if inset is None:
                        inset = m.inset
                    elif inset != m.inset:
                        print(f"error: inputs disagree on the MESH inset", file=sys.stderr)
                        return 1
                    models.extend(m.models)
                except ValidationError as e:
                    print(f"error: {e}", file=sys.stderr)
                    return 1
                except Exception as e:
                    print(f"error: {e}", file=sys.stderr)
                    return 2
            else:
                print(f"error: unsupported input extension {inp_ext!r}", file=sys.stderr)
                return 2

        # Check for textured faces
        for model in models:
            for face in model.faces:
                if (face.colour & meshmod.TEXTURED):
                    print(f"error: textured .glb: use scripts/mkbm.py --models (bm-forge does not import sheets)",
                          file=sys.stderr)
                    return 1

        # Build mesh
        mesh = B.Mesh(models, inset if inset is not None else 64)

        # Check max triangles
        if args.max_tris is not None:
            total_faces = sum(len(m.faces) for m in models)
            if total_faces > args.max_tris:
                print(f"error: {total_faces} triangles, budget {args.max_tris}", file=sys.stderr)
                return 1

        # Validate
        try:
            if args.kernel_limits:
                meshmod.validate(mesh, kernel_limits=True)
            else:
                meshmod.validate(mesh)
        except ValidationError as e:
            print(f"error: {e}", file=sys.stderr)
            return 1

        # Export
        try:
            if out_ext == ".bm":
                export.export_models(args.out, mesh, None, args.title, args.author, kernel_limits=args.kernel_limits)
            else:  # .bmm
                export.export_bmm(args.out, mesh, None, args.title, args.author, kernel_limits=args.kernel_limits)

            total_faces = sum(len(m.faces) for m in models)
            print(f"wrote {args.out}: {len(models)} models, {total_faces} triangles")
            return 0
        except ValidationError as e:
            print(f"error: {e}", file=sys.stderr)
            return 1
        except Exception as e:
            print(f"error: {e}", file=sys.stderr)
            return 2

    except SystemExit as e:
        return e.code if isinstance(e.code, int) else 2


def cmd_skel(args):
    """skel SKELETON.json --in FILE -o OUT [--strategy ...]"""
    try:
        # Check output path
        if os.path.abspath(args.in_file) == os.path.abspath(args.out):
            print(f"error: input and output paths cannot be the same", file=sys.stderr)
            return 2

        # Load skeleton
        try:
            if not os.path.exists(args.skeleton):
                print(f"error: skeleton file not found: {args.skeleton}", file=sys.stderr)
                return 2
            rig = skelgen.skeleton(args.skeleton)
        except ValidationError as e:
            print(f"error: {e}", file=sys.stderr)
            return 1
        except Exception as e:
            print(f"error: {e}", file=sys.stderr)
            return 2

        # Load cartridge
        try:
            if not os.path.exists(args.in_file):
                print(f"error: input file not found: {args.in_file}", file=sys.stderr)
                return 2
            cart = container.read_file(args.in_file)
        except ValidationError as e:
            print(f"error: {e}", file=sys.stderr)
            return 1
        except Exception as e:
            print(f"error: {e}", file=sys.stderr)
            return 2

        # Get model from cartridge
        mesh = cart.mesh()
        if mesh is None:
            print(f"error: no mesh in {args.in_file}", file=sys.stderr)
            return 2

        model = mesh.get(rig.model)
        if model is None:
            print(f"error: no model {rig.model!r} in {args.in_file}", file=sys.stderr)
            return 1

        # Bind skeleton to model
        try:
            rig = skelgen.bind(model, rig, strategy=args.strategy)
        except ValidationError as e:
            print(f"error: {e}", file=sys.stderr)
            return 1

        # Create animation with this rig
        anim_obj = cart.anim()
        if anim_obj is None or not anim_obj.rigs:
            # Create new anim with single rig
            anim_obj = B.Anim([rig])
        else:
            # Replace or append rig for this model
            old_rig_idx = None
            for i, r in enumerate(anim_obj.rigs):
                if r.model == rig.model:
                    old_rig_idx = i
                    break
            if old_rig_idx is not None:
                old_rig = anim_obj.rigs[old_rig_idx]
                dropped_clips = len(old_rig.clips)
                anim_obj.rigs[old_rig_idx] = rig
                print(f"replaced rig {rig.model} ({dropped_clips} clips dropped)")
            else:
                anim_obj.rigs.append(rig)

        cart.set_anim(anim_obj)

        # Get source edition from input file
        source_edition = container.edition_of(args.in_file)

        # Write output
        try:
            out_edition = container.edition_of(args.out)
            if source_edition != out_edition:
                print(f"error: editions are never converted", file=sys.stderr)
                return 2
            container.write_file(args.out, cart, source_edition=source_edition)
            print(f"wrote {args.out}: rig {rig.model}, {len(rig.bones)} bones, root {rig.bones[0].name}")
            return 0
        except ValidationError as e:
            print(f"error: {e}", file=sys.stderr)
            return 1 if "edition" in str(e).lower() else 2

    except SystemExit as e:
        return e.code if isinstance(e.code, int) else 2


def cmd_anim(args):
    """anim CLIP.json [...] --in FILE -o OUT [--rig MODEL] [--replace]"""
    try:
        # Check output path
        if os.path.abspath(args.in_file) == os.path.abspath(args.out):
            print(f"error: input and output paths cannot be the same", file=sys.stderr)
            return 2

        # Load cartridge
        try:
            if not os.path.exists(args.in_file):
                print(f"error: input file not found: {args.in_file}", file=sys.stderr)
                return 2
            cart = container.read_file(args.in_file)
        except ValidationError as e:
            print(f"error: {e}", file=sys.stderr)
            return 1
        except Exception as e:
            print(f"error: {e}", file=sys.stderr)
            return 2

        # Get animation
        anim_obj = cart.anim()
        if anim_obj is None or not anim_obj.rigs:
            print(f"error: no rig: run skel first", file=sys.stderr)
            return 1

        # Determine which rig to use
        rig_idx = 0
        if len(anim_obj.rigs) > 1:
            if not args.rig:
                print(f"error: no rig: run skel first", file=sys.stderr)
                return 1
            rig_idx = None
            for i, r in enumerate(anim_obj.rigs):
                if r.model == args.rig:
                    rig_idx = i
                    break
            if rig_idx is None:
                print(f"error: no rig {args.rig!r}", file=sys.stderr)
                return 1

        rig = anim_obj.rigs[rig_idx]

        # Process clips
        clip_names = set()
        clip_order = []
        for clip_path in args.clips:
            if not os.path.exists(clip_path):
                print(f"error: clip file not found: {clip_path}", file=sys.stderr)
                return 2
            try:
                clip_desc = animgen.load(clip_path)
                clip_names.add(clip_desc["name"])
                clip_order.append(clip_desc["name"])
            except Exception as e:
                print(f"error: {e}", file=sys.stderr)
                return 2

        # Check for duplicates if not replacing
        if not args.replace:
            existing = {c.name for c in rig.clips}
            duplicates = clip_names & existing
            if duplicates:
                print(f"error: duplicate clip names: {', '.join(sorted(duplicates))}", file=sys.stderr)
                return 1

        # If replacing, remove existing clips
        if args.replace:
            rig.clips = [c for c in rig.clips if c.name not in clip_names]

        # Add clips
        reports = []
        try:
            rig = animgen.add_clips(rig, args.clips, reports=reports)
        except ValidationError as e:
            print(f"error: {e}", file=sys.stderr)
            return 1

        # Print clip reports
        for name, report in zip(clip_order, reports):
            frames = report["frames"]
            mode = B.anim.MODES.get(report["mode"], "unknown")
            keys = report["keys"]
            baked = "yes" if report.get("baked") else "no"
            msg = f"clip {name}: {frames} frames, mode {mode}, {keys} keys, baked {baked}"
            if report.get("snapped"):
                msg += f" (snapped {len(report['snapped'])} times)"
            print(msg)

        # Update rig in animation (add_clips may return modified rig)
        anim_obj.rigs[rig_idx] = rig
        cart.set_anim(anim_obj)

        # Get source edition
        source_edition = container.edition_of(args.in_file)

        # Write output
        try:
            out_edition = container.edition_of(args.out)
            if source_edition != out_edition:
                print(f"error: editions are never converted", file=sys.stderr)
                return 2
            container.write_file(args.out, cart, source_edition=source_edition)
            print(f"wrote {args.out}: rig {rig.model}, {len(rig.clips)} clips")
            return 0
        except ValidationError as e:
            print(f"error: {e}", file=sys.stderr)
            return 2

    except SystemExit as e:
        return e.code if isinstance(e.code, int) else 2


def cmd_export(args):
    """export models|bmm|cart|folder|market ..."""
    try:
        if not args.export_cmd:
            print("error: export subcommand required (models, bmm, cart, folder, market)", file=sys.stderr)
            return 2

        if args.export_cmd == "models":
            return _export_models(args)
        elif args.export_cmd == "bmm":
            return _export_bmm(args)
        elif args.export_cmd == "cart":
            return _export_cart(args)
        elif args.export_cmd == "folder":
            return _export_folder(args)
        elif args.export_cmd == "market":
            return _export_market(args)
        else:
            print(f"error: unknown export subcommand {args.export_cmd}", file=sys.stderr)
            return 2

    except SystemExit as e:
        return e.code if isinstance(e.code, int) else 2


def _export_models(args):
    """export models FROM -o OUT [--title] [--author]"""
    try:
        # Load cartridge
        if not os.path.exists(args.from_file):
            print(f"error: input file not found: {args.from_file}", file=sys.stderr)
            return 2
        try:
            cart = container.read_file(args.from_file)
        except ValidationError as e:
            print(f"error: {e}", file=sys.stderr)
            return 1
        except Exception as e:
            print(f"error: {e}", file=sys.stderr)
            return 2

        # Get mesh
        mesh = cart.mesh()
        if mesh is None:
            print(f"error: no MESH in {args.from_file}", file=sys.stderr)
            return 1

        # Get anim
        anim = cart.anim()

        # Export
        try:
            export.export_models(args.out, mesh, anim, args.title, args.author)
            total_faces = sum(len(m.faces) for m in mesh.models)
            print(f"wrote {args.out}: {len(mesh.models)} models, {total_faces} triangles")
            return 0
        except ValidationError as e:
            print(f"error: {e}", file=sys.stderr)
            return 1
        except Exception as e:
            print(f"error: {e}", file=sys.stderr)
            return 2

    except SystemExit as e:
        return e.code if isinstance(e.code, int) else 2


def _export_bmm(args):
    """export bmm FROM -o OUT [--model NAME ...]"""
    try:
        # Load cartridge
        if not os.path.exists(args.from_file):
            print(f"error: input file not found: {args.from_file}", file=sys.stderr)
            return 2
        try:
            cart = container.read_file(args.from_file)
        except ValidationError as e:
            print(f"error: {e}", file=sys.stderr)
            return 1
        except Exception as e:
            print(f"error: {e}", file=sys.stderr)
            return 2

        # Get mesh
        mesh = cart.mesh()
        if mesh is None:
            print(f"error: no MESH in {args.from_file}", file=sys.stderr)
            return 1

        # Get anim
        anim = cart.anim()

        # Filter models if --model given
        if args.models:
            # Validate all model names exist
            for model_name in args.models:
                if mesh.get(model_name) is None:
                    print(f"error: no model {model_name!r}", file=sys.stderr)
                    return 1
            # Build filtered mesh with only selected models and their rigs
            selected_models = [m for m in mesh.models if m.name in args.models]
            mesh = B.Mesh(selected_models, mesh.inset)
            # Filter anim to only include rigs of selected models
            if anim is not None:
                selected_rigs = [r for r in anim.rigs if r.model in args.models]
                if selected_rigs:
                    anim = B.Anim(selected_rigs)
                else:
                    anim = None

        # Export
        try:
            export.export_bmm(args.out, mesh, anim)
            total_faces = sum(len(m.faces) for m in mesh.models)
            print(f"wrote {args.out}: {len(mesh.models)} models, {total_faces} triangles")
            return 0
        except ValidationError as e:
            print(f"error: {e}", file=sys.stderr)
            return 1
        except Exception as e:
            print(f"error: {e}", file=sys.stderr)
            return 2

    except SystemExit as e:
        return e.code if isinstance(e.code, int) else 2


def _export_cart(args):
    """export cart FROM --lua MAIN.lua -o OUT.bm|OUT.b16 --title T [--author A] [--res WxH] [--cover-model NAME] [--draw NAME=COUNT ...] [--budget N]"""
    try:
        # Load cartridge
        if not os.path.exists(args.from_file):
            print(f"error: input file not found: {args.from_file}", file=sys.stderr)
            return 2
        try:
            cart = container.read_file(args.from_file)
        except ValidationError as e:
            print(f"error: {e}", file=sys.stderr)
            return 1
        except Exception as e:
            print(f"error: {e}", file=sys.stderr)
            return 2

        # Get mesh
        mesh = cart.mesh()
        if mesh is None:
            print(f"error: no MESH in {args.from_file}", file=sys.stderr)
            return 1

        # Get anim
        anim = cart.anim()

        # Read Lua
        if not os.path.exists(args.lua):
            print(f"error: lua file not found: {args.lua}", file=sys.stderr)
            return 2
        try:
            with open(args.lua, "rb") as f:
                lua = f.read()
        except Exception as e:
            print(f"error: reading lua: {e}", file=sys.stderr)
            return 2

        # Parse resolution
        res = (320, 180)  # default for .bm
        out_ed = container.edition_of(args.out)
        if out_ed == "b16":
            res = (360, 360)  # default for .b16
        if args.res:
            try:
                parts = args.res.split("x")
                if len(parts) != 2:
                    print(f"error: malformed --res {args.res!r}, expected WxH", file=sys.stderr)
                    return 2
                res = (int(parts[0]), int(parts[1]))
            except ValueError:
                print(f"error: malformed --res {args.res!r}, expected WxH", file=sys.stderr)
                return 2

        # Parse draws
        draws = None
        if args.draws:
            draws = {}
            for draw_spec in args.draws:
                if "=" not in draw_spec:
                    print(f"error: malformed --draw {draw_spec!r}, expected NAME=COUNT", file=sys.stderr)
                    return 2
                name, count_str = draw_spec.split("=", 1)
                try:
                    draws[name] = int(count_str)
                except ValueError:
                    print(f"error: malformed --draw {draw_spec!r}, expected NAME=COUNT", file=sys.stderr)
                    return 2

        # Render cover if --cover-model given
        cover = None
        if args.cover_model:
            try:
                from bm_core import preview
                model = mesh.get(args.cover_model)
                if model is None:
                    print(f"error: no model {args.cover_model!r} for cover", file=sys.stderr)
                    return 1
                cover = preview.render_mesh(model, size=(88, 88), yaw=0.6, pitch=0.2, background=0x8CC8F0)
            except Exception as e:
                print(f"error: rendering cover: {e}", file=sys.stderr)
                return 2

        # Export
        try:
            export.export_cart(args.out, lua, mesh, anim, args.title, args.author, res=res, cover=cover, draws=draws, budget=args.budget)

            # Print info
            ed = "b16" if out_ed == "b16" else "bm"
            if draws is None:
                draws = {m.name: 1 for m in mesh.models}
            frame_tris = export.frame_triangles(mesh, draws)
            actual_budget = args.budget or (export.TRI_BUDGET_B16 if ed == "b16" else export.TRI_BUDGET_PI)
            print(f"wrote {args.out}: {ed} {res[0]}x{res[1]}, {frame_tris} triangles a frame (budget {actual_budget})")
            return 0
        except ValidationError as e:
            print(f"error: {e}", file=sys.stderr)
            return 1
        except Exception as e:
            print(f"error: {e}", file=sys.stderr)
            return 2

    except SystemExit as e:
        return e.code if isinstance(e.code, int) else 2


def _export_folder(args):
    """export folder FROM --lua MAIN.lua --root DIR --game ID --title T [--author] [--res] [--cover-model] [--overwrite]"""
    try:
        # Load cartridge
        if not os.path.exists(args.from_file):
            print(f"error: input file not found: {args.from_file}", file=sys.stderr)
            return 2
        try:
            cart = container.read_file(args.from_file)
        except ValidationError as e:
            print(f"error: {e}", file=sys.stderr)
            return 1
        except Exception as e:
            print(f"error: {e}", file=sys.stderr)
            return 2

        # Get mesh
        mesh = cart.mesh()
        if mesh is None:
            print(f"error: no MESH in {args.from_file}", file=sys.stderr)
            return 1

        # Get anim
        anim = cart.anim()

        # Read Lua
        if not os.path.exists(args.lua):
            print(f"error: lua file not found: {args.lua}", file=sys.stderr)
            return 2
        try:
            with open(args.lua, "rb") as f:
                lua = f.read()
        except Exception as e:
            print(f"error: reading lua: {e}", file=sys.stderr)
            return 2

        # Parse resolution
        res = (320, 180)
        if args.res:
            try:
                parts = args.res.split("x")
                if len(parts) != 2:
                    print(f"error: malformed --res {args.res!r}, expected WxH", file=sys.stderr)
                    return 2
                res = (int(parts[0]), int(parts[1]))
            except ValueError:
                print(f"error: malformed --res {args.res!r}, expected WxH", file=sys.stderr)
                return 2

        # Render cover if --cover-model given
        cover = None
        if args.cover_model:
            try:
                from bm_core import preview
                model = mesh.get(args.cover_model)
                if model is None:
                    print(f"error: no model {args.cover_model!r} for cover", file=sys.stderr)
                    return 1
                cover = preview.render_mesh(model, size=(88, 88), yaw=0.6, pitch=0.2, background=0x8CC8F0)
            except Exception as e:
                print(f"error: rendering cover: {e}", file=sys.stderr)
                return 2

        # Export
        try:
            result = export.export_folder(args.root, args.game, lua, mesh, anim, args.title, args.author, res=res, cover=cover, overwrite=args.overwrite)
            for name, path in result.items():
                print(path)
            return 0
        except ValidationError as e:
            print(f"error: {e}", file=sys.stderr)
            return 1
        except Exception as e:
            print(f"error: {e}", file=sys.stderr)
            return 2

    except SystemExit as e:
        return e.code if isinstance(e.code, int) else 2


def _export_market(args):
    """export market --root DIR --id ID --version V --license L --about TEXT [--bm P] [--b16 P]"""
    try:
        # Export
        try:
            result = export.export_market(args.root, args.id, args.version, args.license, args.about, args.bm, args.b16)
            for name, path in result.items():
                print(path)
            # Print about.txt line
            about_line_text = export.about_line(args.id, args.license, args.about)
            print(f"about.txt line: {about_line_text}")
            return 0
        except ValidationError as e:
            print(f"error: {e}", file=sys.stderr)
            return 1
        except Exception as e:
            print(f"error: {e}", file=sys.stderr)
            return 2

    except SystemExit as e:
        return e.code if isinstance(e.code, int) else 2


def cmd_check(args):
    """check FILE [FILE ...] [--kernel-limits] [--budget N] [--draw NAME=COUNT ...] [--reference REF]
    [--model NAME] [--ref-model NAME] [--view front|side|top] [--min-iou F] [--no-sync] [--json]

    Per file: read and check it, print edition, sections, models, rigs and the triangle budget.
    --reference compares the silhouette of FILE with a .png or a cart. Drift of bm_core against
    the repo sources (syncwatch) unless --no-sync. Exit: 2 usage or missing file, 1 file or
    budget failure, 3 similarity below --min-iou, 4 drift; the first of these that applies."""
    try:
        from bm_core import sections as S, similarity, syncwatch

        if args.reference is not None and len(args.files) != 1:
            print("error: --reference needs exactly one FILE", file=sys.stderr)
            return 2

        draws = None
        if args.draws:
            draws = {}
            for draw_spec in args.draws:
                if "=" not in draw_spec:
                    print(f"error: malformed --draw {draw_spec!r}, expected NAME=COUNT", file=sys.stderr)
                    return 2
                name, count_str = draw_spec.split("=", 1)
                try:
                    draws[name] = int(count_str)
                except ValueError:
                    print(f"error: malformed --draw {draw_spec!r}, expected NAME=COUNT", file=sys.stderr)
                    return 2

        budget_enforced = args.budget is not None or draws is not None
        min_iou = similarity.DEFAULT_MIN_IOU if args.min_iou is None else args.min_iou
        codes, files_out, sim_out = [], [], None

        def say(line):
            if not args.json:
                print(line)

        def reference(mesh):
            """--reference of the one FILE -> (detail lines, similarity dict or None, exit code or None)"""
            ref = args.reference
            if not os.path.exists(ref):
                print(f"error: reference file not found: {ref}", file=sys.stderr)
                return [], None, 2
            if mesh is None or not mesh.models:
                return ["  FAIL similarity: no MESH to compare"], None, 1
            model = mesh.get(args.model) if args.model else mesh.models[0]
            if model is None:
                print(f"error: no model {args.model!r} in {args.files[0]}", file=sys.stderr)
                return [], None, 2
            if ref.lower().endswith(".png"):
                try:
                    with open(ref, "rb") as f:
                        iou = similarity.compare_png(model, f.read(), args.view)
                except ValidationError as e:
                    return [f"  FAIL reference {ref}: {e}"], None, 1
                lines = [f"  iou {args.view}={iou:.4f}"]
                sim = {"ref": ref, "model": model.name, "view": args.view, "iou": iou}
                value = iou
            else:
                try:
                    ref_cart = B.read_file(ref, kernel_limits=args.kernel_limits)
                except ValidationError as e:
                    return [f"  FAIL reference {ref}: {e}"], None, 1
                ref_mesh = ref_cart.mesh(kernel_limits=args.kernel_limits)
                if ref_mesh is None or not ref_mesh.models:
                    return [f"  FAIL reference {ref}: no MESH to compare"], None, 1
                if args.ref_model:
                    ref_model = ref_mesh.get(args.ref_model)
                    if ref_model is None:
                        print(f"error: no model {args.ref_model!r} in {ref}", file=sys.stderr)
                        return [], None, 2
                else:
                    ref_model = ref_mesh.get(model.name)
                    if ref_model is None:
                        ref_model = ref_mesh.models[0]
                res = similarity.compare_models(model, ref_model)
                lines = [f"  iou front={res['front']:.4f} side={res['side']:.4f} top={res['top']:.4f} "
                         f"mean={res['mean']:.4f}"]
                sim = {"ref": ref, "model": model.name, "ref_model": ref_model.name, "front": res["front"],
                       "side": res["side"], "top": res["top"], "mean": res["mean"]}
                value = res["mean"]
            sim["min_iou"] = min_iou
            sim["pass"] = value >= min_iou
            if not sim["pass"]:
                lines.append(f"  FAIL iou: {value:.4f} below --min-iou {min_iou}")
                return lines, sim, 3
            return lines, sim, None

        for path in args.files:
            entry = {"path": path, "ok": False, "error": None, "edition": None, "width": None, "height": None,
                     "sections": [], "models": [], "limits": None, "rigs": [], "triangles": None,
                     "budget": None, "budget_enforced": budget_enforced}
            files_out.append(entry)
            if not os.path.exists(path):
                print(f"error: file not found: {path}", file=sys.stderr)
                entry["error"] = "file not found"
                codes.append(2)
                continue
            try:
                cart = B.read_file(path, kernel_limits=args.kernel_limits)
            except ValidationError as e:
                entry["error"] = str(e)
                codes.append(1)
                say(f"FAIL {path}: {e}")
                continue
            except OSError as e:
                print(f"error: {e}", file=sys.stderr)
                entry["error"] = str(e)
                codes.append(2)
                continue

            hd = cart.header
            ed = container.edition_of_cart(cart)
            names = [S.sec_name(s.type) for s in cart.sections]
            mv, mf = meshmod.limits(args.kernel_limits)
            mesh = cart.mesh(kernel_limits=args.kernel_limits)
            anim = cart.anim(kernel_limits=args.kernel_limits)
            lines = []
            if hd.is_res:
                lines.append(f"  resource kind {hd.kind}")
            else:
                lines.append(f"  edition {ed} {hd.width}x{hd.height}")
                entry["width"], entry["height"] = hd.width, hd.height
            lines.append("  sections " + ", ".join(names))
            entry.update(edition=ed, sections=names, limits=[mv, mf])
            if mesh is not None:
                for m in mesh.models:
                    entry["models"].append({"name": m.name, "verts": len(m.verts), "faces": len(m.faces)})
                    lines.append(f"  model {m.name}: {len(m.verts)} verts, {len(m.faces)} faces (limit {mv}/{mf})")
            if anim is not None:
                for r in anim.rigs:
                    clip_names = [c.name for c in r.clips]
                    entry["rigs"].append({"model": r.model, "bones": len(r.bones), "clips": clip_names})
                    clips_txt = ", ".join(clip_names) or "-"
                    lines.append(f"  rig {r.model}: {len(r.bones)} bones, clips {clips_txt}")

            ok = True
            if mesh is not None and ed in ("bm", "b16"):
                default = export.TRI_BUDGET_B16 if ed == "b16" else export.TRI_BUDGET_PI
                budget = default if args.budget is None else args.budget
                entry["budget"] = budget
                draw_map = draws if draws is not None else {m.name: 1 for m in mesh.models}
                try:
                    tris = export.frame_triangles(mesh, draw_map)
                    entry["triangles"] = tris
                    lines.append(f"  budget: {tris} triangles a frame, budget {budget}")
                    fail = f"{tris} triangles a frame over budget {budget}" if budget_enforced and tris > budget else None
                except ValidationError as e:
                    fail = str(e)
                if fail:
                    lines.append(f"  FAIL budget: {fail}")
                    ok = False
                    entry["error"] = f"budget: {fail}"
                    codes.append(1)

            if args.reference is not None:
                ref_lines, sim_out, ref_code = reference(mesh)
                lines += ref_lines
                if ref_code is not None:
                    codes.append(ref_code)

            entry["ok"] = ok
            say(("OK " if ok else "FAIL ") + path)
            for line in lines:
                say(line)

        sync = {"checked": False, "drift": []}
        if not args.no_sync:
            drift = syncwatch.check()
            sync = {"checked": True, "drift": [[rel, old, new] for rel, old, new in drift]}
            if drift:
                codes.append(4)
            for rel, old, new in drift:
                say(f"DRIFT {rel}: recorded {old}, now {new}")

        if 2 in codes:
            code = 2
        elif 1 in codes:
            code = 1
        elif 3 in codes:
            code = 3
        elif 4 in codes:
            code = 4
        else:
            code = 0
        if args.json:
            print(json.dumps({"files": files_out, "sync": sync, "similarity": sim_out, "exit": code}, sort_keys=True))
        return code

    except SystemExit as e:
        return e.code if isinstance(e.code, int) else 2


def cmd_preview(args):
    """not implemented"""
    print("error: not implemented", file=sys.stderr)
    return 2


def cmd_doctor(args):
    """not implemented"""
    print("error: not implemented", file=sys.stderr)
    return 2


def main(argv=None):
    """Main entry point."""
    parser = build_parser()
    try:
        args = parser.parse_args(argv)
    except SystemExit as e:
        return e.code if isinstance(e.code, int) else 2

    if not args.command:
        parser.print_help()
        return 0

    # Dispatch to subcommand
    cmd_func = globals().get(f"cmd_{args.command}")
    if cmd_func:
        try:
            return cmd_func(args)
        except Exception as e:
            print(f"error: {e}", file=sys.stderr)
            return 2
    else:
        print(f"error: unknown command {args.command}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
