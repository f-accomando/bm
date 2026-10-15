#!/usr/bin/env python3
"""Checks that modified files stay within a Work Order's perimeter."""
import argparse
import fnmatch
import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))


def extract_tokens(text):
    tokens = []
    for span in re.findall(r"`([^`]+)`", text):
        span = re.sub(r":\d+(?:-\d+)?$", "", span.strip())
        if " " in span or "(" in span:
            continue
        m = re.match(r"^(.*?)(\.\w+)\|\.(\w+)$", span)
        if m:
            expanded = [f"{m.group(1)}{m.group(2)}", f"{m.group(1)}.{m.group(3)}"]
        elif "|" in span:
            expanded = span.split("|")
        else:
            expanded = [span]
        for t in expanded:
            t = t.strip().replace("\\", "/")
            if not t:
                continue
            if "/" in t or re.match(r"^[\w*?\[\]\-]+\.[\w*?\[\]\-]+$", t):
                tokens.append(t)
    return tokens


def parse_perimeter_lines(lines):
    allowed_tokens = []
    forbidden_tokens = []
    for line in lines:
        cleaned = line.strip()
        cleaned = re.sub(r"^[-*#\s]+", "", cleaned)
        cleaned = re.sub(r"^\*+", "", cleaned)
        cleaned_lower = cleaned.lower()
        if cleaned_lower.startswith("perimetro") or cleaned_lower.startswith("file"):
            parts = re.split(r"\bnon\s+toccare\b", cleaned, maxsplit=1, flags=re.IGNORECASE)
            allowed_part = parts[0]
            forbidden_part = parts[1] if len(parts) > 1 else ""
            allowed_tokens.extend(extract_tokens(allowed_part))
            forbidden_tokens.extend(extract_tokens(forbidden_part))
    return allowed_tokens, forbidden_tokens


def match_any(path, tokens):
    norm_path = path.replace("\\", "/").lstrip("./")
    base = os.path.basename(norm_path)
    for t in tokens:
        if "/" in t:
            if fnmatch.fnmatch(norm_path, t):
                return True
        else:
            if fnmatch.fnmatch(base, t):
                return True
    return False


def check_perimeter(wo_path, section_id=None, files=None):
    if not os.path.exists(wo_path):
        sys.stderr.write(f"Cannot read {wo_path}\n")
        return 2, []

    try:
        with open(wo_path, "r", encoding="utf-8") as f:
            all_lines = f.readlines()
    except Exception as e:
        sys.stderr.write(f"Error reading {wo_path}: {e}\n")
        return 2, []

    if section_id:
        initial_lines = []
        section_lines = []
        in_section = False
        target_prefix = f"## {section_id.lower()}"

        for line in all_lines:
            stripped = line.strip()
            if stripped.startswith("## "):
                if in_section:
                    break
                header_lower = stripped.lower()
                if header_lower.startswith(target_prefix):
                    in_section = True
            elif not in_section and not initial_lines and stripped.startswith("## "):
                pass
            if not in_section:
                # lines before the first ## header
                if not any(l.strip().startswith("## ") for l in initial_lines):
                    if not stripped.startswith("## "):
                        initial_lines.append(line)
            else:
                section_lines.append(line)

        init_allowed, init_forbidden = parse_perimeter_lines(initial_lines)
        sec_allowed, sec_forbidden = parse_perimeter_lines(section_lines)

        violations = []
        for p in files:
            norm_p = p.replace("\\", "/").lstrip("./")
            if match_any(norm_p, sec_allowed):
                continue
            if match_any(norm_p, sec_forbidden):
                violations.append(f"FORBIDDEN {norm_p}")
            elif match_any(norm_p, init_forbidden):
                violations.append(f"FORBIDDEN {norm_p}")
            elif match_any(norm_p, init_allowed):
                continue
            else:
                violations.append(f"OUT {norm_p}")
    else:
        allowed, forbidden = parse_perimeter_lines(all_lines)
        violations = []
        for p in files:
            norm_p = p.replace("\\", "/").lstrip("./")
            if match_any(norm_p, forbidden):
                violations.append(f"FORBIDDEN {norm_p}")
            elif match_any(norm_p, allowed):
                continue
            else:
                violations.append(f"OUT {norm_p}")

    code = 1 if violations else 0
    return code, violations


def main(argv=None):
    if argv is None:
        argv = sys.argv[1:]

    parser = argparse.ArgumentParser(description="Check modified files against Work Order perimeter.")
    parser.add_argument("--wo", required=True, help="Work Order markdown file")
    parser.add_argument("--section", default=None, help="Section ID to check (e.g. FRZ-6)")
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--files", nargs="+", help="List of file paths")
    group.add_argument("--diff", help="Git diff revision range (e.g. BASE...REF)")

    try:
        args = parser.parse_args(argv)
    except SystemExit:
        return 2

    files = []
    if args.files:
        files = args.files
    elif args.diff:
        try:
            res = subprocess.run(
                ["git", "diff", "--name-only", args.diff],
                cwd=ROOT,
                capture_output=True,
                text=True,
                check=True,
            )
            files = [line.strip() for line in res.stdout.splitlines() if line.strip()]
        except Exception as e:
            sys.stderr.write(f"git diff error: {e}\n")
            return 2

    code, violations = check_perimeter(args.wo, section_id=args.section, files=files)
    for v in violations:
        print(v)
    return code


if __name__ == "__main__":
    sys.exit(main())
