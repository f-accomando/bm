#!/usr/bin/env python3
"""
Island Bench (carts/islandbench) in bmhost, no hardware:

  1. the kit is in the cartridge: 24 models, the objects layer holds the island's objects;
  2. a manual run prints its line with the island (objects > 0, the 3D draws some);
  3. the benchmark (Start + A) runs every scene and ends with `bench done`.

  tests/islandbench/check.py BMHOST-BIN CART.bm WORKDIR
"""
import os
import re
import subprocess
import sys

LINE = re.compile(r"^islandbench 1 t=\d+ scene=\S+ res=\d+x\d+ opt=\S+ \| ms=[\d.]+ .* o3=(\d+) o2=(\d+)$")


def run(host, cart, work, name, taps, seconds):
    inp = os.path.join(work, name + ".txt")
    with open(inp, "w") as f:
        f.write("0 device 1 keyboard\n")
        for frame, b in taps:
            f.write(f"{frame} pad 1 {b}\n{frame + 3} pad 1 none\n")
    r = subprocess.run([host, cart, "--input", inp, "--seconds", str(seconds)], capture_output=True, text=True)
    out = r.stdout + r.stderr
    assert r.returncode == 0, out[-2000:]
    assert "error" not in out.lower().replace("0 error", ""), out[-2000:]
    return out.splitlines()


def main():
    host, cart, work = sys.argv[1:4]
    os.makedirs(work, exist_ok=True)
    lines = run(host, cart, work, "manual", [], 4)
    start = [l for l in lines if l.startswith("islandbench 1 start objects=")]
    assert start and int(start[0].rsplit("=", 1)[1]) > 100, start
    rows = [LINE.match(l) for l in lines if l.startswith("islandbench 1 t=")]
    assert len(rows) >= 3 and all(rows), lines[-5:]
    assert int(rows[-1].group(1)) + int(rows[-1].group(2)) > 10, "no object of the island drawn"
    lines = run(host, cart, work, "bench", [(5, "start a")], 120)
    done = [l for l in lines if l.startswith("bench ") and not l.startswith("bench start") and not l.startswith("bench done")]
    assert len(done) == 16, len(done)
    assert any(l.startswith("bench done score=") for l in lines), lines[-3:]
    print("islandbench: the kit, a manual run and the 16 scenes of the benchmark are fine")


if __name__ == "__main__":
    main()
