#!/usr/bin/env python3
"""scripts/doc_refs.py - Check paths, commands, and links in documentation.

Usage:
  python scripts/doc_refs.py FILE... [--root DIR] [--final] [--max-lines N]
"""
import argparse
import glob
import os
import re
import sys


REPO_PREFIXES = (
    "docs/",
    "scripts/",
    "src/",
    "carts/",
    "tests/",
    "tools/",
    "manuals/",
    "sdk/",
)
REPO_ROOT_FILES = ("easy_install.sh", "Makefile", "rgb30.mk")


def find_repo_root():
    d = os.path.abspath(os.path.dirname(__file__))
    while d and os.path.dirname(d) != d:
        if os.path.exists(os.path.join(d, "Makefile")):
            return d
        d = os.path.dirname(d)
    return os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))


def clean_token(token):
    # Remove trailing punctuation: , . ; : )
    while token and token[-1] in (",", ".", ";", ":", ")"):
        token = token[:-1]
    # Remove suffix :N or :N-M
    token = re.sub(r":\d+(?:-\d+)?$", "", token)
    while token and token[-1] in (",", ".", ";", ":", ")"):
        token = token[:-1]
    return token


def check_make_command(element_tokens, root, file_path, lineno, errors):
    for i, tok in enumerate(element_tokens):
        if tok == "make" and (i == 0 or element_tokens[i - 1] in ("&&", "||", ";", "|", "sudo", "`", ">", ">>")):
            target = None
            for nxt in element_tokens[i + 1 :]:
                if "=" in nxt or nxt.startswith("-"):
                    continue
                clean_nxt = clean_token(nxt)
                if "<" in clean_nxt or ">" in clean_nxt:
                    break
                target = clean_nxt
                break
            if target:
                if "/" in target:
                    print(f"note: make target not checked {target}")
                else:
                    found = False
                    for mk in ("Makefile", "rgb30.mk"):
                        mk_path = os.path.join(root, mk)
                        if os.path.exists(mk_path):
                            try:
                                content = open(mk_path, encoding="utf-8", errors="replace").read()
                                if re.search(rf"^{re.escape(target)}\s*:", content, re.M) or re.search(
                                    rf"^\.PHONY:.*?\b{re.escape(target)}\b", content, re.M
                                ):
                                    found = True
                                    break
                            except Exception:
                                pass
                    if not found:
                        errors.append((file_path, lineno, f"unknown make target {target}"))


def check_easy_install_command(element_tokens, root, file_path, lineno, errors):
    for i, tok in enumerate(element_tokens):
        if tok in ("./easy_install.sh", "easy_install.sh"):
            sub = None
            for nxt in element_tokens[i + 1 :]:
                if nxt.startswith("-"):
                    continue
                clean_nxt = clean_token(nxt)
                if "<" in clean_nxt or ">" in clean_nxt:
                    break
                sub = clean_nxt
                break
            if sub:
                ei_path = os.path.join(root, "easy_install.sh")
                found = False
                if os.path.exists(ei_path):
                    try:
                        content = open(ei_path, encoding="utf-8", errors="replace").read()
                        if re.search(rf"^\s*{re.escape(sub)}\)", content, re.M):
                            found = True
                    except Exception:
                        pass
                if not found:
                    errors.append((file_path, lineno, f"unknown easy_install.sh command {sub}"))


