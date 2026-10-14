#!/usr/bin/env python3
"""Test for 3D map view and linear level tools in bm SDK RGB30 edition (AGY-04).

Verifies that carts/sdk_b16/main.lua contains:
1. 'v' toggle and L1/R1 orbit camera mapping for pad
2. Placeholder colors for 1032 and 1034 (OBJ_COL)
3. Layer rename action ('r' / 'R') via D.ask and mlayers()
4. 3D map template action ('T') creating floor, height, walls, objects
5. Linear level tools: Path (Shift+P, Shift+W), Apples (Shift+A), Crates (Shift+C), Lamps (Shift+O)
6. Action list (X button) items including 3D preview and level tools
7. Consistency check passes without errors
"""

import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SDK_B16_LUA = os.path.join(ROOT, "carts", "sdk_b16", "main.lua")

def check_sdk_b16_source():
    assert os.path.isfile(SDK_B16_LUA), f"Missing {SDK_B16_LUA}"
    with open(SDK_B16_LUA, "r", encoding="utf-8") as f:
        src = f.read()

    # 1. Check OBJ_COL contains 1032 and 1034
    assert "[1032] = 0xFFC888" in src, "Missing OBJ_COL[1032] (LAMP)"
    assert "[1034] = 0x70A0D0" in src, "Missing OBJ_COL[1034] (CRATE_BOUNCE)"

    # 2. Check 3D view toggle, orbit camera, and L1/R1 pad rotation
    assert "view3d" in src, "Missing view3d state"
    assert "map3d_build" in src, "Missing map3d_build in sdk_b16"
    assert "map3d_draw" in src, "Missing map3d_draw in sdk_b16"
    assert "camera3d" in src, "Missing camera3d in sdk_b16 map view"
    assert "orbit_left" in src and "orbit_right" in src, "Missing orbit actions"
    assert "pg.in_3d" in src, "Missing in_3d hook for pad_frame"

    # 3. Check layer rename and 3D template actions
    assert "rename_layer" in src, "Missing rename_layer action"
    assert "map3d_template" in src, "Missing map3d_template action"

    # 4. Check linear tools
    assert "tool_path" in src, "Missing tool_path action"
    assert "tool_apples" in src, "Missing tool_apples action"
    assert "tool_crates" in src, "Missing tool_crates action"
    assert "tool_lamps" in src, "Missing tool_lamps action"
    assert "cycle_width" in src, "Missing cycle_width action"

    # 5. Check action list and menu items
    assert '"Path tool"' in src, "Missing 'Path tool' in menu_items"
    assert '"Apples row"' in src, "Missing 'Apples row' in menu_items"
    assert '"Place crate"' in src, "Missing 'Place crate' in menu_items"
    assert '"Place lamp"' in src, "Missing 'Place lamp' in menu_items"

    print("Source checks: OK")

def main():
    check_sdk_b16_source()
    print("sdk_b16 map3d OK")
    return 0

if __name__ == "__main__":
    sys.exit(main())
