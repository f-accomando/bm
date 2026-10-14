#!/usr/bin/env python3
"""
Unit test for carts/sdk_b16/layout.lua helper L.side_by_side.
Verifies behavior on narrow screens (360x360, < 480 px) vs wide screens (720x720, >= 480 px).
Uses lupa to execute the Lua module directly.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
LAYOUT_PATH = os.path.join(ROOT, "carts", "sdk_b16", "layout.lua")

def run_tests():
    try:
        import lupa
    except ImportError:
        print("SKIP (no lupa)")
        sys.exit(0)

    lua = lupa.LuaRuntime(unpack_returned_tuples=True)

    with open(LAYOUT_PATH, "r", encoding="utf-8") as f:
        code = f.read()

    LY = lua.execute(code)
    assert LY is not None, "Failed to load layout.lua"

    # 1. Test 360x360 (Narrow, w < 480)
    scr360 = LY.rect(0, 0, 360, 360)
    p360, w360 = LY.side_by_side(scr360)
    assert p360.w == 128, f"Expected panel.w == 128 on 360x360, got {p360.w}"
    assert w360.x == 128, f"Expected work.x == 128 on 360x360, got {w360.x}"
    assert w360.w == 232, f"Expected work.w == 232 on 360x360, got {w360.w}"

    # 2. Test 720x720 (Wide, w >= 480)
    scr720 = LY.rect(0, 0, 720, 720)
    p720, w720 = LY.side_by_side(scr720)
    assert p720.w == 240, f"Expected panel.w == 240 on 720x720 (30 cols), got {p720.w}"
    assert w720.x == 240, f"Expected work.x == 240 on 720x720, got {w720.x}"
    assert w720.w == 480, f"Expected work.w == 480 on 720x720 (60 cols), got {w720.w}"

    # 3. Test custom table cols { narrow = 16, wide = 25 }
    lua.execute("custom_spec = { narrow = 16, wide = 25 }")
    custom_spec = lua.globals().custom_spec
    p_cust, w_cust = LY.side_by_side(scr720, custom_spec)
    assert p_cust.w == 200, f"Expected panel.w == 200 on 25 cols, got {p_cust.w}"
    assert w_cust.x == 200 and w_cust.w == 520, f"Expected work.w == 520, got {w_cust.w}"

    # 4. Test string cols format "20c"
    p_str, w_str = LY.side_by_side(scr720, "20c")
    assert p_str.w == 160, f"Expected panel.w == 160 for '20c', got {p_str.w}"
    assert w_str.w == 560, f"Expected work.w == 560, got {w_str.w}"

    # 5. Test narrow clamp when given wide request
    p_clamp, w_clamp = LY.side_by_side(scr360, 30)
    assert p_clamp.w == 128, f"Expected narrow clamp to 16 cols (128px), got {p_clamp.w}"

    print("All layout side_by_side tests PASSED successfully.")

if __name__ == "__main__":
    run_tests()
