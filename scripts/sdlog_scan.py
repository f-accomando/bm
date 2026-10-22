#!/usr/bin/env python3
"""
sdlog_scan.py - scan SD card log files (bootprev.txt, bootlog.txt, lastrun.txt) on PC.

Usage:
  python scripts/sdlog_scan.py DIR [--secret S]...
"""
import argparse
import os
import re
import sys


def parse_args():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dir", metavar="DIR", help="SD root or bm/ directory")
    ap.add_argument("--secret", action="append", default=[], help="secret to check for unmasked leaks")
    return ap.parse_args()


def main():
    args = parse_args()

    raw_dir = args.dir
    if not os.path.exists(raw_dir) or not os.path.isdir(raw_dir):
        sys.stderr.write(f"ERROR: directory not found: {raw_dir}\n")
        return 2

    # Check if DIR contains a case-insensitive 'bm' subfolder
    target_dir = raw_dir
    try:
        for entry in os.listdir(raw_dir):
            if entry.lower() == "bm" and os.path.isdir(os.path.join(raw_dir, entry)):
                target_dir = os.path.join(raw_dir, entry)
                break
    except Exception as e:
        sys.stderr.write(f"ERROR: cannot read directory {raw_dir}: {e}\n")
        return 2

    # Locate case-insensitive files
    file_map = {}
    try:
        for entry in os.listdir(target_dir):
            elower = entry.lower()
            if elower in ("bootprev.txt", "bootlog.txt", "lastrun.txt"):
                file_map[elower] = os.path.join(target_dir, entry)
    except Exception as e:
        sys.stderr.write(f"ERROR: cannot read directory {target_dir}: {e}\n")
        return 2

    ansi_re = re.compile(r"\x1b\[[0-9;]*m")

    def read_clean(name):
        path = file_map.get(name)
        if not path:
            return None
        try:
            with open(path, "r", encoding="utf-8", errors="replace") as f:
                content = f.read()
            return ansi_re.sub("", content)
        except Exception as e:
            sys.stderr.write(f"ERROR reading {path}: {e}\n")
            sys.exit(2)

    bootprev_content = read_clean("bootprev.txt")
    bootlog_content = read_clean("bootlog.txt")
    lastrun_content = read_clean("lastrun.txt")

    # 1. bootprev
    header_re = re.compile(r"^--- the run before (.*); the last lines it printed \(RAM\) ---$", re.MULTILINE)
    bootprev_status = "MISSING"
    if bootprev_content is None:
        print("bootprev: MISSING")
    else:
        matches = list(header_re.finditer(bootprev_content))
        if not matches:
            bootprev_status = "NO HEADER"
            print("bootprev: NO HEADER")
        else:
            state = matches[-1].group(1).strip()
            if state.startswith("ended cleanly"):
                bootprev_status = "CLEAN"
            elif state.startswith("ended in a crash"):
                bootprev_status = "CRASH"
            elif state.startswith("ended without a clean exit"):
                bootprev_status = "UNCLEAN"
            else:
                bootprev_status = "NO HEADER"
            print(f"bootprev: {bootprev_status} {state}")

    # 2. bootlog
    previous_run = "-"
    if bootlog_content is None:
        print("bootlog: MISSING")
    else:
        watchdog = "NOT REPORTED"
        for line in bootlog_content.splitlines():
            sline = line.strip()
            body = re.sub(r"^\[[^\]]*\]\s*", "", sline)
            if body.startswith("watchdog: on"):
                watchdog = "ON"
                break
            elif body.startswith("watchdog: not available"):
                watchdog = "OFF"
                break
            elif body.startswith("watchdog: resets at once"):
                watchdog = "EMULATOR"
                break

        if "the last run ended in a crash" in bootlog_content:
            previous_run = "CRASHED"
        elif "the Pi froze last time" in bootlog_content:
            previous_run = "FROZE"
        else:
            previous_run = "-"

        print(f"bootlog: watchdog {watchdog}; previous run {previous_run}")

    # 3. lastrun
    if lastrun_content is None:
        print("lastrun: MISSING")
    else:
        n_lines = len(lastrun_content.splitlines())
        full_tag = ", log full" if "--- (the log is full) the last lines printed ---" in lastrun_content else ""
        print(f"lastrun: PRESENT ({n_lines} lines{full_tag})")

    # 4. secrets
    has_leak = False
    file_contents = {
        "bootprev.txt": bootprev_content,
        "bootlog.txt": bootlog_content,
        "lastrun.txt": lastrun_content,
    }

    for idx, secret in enumerate(args.secret, 1):
        if len(secret) < 4:
            print(f"secret #{idx}: shorter than 4 characters, the kernel does not mask it")
            continue

        pattern = re.compile(rf"(?<![A-Za-z0-9]){re.escape(secret)}(?![A-Za-z0-9])")
        leak_lines = []
        for fname in ("bootprev.txt", "bootlog.txt", "lastrun.txt"):
            content = file_contents.get(fname)
            if content is not None:
                count = len(pattern.findall(content))
                if count > 0:
                    leak_lines.append((fname, count))

        if leak_lines:
            has_leak = True
            for fname, count in leak_lines:
                print(f"secret #{idx}: LEAK in {fname} ({count} times)")
        else:
            print(f"secret #{idx}: OK")

    # 5. verdict
    if bootprev_status == "CRASH" or previous_run == "CRASHED":
        verdict = "CRASH"
    elif bootprev_status == "UNCLEAN" or previous_run == "FROZE":
        verdict = "FREEZE"
    elif bootprev_status in ("MISSING", "NO HEADER") or bootlog_content is None:
        verdict = "INCOMPLETE"
    else:
        verdict = "OK"

    print(f"verdict: {verdict}")

    return 0 if (verdict == "OK" and not has_leak) else 1


if __name__ == "__main__":
    sys.exit(main())
