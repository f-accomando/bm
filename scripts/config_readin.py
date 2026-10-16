#!/usr/bin/env python3
"""Checks that read_in lines in docs/spec/config-keys.toml point to correct lines."""

import os
import re
import sys
import tomllib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KEYS = "docs/spec/config-keys.toml"


def check():
    toml_path = os.path.join(ROOT, KEYS)
    if not os.path.isfile(toml_path):
        return [], 0

    with open(toml_path, "rb") as f:
        data = tomllib.load(f)

    keys = data.get("key", [])
    lines = []
    stale_count = 0

    for k in keys:
        name = k.get("name")
        read_in = k.get("read_in")
        if not name or not read_in:
            continue

        parts = [p.strip() for p in read_in.split(";") if p.strip()]
        for part in parts:
            m = re.match(r"^\s*([\w./-]+):(\d+)", part)
            if not m:
                continue

            rel_path = m.group(1)
            line_num = int(m.group(2))
            full_path = os.path.join(ROOT, rel_path)

            if not os.path.isfile(full_path):
                lines.append(f"UNCHECKED {name}: {rel_path}:{line_num}")
                continue

            with open(full_path, "r", encoding="utf-8", errors="replace") as f:
                file_lines = f.readlines()

            target = f'"{name}"'
            if 1 <= line_num <= len(file_lines) and target in file_lines[line_num - 1]:
                continue

            matching_lines = [idx + 1 for idx, l in enumerate(file_lines) if target in l]
            if matching_lines:
                with_config = [idx for idx in matching_lines if "config" in file_lines[idx - 1]]
                m_line = with_config[0] if with_config else matching_lines[0]
                lines.append(f"STALE {name}: {rel_path}:{line_num} -> {m_line}")
                stale_count += 1
            else:
                lines.append(f"UNCHECKED {name}: {rel_path}:{line_num}")

    return lines, stale_count


def main():
    lines, stale_count = check()
    for line in lines:
        print(line)
    if stale_count > 0:
        sys.exit(1)
    else:
        print("config_readin: 0 stale")
        sys.exit(0)


if __name__ == "__main__":
    main()
