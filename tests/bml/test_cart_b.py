#!/usr/bin/env python3
"""End-to-end host tests for BM_SEC_B (.b and hybrid Lua+.b cartridges, native_disasm, bm Code)."""
import os
import struct
import subprocess
import sys
import tempfile
import zlib

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))


def read_png_pixel(path, px, py):
    d = open(path, "rb").read()
    pos, idat, w, bpp = 8, b"", 0, 3
    while pos < len(d):
        n, t = struct.unpack(">I4s", d[pos:pos + 8])
        body = d[pos + 8:pos + 8 + n]
        if t == b"IHDR":
            w, _, _, ct = struct.unpack(">IIBB", body[:10])
            bpp = {2: 3, 6: 4}[ct]
        elif t == b"IDAT":
            idat += body
        pos += 12 + n
    raw = zlib.decompress(idat)
    stride = w * bpp
    prev = bytearray(stride)
    for y in range(py + 1):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1: line[i] = (line[i] + a) & 255
            elif f == 2: line[i] = (line[i] + b) & 255
            elif f == 3: line[i] = (line[i] + ((a + b) >> 1)) & 255
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        prev = line
    return tuple(prev[px * bpp:px * bpp + 3])


def main():
    bmhost = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build/host/bmhost-bin")
    luahost = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, "build/host/luahost")
    mkbm = os.path.join(ROOT, "core/scripts/mkbm.py")

    with tempfile.TemporaryDirectory(prefix="cart-b-") as tmp:
        # 1. Pure .b cartridge
        pure_b = os.path.join(tmp, "pure.b")
        open(pure_b, "w").write(
            "function _init(s: i32[])\n"
            "  s[1] = 20\n"
            "end\n"
            "function _update(s: i32[])\n"
            "  s[1] = s[1] + 1\n"
            "end\n"
            "function _draw(s: i32[])\n"
            "  cls(0x102030)\n"
            "  rectfill(10, 10, s[1], 40, 0xFF8020)\n"
            "end\n"
        )
        pure_bm = os.path.join(tmp, "pure.bm")
        subprocess.run([sys.executable, mkbm, "-o", pure_bm, "--b", pure_b, "--title", "PureB"], check=True)
        shots1 = os.path.join(tmp, "s1")
        os.makedirs(shots1)
        p1 = subprocess.run([bmhost, pure_bm, "--shots", shots1, "--every", "15", "--seconds", "1", "--quiet"],
                            capture_output=True, text=True)
        assert p1.returncode == 0, f"bmhost pure .b failed: {p1.stdout}\n{p1.stderr}"
        shot_files = sorted(f for f in os.listdir(shots1) if f.endswith(".png"))
        assert shot_files, "pure .b produced no screenshots"
        rgb = read_png_pixel(os.path.join(shots1, shot_files[-1]), 25, 25)
        assert rgb[0] > 200 and 100 <= rgb[1] <= 160 and rgb[2] < 60, f"unexpected pure .b pixel: {rgb}"

        # 2. Hybrid Lua + .b project (.bme)
        hyb_lua = os.path.join(tmp, "hyb.lua")
        open(hyb_lua, "w").write(
            "function _draw()\n"
            "  cls(0x000000)\n"
            "  if B.step_sum(10) == 55 then\n"
            "    rectfill(10, 10, 40, 40, 0x20E040)\n"
            "  end\n"
            "end\n"
        )
        hyb_b = os.path.join(tmp, "hyb.b")
        open(hyb_b, "w").write(
            "function step_sum(n: i32): i32\n"
            "  local s = 0\n"
            "  for i = 1, n do s = s + i end\n"
            "  return s\n"
            "end\n"
        )
        hyb_bme = os.path.join(tmp, "hybrid.bme")
        subprocess.run([sys.executable, mkbm, "-o", hyb_bme, "--lua", hyb_lua, "--b", hyb_b, "--title", "Hybrid"],
                       check=True)
        shots2 = os.path.join(tmp, "s2")
        os.makedirs(shots2)
        p2 = subprocess.run([bmhost, hyb_bme, "--shots", shots2, "--every", "15", "--seconds", "1", "--quiet"],
                            capture_output=True, text=True)
        assert p2.returncode == 0, f"bmhost hybrid failed: {p2.stdout}\n{p2.stderr}"
        shot_files2 = sorted(f for f in os.listdir(shots2) if f.endswith(".png"))
        assert shot_files2, "hybrid produced no screenshots"
        rgb2 = read_png_pixel(os.path.join(shots2, shot_files2[-1]), 25, 25)
        assert rgb2[1] > 200 and rgb2[0] < 60, f"unexpected hybrid pixel: {rgb2}"

        # 3. Compile error reporting in .b cartridge
        bad_b = os.path.join(tmp, "bad.b")
        open(bad_b, "w").write("function f(): i32\n  return nope\nend\n")
        bad_bm = os.path.join(tmp, "bad.bm")
        subprocess.run([sys.executable, mkbm, "-o", bad_bm, "--b", bad_b, "--title", "BadB"], check=True)
        p3 = subprocess.run([bmhost, bad_bm, "--seconds", "1"], capture_output=True, text=True)
        out3 = p3.stdout + p3.stderr
        assert "main.b:2:" in out3, f"expected main.b:2: error, got:\n{out3}"

        # 4. Syntax-check bm/code/main.lua via luahost
        lua_check = os.path.join(tmp, "check_syntax.lua")
        open(lua_check, "w").write(
            'local code_src = assert(io.open("' + os.path.join(ROOT, "bm/code/main.lua").replace("\\", "/") + '", "rb")):read("a")\n'
            'local fn, cerr = load(code_src, "=code", "t")\n'
            'assert(fn, cerr)\n'
        )
        p4 = subprocess.run([luahost, lua_check], capture_output=True, text=True)
        assert p4.returncode == 0, f"luahost check failed:\n{p4.stdout}\n{p4.stderr}"

        # 5. Tool APIs via bmhost --tool (native_disasm, tool_read/write, tool_lua_check/run, cart_write lang="b")
        tool_lua = os.path.join(tmp, "tool.lua")
        open(tool_lua, "w").write(
            'local ran = false\n'
            'function _draw()\n'
            '  if not ran then\n'
            '    ran = true\n'
            '    local blob, err = native_compile("function add1(x: i32): i32\\n  return x + 1\\nend\\n")\n'
            '    assert(blob, err)\n'
            '    local d, derr = native_disasm(blob)\n'
            '    assert(d and d.nfn == 1 and d.ncode > 0 and #d.lines > 0, derr)\n'
            '    assert(d.functions[1].name == "add1")\n'
            '    assert(tool_write("/code-b/hi.b", "function f(): i32 return 42 end"))\n'
            '    assert(tool_read("/code-b/hi.b") == "function f(): i32 return 42 end")\n'
            '    assert(tool_write("/code-lua/hi.lua", "return 7"))\n'
            '    assert(tool_read("/code-lua/hi.lua") == "return 7")\n'
            '    local ok1 = tool_lua_check("local a = 1 + 2\\nreturn a", "test.lua")\n'
            '    assert(ok1 == true)\n'
            '    local ok2, lerr = tool_lua_check("local a = 1\\nif then end", "test.lua")\n'
            '    assert(not ok2 and type(lerr) == "string" and lerr:find("2"), tostring(lerr))\n'
            '    local r = tool_lua_run("assert(dofile == nil)\\ncls(0)\\nrectfill(0, 0, 50, 50, 0x2080FF)\\nreturn 123")\n'
            '    assert(r and r.ok and type(r.us) == "number" and r.ret == "123", tostring(r and r.error))\n'
            '    local ok3, err3 = cart_write("/bme/pure_out.bme", { lua = false, b = "function _draw() cls(0) end", title = "OutB" })\n'
            '    assert(ok3, err3)\n'
            '    local p, perr = cart_read("/bme/pure_out.bme")\n'
            '    assert(p and p.lang == "b" and p.b and p.b:find("_draw"), perr)\n'
            '  else\n'
            '    cls(0)\n'
            '    rectfill(0, 0, 50, 50, 0x2080FF)\n'
            '  end\n'
            'end\n'
        )
        tool_bm = os.path.join(tmp, "tool.bm")
        subprocess.run([sys.executable, mkbm, "-o", tool_bm, "--lua", tool_lua, "--title", "ToolTest"], check=True)
        sd_dir = os.path.join(tmp, "sd")
        shots5 = os.path.join(tmp, "s5")
        os.makedirs(sd_dir)
        os.makedirs(shots5)
        p5 = subprocess.run([bmhost, tool_bm, "--tool", "--sd", sd_dir, "--shots", shots5,
                             "--every", "10", "--seconds", "1"],
                            capture_output=True, text=True)
        assert p5.returncode == 0, f"bmhost tool checks failed:\n{p5.stdout}\n{p5.stderr}"
        shot_files5 = sorted(f for f in os.listdir(shots5) if f.endswith(".png"))
        assert shot_files5, "tool test produced no screenshots"
        rgb5 = read_png_pixel(os.path.join(shots5, shot_files5[-1]), 25, 25)
        assert rgb5[2] > 200 and rgb5[0] < 60, f"unexpected tool pixel: {rgb5}"

    print("test_cart_b: ALL CHECKS PASSED")


if __name__ == "__main__":
    main()
