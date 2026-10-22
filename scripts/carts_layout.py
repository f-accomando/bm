#!/usr/bin/env python3
"""
carts_layout.py - check that projects in carts/ adhere to docs/PROJECT-LAYOUT.md.

Usage:
  python scripts/carts_layout.py [--root DIR]
"""
import argparse
import fnmatch
import os
import re
import sys

VALID_RES = ("640x360", "480x270", "320x180", "256x256", "360x360", "720x720")
ASSET_PATTERNS = ["sheet.png", "map.csv", "map_*.csv", "flags.csv", "sprites.txt", "models.bm", "models.glb"]
BUILD_PATTERNS = ["mk*.py", "build.py"]
RES_PATTERNS = ["*.bmm", "*.bmi", "*.bms", "*.bmt", "*.bmc", "*.bmk"]


def match_destination(filename):
    lower = filename.lower()
    for pat in ASSET_PATTERNS:
        if fnmatch.fnmatch(lower, pat):
            return "assets"
    for pat in BUILD_PATTERNS:
        if fnmatch.fnmatch(lower, pat):
            return "build"
    for pat in RES_PATTERNS:
        if fnmatch.fnmatch(lower, pat):
            return "res"
    return None


def parse_bminfo(path):
    keys = {}
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            for line in f:
                sline = line.strip()
                if sline.startswith("["):
                    break
                if not sline or sline.startswith("#"):
                    continue
                if ":" in sline:
                    k, v = sline.split(":", 1)
                    keys[k.strip()] = v.strip()
    except Exception:
        pass
    return keys


def parse_doc_table(doc_path):
    if not os.path.isfile(doc_path):
        return None
    try:
        with open(doc_path, "r", encoding="utf-8", errors="replace") as f:
            text = f.read()
    except Exception:
        return None

    marker = "## Migrated so far"
    pos = text.find(marker)
    if pos == -1:
        return None

    section = text[pos + len(marker):]
    next_sec = section.find("\n## ")
    if next_sec != -1:
        section = section[:next_sec]

    table_ids = []
    for line in section.splitlines():
        s = line.strip()
        if not s.startswith("|") or not s.endswith("|"):
            continue
        cells = [c.strip() for c in s.split("|")]
        # cells: ['', first_cell, second_cell, '']
        if len(cells) < 3:
            continue
        first_cell = cells[1]
        if "---" in first_cell or first_cell.lower() == "game":
            continue
        ids = re.findall(r"`([^`]+)`", first_cell)
        table_ids.extend(ids)

    return table_ids


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    repo_root = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
    ap.add_argument("--root", default=repo_root, help="repository root directory")
    args = ap.parse_args()

    root = os.path.abspath(args.root)
    carts_dir = os.path.join(root, "carts")
    if not os.path.isdir(carts_dir):
        sys.stderr.write(f"ERROR: carts directory not found: {carts_dir}\n")
        return 2

    # Discover projects
    project_names = []
    for entry in sorted(os.listdir(carts_dir)):
        pdir = os.path.join(carts_dir, entry)
        if os.path.isdir(pdir):
            has_main = os.path.isfile(os.path.join(pdir, "main.lua"))
            has_info = os.path.isfile(os.path.join(pdir, "bm.info"))
            if has_main or has_info:
                project_names.append(entry)

    P = len(project_names)
    M = 0
    O = 0
    E = 0
    with_info = []

    for proj in project_names:
        pdir = os.path.join(carts_dir, proj)
        info_path = os.path.join(pdir, "bm.info")
        has_info = os.path.isfile(info_path)

        if has_info:
            M += 1
            with_info.append(proj)
            proj_errors = 0

            # Parse and check bm.info
            info_keys = parse_bminfo(info_path)
            for req in ("name", "author", "resolution"):
                if req not in info_keys or not info_keys[req]:
                    print(f"ERROR  {proj}: bm.info has no {req}")
                    E += 1
                    proj_errors += 1

            if "resolution" in info_keys and info_keys["resolution"]:
                res_val = info_keys["resolution"]
                if res_val not in VALID_RES:
                    print(f"ERROR  {proj}: resolution {res_val} is not one of 640x360, 480x270, 320x180, 256x256, 360x360, 720x720")
                    E += 1
                    proj_errors += 1

            # Check files at top
            top_files = sorted(os.listdir(pdir))
            for f in top_files:
                fpath = os.path.join(pdir, f)
                if not os.path.isfile(fpath):
                    continue
                dest = match_destination(f)
                if dest:
                    print(f"MISPLACED {proj}/{f}: goes in {dest}/")
                    E += 1
                    proj_errors += 1

            if proj_errors == 0:
                print(f"ok     {proj}")

        else:
            O += 1
            print(f"old    {proj}: no bm.info (old layout)")
            top_files = sorted(os.listdir(pdir))
            for f in top_files:
                fpath = os.path.join(pdir, f)
                if not os.path.isfile(fpath):
                    continue
                dest = match_destination(f)
                if dest:
                    print(f"note  {proj}/{f}: goes in {dest}/ when migrated")

    # Document table checks
    doc_path = os.path.join(root, "docs", "PROJECT-LAYOUT.md")
    table_ids = parse_doc_table(doc_path)
    if table_ids is None:
        print('ERROR  doc: no "Migrated so far" table')
        E += 1
    else:
        for tid in table_ids:
            if tid not in with_info:
                print(f'ERROR  doc: {tid} is in "Migrated so far" but has no bm.info')
                E += 1
        for wid in sorted(with_info):
            if wid not in table_ids:
                print(f'ERROR  doc: {wid} has bm.info but is not in "Migrated so far"')
                E += 1

    print(f"layout: {P} projects, {M} with bm.info, {O} old layout, {E} errors")
    return 1 if E > 0 else 0


if __name__ == "__main__":
    sys.exit(main())