def check_file(file_path, root, final=False, max_lines=None):
    errors = []
    if not os.path.exists(file_path):
        return [(file_path, 1, f"file not found: {file_path}")]

    try:
        with open(file_path, encoding="utf-8", errors="replace") as f:
            lines = f.readlines()
    except Exception as e:
        return [(file_path, 1, f"cannot read file: {e}")]

    # R6: line count check
    if max_lines is not None and len(lines) > max_lines:
        errors.append((file_path, 1, f"too long: {len(lines)} lines (max {max_lines})"))

    in_fenced_code = False
    in_copy_block = False

    for idx, raw_line in enumerate(lines):
        lineno = idx + 1
        line = raw_line.rstrip("\r\n")
        stripped = line.strip()

        # R5: gap left check
        if final and ("[TO FILL:" in line or "[DA COMPLETARE:" in line):
            errors.append((file_path, lineno, "gap left"))

        # Check block state
        if stripped.startswith("```"):
            in_fenced_code = not in_fenced_code
            continue

        if stripped.startswith("::: copy"):
            in_copy_block = True
            continue

        if in_copy_block and stripped == ":::":
            in_copy_block = False
            continue

        elements = []
        if in_fenced_code or in_copy_block:
            elements.append(line)
        else:
            # Inline backtick spans
            spans = re.findall(r"`([^`]+)`", line)
            elements.extend(spans)

            # R4: markdown links [text](target)
            # Find markdown links that are not images (not preceded by !)
            link_matches = re.finditer(r"(?<!\!)\[([^\]]*)\]\(([^)]+)\)", line)
            for m in link_matches:
                target = m.group(2).strip()
                if target.startswith("http://") or target.startswith("https://") or target.startswith("#"):
                    continue
                target_file = target.split("#")[0].strip()
                if not target_file:
                    continue
                doc_dir = os.path.dirname(os.path.abspath(file_path))
                resolved = os.path.normpath(os.path.join(doc_dir, target_file))
                if not os.path.exists(resolved):
                    errors.append((file_path, lineno, f"broken link {target}"))

        for elem in elements:
            raw_tokens = elem.split()
            # Command checks on the element
            check_make_command(raw_tokens, root, file_path, lineno, errors)
            check_easy_install_command(raw_tokens, root, file_path, lineno, errors)

            # Check individual tokens
            for raw_tok in raw_tokens:
                if "<" in raw_tok or ">" in raw_tok:
                    continue
                tok = clean_token(raw_tok)
                if not tok:
                    continue

                # R1: repo paths
                p = tok[2:] if tok.startswith("./") else tok
                if p.startswith(REPO_PREFIXES) or p in REPO_ROOT_FILES:
                    if "*" in p:
                        matches = glob.glob(os.path.join(root, p))
                        if not matches:
                            errors.append((file_path, lineno, f"missing path {tok}"))
                    elif p.endswith("/"):
                        if not os.path.isdir(os.path.join(root, p)):
                            errors.append((file_path, lineno, f"missing path {tok}"))
                    else:
                        if not os.path.exists(os.path.join(root, p)):
                            errors.append((file_path, lineno, f"missing path {tok}"))

                # R2: console paths
                elif tok.startswith("/"):
                    if tok in ("/carts", "/bm") or tok.startswith(("/carts/", "/bm/")):
                        pass
                    else:
                        errors.append((file_path, lineno, f"unknown console path {tok}"))

    return errors


def main():
    parser = argparse.ArgumentParser(description="Check documentation references, paths and commands.")
    parser.add_argument("files", nargs="*", help="Markdown files to check")
    parser.add_argument("--root", default=None, help="Repository root directory")
    parser.add_argument("--final", action="store_true", help="Disallow TO FILL / DA COMPLETARE gaps")
    parser.add_argument("--max-lines", type=int, default=None, help="Maximum allowed lines per file")
    args = parser.parse_args()

    if not args.files:
        parser.print_usage(sys.stderr)
        return 2

    root = os.path.abspath(args.root) if args.root else find_repo_root()
    if not os.path.isdir(root):
        print(f"Error: root directory not found: {root}", file=sys.stderr)
        return 2

    all_errors = []
    for f in args.files:
        errs = check_file(f, root, final=args.final, max_lines=args.max_lines)
        all_errors.extend(errs)

    if all_errors:
        for fpath, lno, msg in all_errors:
            print(f"{fpath}:{lno}: {msg}")
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
