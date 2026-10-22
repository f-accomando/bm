#!/usr/bin/env python3
"""make test-stages3d: reads the bmhost log of carts/stages3d and checks the three stages, the finale and the record.
  check.py LOG   prints `stages3d: N/N checks passed` (exit 0) or `stages3d: FAIL <what>` (exit 1)"""
import re
import sys

ORDER = ["title gpu3d=off", "stage 1 start", "stage 1 clear", "stage 2 start", "stage 2 clear", "stage 3 start",
         "stage 3 clear", "final", "record", "title"]


def main(path):
    text = open(path, encoding="utf-8", errors="replace").read()
    lines = [l.split("stages3d: ", 1)[1].strip() for l in text.splitlines() if "stages3d: " in l]
    checks = fails = 0

    def check(ok, what):
        nonlocal checks, fails
        checks += 1
        if not ok:
            fails += 1
            print(f"stages3d: FAIL {what}")

    got = [next((o for o in ORDER if l.startswith(o)), "?") for l in lines]
    check(got == ORDER, f"order {lines}")
    fin = next((l for l in lines if l.startswith("final")), "")
    rec = next((l for l in lines if l.startswith("record")), "")
    f1 = re.search(r"final frames=(\d+)", fin)
    r1 = re.search(r"record frames=(\d+) saved=(\w+) readback=(\w+) best=(\w+)", rec)
    check(bool(f1) and bool(r1) and f1.group(1) == r1.group(1), f"final and record frames: {fin} / {rec}")
    if r1:
        f = r1.group(1)
        check(r1.group(2) == "true", f"record saved: {rec}")
        check(r1.group(3) == f, f"record readback: {rec}")
        check(r1.group(4) == f, f"record best: {rec}")
    else:
        check(False, "record line missing")
    for l in lines:
        if "clear" in l and l.startswith("stage"):
            check("apples=2/2" in l, f"apples in {l}")
    check(not re.search(r"error|traceback", text, re.I), "an error or a traceback in the log")
    if fails:
        sys.exit(1)
    print(f"stages3d: {checks}/{checks} checks passed")


if __name__ == "__main__":
    main(sys.argv[1])
