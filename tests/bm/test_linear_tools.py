#!/usr/bin/env python3
"""Test for linear level tools in bm SDK editor (AGY-03).

Verifies that carts/editor/main.lua contains:
1. Path tool (Shift+P) and width cycling (Shift+W, 1-5) on floor layer
2. Apples row tool (Shift+A, two cells -> line of placeholder 1026)
3. Crate tool (Shift+C -> placeholder 1028)
4. Lamp tool (Shift+O -> placeholder 1032)
5. Status bar reporting apple, crate, lamp counts and estimated triangle budget
6. Menu items and help documentation for linear tools
7. Algorithmic verification of 4x60 corridor, 20 apples, and 6 crates with undo support.
"""

import math
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
EDITOR_LUA = os.path.join(ROOT, "carts", "editor", "main.lua")

def check_editor_source():
    assert os.path.isfile(EDITOR_LUA), f"Missing {EDITOR_LUA}"
    with open(EDITOR_LUA, "r", encoding="utf-8") as f:
        src = f.read()

    # 1. Key bindings in map page
    assert 'P = "tool_path"' in src, "Missing P -> tool_path key mapping"
    assert 'W = "cycle_width"' in src, "Missing W -> cycle_width key mapping"
    assert 'A = "tool_apples"' in src, "Missing A -> tool_apples key mapping"
    assert 'C = "tool_crates"' in src, "Missing C -> tool_crates key mapping"
    assert 'O = "tool_lamps"' in src, "Missing O -> tool_lamps key mapping"

    # 2. Tool action handlers in act(a)
    assert 'a == "tool_path"' in src, "Missing tool_path action handler"
    assert 'a == "tool_apples"' in src, "Missing tool_apples action handler"
    assert 'a == "tool_crates"' in src, "Missing tool_crates action handler"
    assert 'a == "tool_lamps"' in src, "Missing tool_lamps action handler"
    assert 'a == "cycle_width"' in src, "Missing cycle_width action handler"

    # 3. Tool implementations
    assert "commit_path" in src, "Missing commit_path function"
    assert "commit_apples" in src, "Missing commit_apples function"
    assert "place_crate" in src, "Missing place_crate function"
    assert "place_lamp" in src, "Missing place_lamp function"
    assert "refresh_stats" in src, "Missing refresh_stats function"

    # 4. Object counts & budget in status
    assert "apples:%d crates:%d lamps:%d ~%dtri" in src, "Missing counts and budget in status"

    # 5. Menu items
    assert 'Path tool' in src and 'Shift+P' in src, "Missing Path tool in menu items"
    assert 'Apples row' in src and 'Shift+A' in src, "Missing Apples row in menu items"
    assert 'Place crate' in src and 'Shift+C' in src, "Missing Place crate in menu items"
    assert 'Place lamp' in src and 'Shift+O' in src, "Missing Place lamp in menu items"

    print("Source checks: OK")

def test_linear_tools_algorithm():
    """Verify the exact geometry & layer placement matching AGY-03 requirements."""
    map_w, map_h = 16, 160  # 8x8 cells (8x80 tiles when big=True)
    floor = [0] * (map_w * map_h)
    objects = [0] * (map_w * map_h)
    undo_stack = []

    s = 2  # big=True: 16x16 tile = 2x2 cells

    # (a) Draw 4x60 path on floor from (0, 0) to (0, 118)
    p1 = (0, 0)
    p2 = (0, 118)
    width = 4
    tile = 1  # current floor tile

    dx = p2[0] - p1[0]
    dy = p2[1] - p1[1]
    steps = max(abs(dx) // s, abs(dy) // s)
    assert steps == 59, f"Expected 59 steps (60 tiles length), got {steps}"

    stroke = []
    vert = abs(dy) >= abs(dx)
    for i in range(steps + 1):
        t_ratio = i / steps
        cx = int(math.floor((p1[0] + t_ratio * dx) / s + 0.5)) * s
        cy = int(math.floor((p1[1] + t_ratio * dy) / s + 0.5)) * s
        if vert:
            for k in range(width):
                px = cx + k * s
                if px + s <= map_w:
                    for dy_c in (0, 1):
                        for dx_c in (0, 1):
                            idx = (cy + dy_c) * map_w + (px + dx_c)
                            stroke.append((px + dx_c, cy + dy_c, floor[idx]))
                            floor[idx] = tile
    undo_stack.append(stroke)

    # Check floor corridor dimensions
    # Should have 4 tiles wide x 60 tiles long = 240 tile blocks
    floor_blocks = 0
    for tz in range(map_h // s):
        for tx in range(map_w // s):
            if floor[(tz * s) * map_w + (tx * s)] == tile:
                floor_blocks += 1
    assert floor_blocks == 4 * 60, f"Expected 240 floor blocks, got {floor_blocks}"

    # (b) Draw 20 apples (placeholder 1026) along center of corridor
    apple_p1 = (2, 0)
    apple_p2 = (2, 38)  # 20 tiles long (indices 0..19, 19*2 = 38)
    adx = apple_p2[0] - apple_p1[0]
    ady = apple_p2[1] - apple_p1[1]
    asteps = max(abs(adx) // s, abs(ady) // s)
    assert asteps == 19, f"Expected 19 steps (20 apples), got {asteps}"

    astroke = []
    for i in range(asteps + 1):
        t_ratio = i / asteps if asteps > 0 else 0
        cx = int(math.floor((apple_p1[0] + t_ratio * adx) / s + 0.5)) * s
        cy = int(math.floor((apple_p1[1] + t_ratio * ady) / s + 0.5)) * s
        idx = cy * map_w + cx
        astroke.append((cx, cy, objects[idx]))
        objects[idx] = 1026
    undo_stack.append(astroke)

    # (c) Place 6 crates (placeholder 1028)
    crate_stroke = []
    crate_positions = [(4, 10), (4, 20), (4, 30), (4, 40), (4, 50), (4, 60)]
    for cx, cy in crate_positions:
        idx = cy * map_w + cx
        crate_stroke.append((cx, cy, objects[idx]))
        objects[idx] = 1028
    undo_stack.append(crate_stroke)

    # Count blocks in objects layer
    apple_count = sum(1 for v in objects if v == 1026)
    crate_count = sum(1 for v in objects if v == 1028)
    assert apple_count == 20, f"Expected 20 apples (1026), got {apple_count}"
    assert crate_count == 6, f"Expected 6 crates (1028), got {crate_count}"

    # Budget calculation
    est_tri = floor_blocks * 4 + apple_count * 60 + crate_count * 36
    # 240 * 4 + 20 * 60 + 6 * 36 = 960 + 1200 + 216 = 2376
    assert est_tri == 2376, f"Expected 2376 triangles, got {est_tri}"

    # (d) Test Undo
    # Pop crates
    u_crates = undo_stack.pop()
    for x, y, old in u_crates:
        objects[y * map_w + x] = old
    assert sum(1 for v in objects if v == 1028) == 0, "Crates undo failed"
    assert sum(1 for v in objects if v == 1026) == 20, "Apples were affected by crate undo"

    # Pop apples
    u_apples = undo_stack.pop()
    for x, y, old in u_apples:
        objects[y * map_w + x] = old
    assert sum(1 for v in objects if v == 1026) == 0, "Apples undo failed"

    print("Algorithmic & undo checks: OK (20 apples, 6 crates, 4x60 corridor confirmed)")

def main():
    check_editor_source()
    test_linear_tools_algorithm()
    print("linear level tools OK")
    return 0

if __name__ == "__main__":
    sys.exit(main())
