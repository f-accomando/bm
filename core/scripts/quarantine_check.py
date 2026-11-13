#!/usr/bin/env python3
"""Checks validity of tests/quarantine.txt entries."""
import argparse
import datetime
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
DEFAULT_FILE = os.path.join(ROOT, "tests", "quarantine.txt")


def check_quarantine(quarantine_path, today_str=None):
    if today_str is None:
        today_date = datetime.date.today()
    else:
        try:
            today_date = datetime.date.fromisoformat(today_str)
        except ValueError:
            sys.stderr.write(f"invalid --today '{today_str}'\n")
            return 1

    if not os.path.isabs(quarantine_path) and not os.path.exists(quarantine_path):
        candidate = os.path.join(ROOT, quarantine_path)
        if os.path.exists(candidate):
            quarantine_path = candidate

    if not os.path.exists(quarantine_path):
        sys.stderr.write(f"Cannot read {quarantine_path}\n")
        return 1

    try:
        with open(quarantine_path, "r", encoding="utf-8") as f:
            lines = f.read().splitlines()
    except Exception as e:
        sys.stderr.write(f"Error reading {quarantine_path}: {e}\n")
        return 1

    errors = []
    seen = set()

    for lno, line in enumerate(lines, 1):
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue

        parts = [p.strip() for p in line.split("|")]
        if len(parts) != 3:
            errors.append((lno, f"expected 3 fields, got {len(parts)}"))
            continue

        target, reason, until_str = parts

        if not reason:
            errors.append((lno, "reason is empty"))

        if "::" not in target:
            errors.append((lno, f"invalid target format '{target}', expected file::test_name"))
            continue

        file_part, test_name = target.split("::", 1)
        file_part = file_part.strip()
        test_name = test_name.strip()

        target_key = f"{file_part}::{test_name}"
        if target_key in seen:
            errors.append((lno, f"duplicate entry for '{target_key}'"))
        seen.add(target_key)

        full_file = file_part if os.path.isabs(file_part) else os.path.join(ROOT, file_part)
        if not os.path.exists(full_file):
            errors.append((lno, f"file '{file_part}' does not exist"))
        else:
            try:
                with open(full_file, "r", encoding="utf-8", errors="ignore") as tf:
                    file_content = tf.read()
                expected_def = f"def {test_name}("
                if expected_def not in file_content:
                    errors.append((lno, f"'{expected_def}' not found in {file_part}"))
            except Exception as e:
                errors.append((lno, f"cannot read {file_part}: {e}"))

        if not re.fullmatch(r"\d{4}-\d{2}-\d{2}", until_str):
            errors.append((lno, f"invalid date '{until_str}'"))
        else:
            try:
                until_date = datetime.date.fromisoformat(until_str)
                if until_date < today_date:
                    errors.append((lno, f"quarantine expired on {until_str}"))
            except ValueError:
                errors.append((lno, f"invalid date '{until_str}'"))

    for lno, msg in errors:
        sys.stderr.write(f"line {lno}: {msg}\n")

    return 1 if errors else 0


def main(argv=None):
    if argv is None:
        argv = sys.argv[1:]

    parser = argparse.ArgumentParser(description="Check tests/quarantine.txt validity.")
    parser.add_argument("--file", default=DEFAULT_FILE, help="Path to quarantine.txt")
    parser.add_argument("--today", default=None, help="Reference date YYYY-MM-DD")
    args = parser.parse_args(argv)

    return check_quarantine(args.file, args.today)


if __name__ == "__main__":
    sys.exit(main())
