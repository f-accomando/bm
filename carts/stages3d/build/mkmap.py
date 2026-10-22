#!/usr/bin/env python3
"""The map of Stages 3D: assets/map_floor.csv, map_height.csv, map_walls.csv, map_objects.csv.
16 tiles x 96 tiles (8 x 48 cells); the 3D map reads one tile in 2x2 (cell (cx, cz) is tile (2cx, 2cz)).
Stage k (0..2) takes the cells z 16k .. 16k+15, lz = cz - 16k:
  floor  k+1 for cx 1..6, lz 1..14
  height stage 1: 0 for lz 1..4, 1 for lz 5..9, 2 for lz 10..14 (other stages 0)
  walls  stage 2 only: 4 at lz 7 for cx 1, 2, 5, 6 (the gap is cx 3 and 4)
  objects PLAYER_START 1024 at (3, 1), GOAL 1030 at (3, 13), APPLE 1026 at (3, 5) and (3, 9)"""
import os

COLS, ROWS = 16, 96
ZONE = 16
START, APPLE, GOAL = 1024, 1026, 1030


def main():
    layers = {n: [[0] * COLS for _ in range(ROWS)] for n in ("floor", "height", "walls", "objects")}
    for k in range(3):
        for lz in range(1, 15):
            cz = ZONE * k + lz
            for cx in range(1, 7):
                layers["floor"][2 * cz][2 * cx] = k + 1
                if k == 1:
                    layers["height"][2 * cz][2 * cx] = 0 if lz <= 4 else 1 if lz <= 9 else 2
                if k == 2 and lz == 7 and cx in (1, 2, 5, 6):
                    layers["walls"][2 * cz][2 * cx] = 4
        for cx, lz, kind in ((3, 1, START), (3, 13, GOAL), (3, 5, APPLE), (3, 9, APPLE)):
            layers["objects"][2 * (ZONE * k + lz)][2 * cx] = kind
    assets = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "assets")
    for name, rows in layers.items():
        with open(os.path.join(assets, f"map_{name}.csv"), "w", newline="\n") as f:
            f.write(f"# Stages 3D, layer {name} (carts/stages3d/build/mkmap.py)\n")
            for r in rows:
                f.write(",".join(str(v) for v in r) + "\n")


if __name__ == "__main__":
    main()
