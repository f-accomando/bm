#!/usr/bin/env python3
"""bm-forge: mesh, skeleton, animation CLI.

    python bm_forge.py mesh INPUT [INPUT ...] -o OUT [--title T] [--author A] [--max-tris N] [--kernel-limits]
    python bm_forge.py skel SKELETON.json --in FILE -o OUT [--strategy nearest_bone|nearest_bone_part]
    python bm_forge.py anim CLIP.json [CLIP.json ...] --in FILE -o OUT [--rig MODEL] [--replace]
    python bm_forge.py export ... (not implemented)
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

    # export (stub)
    export_parser = subparsers.add_parser("export", help="Export a cartridge")
    export_parser.add_argument("input", nargs="*", help="(not implemented)")

    # check (stub)
    check_parser = subparsers.add_parser("check", help="Check a cartridge")
    check_parser.add_argument("input", nargs="*", help="(not implemented)")

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
                if args.kernel_limits:
                    cart = export.models_cart(mesh, None, args.title, args.author)
                    container.write_file(args.out, cart, kernel_limits=True)
                else:
                    export.export_models(args.out, mesh, None, args.title, args.author)
            else:  # .bmm
                if args.kernel_limits:
                    cart = export.models_cart(mesh, None, args.title, args.author)
                    container.write_file(args.out, cart, kernel_limits=True)
                else:
                    export.export_bmm(args.out, mesh, None, args.title, args.author)

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
    """not implemented"""
    print("error: not implemented", file=sys.stderr)
    return 2


def cmd_check(args):
    """not implemented"""
    print("error: not implemented", file=sys.stderr)
    return 2


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
