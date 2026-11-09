#!/usr/bin/env python3
"""make test-bml: verifies stages 1, 2, 3, and 4 of the language of bm (tools/bml/bml.py).

  run.py LUAHOST BUILD_DIR

1. Stage 1 (Front end): lexer, parser, AST, and type checker reject invalid programs and name the exact line.
2. Stage 2 (StackVM): sum(10) == 55, sum(-5..100), features.b (347 calls), and 200 random programs match Lua.
3. Stage 3 (Kernel bmvm): compiles sum, features, and 200 random programs to BMV1 blobs and runs them in C (src/bm/bmvm.c).
4. Stage 4 (ARMv6 A32 back end): compiles sum (both 9-word leaf and general), features, and 200 random programs to
   32-bit ARMv6 machine words and executes them on an emulated ARM1176 CPU in qemu-system-arm against Lua.
"""
import os
import random
import shutil
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools", "bml"))
import bml  # noqa: E402

LUAHOST = sys.argv[1]
BUILD = sys.argv[2]
os.makedirs(BUILD, exist_ok=True)
os.environ["QEMU_AUDIO_DRV"] = "none"

LUA_PRELUDE = """\
local abs, min, max, floor, sqrt = math.abs, math.min, math.max, math.floor, math.sqrt
local function f32(x) return x + 0.0 end
local __mem = {}
local function peek(addr) return __mem[addr] or 0 end
local function poke(addr, val) __mem[addr] = val & 255 end
local function peek2(addr)
  local b0 = __mem[addr] or 0
  local b1 = __mem[addr+1] or 0
  return b0 | (b1 << 8)
end
local function poke2(addr, val)
  __mem[addr]   = val & 255
  __mem[addr+1] = (val >> 8) & 255
end
local function peek4(addr)
  local b0 = __mem[addr] or 0
  local b1 = __mem[addr+1] or 0
  local b2 = __mem[addr+2] or 0
  local b3 = __mem[addr+3] or 0
  local v = b0 | (b1 << 8) | (b2 << 16) | (b3 << 24)
  if v >= 0x80000000 then v = v - 0x100000000 end
  return v
end
local function poke4(addr, val)
  __mem[addr]   = val & 255
  __mem[addr+1] = (val >> 8) & 255
  __mem[addr+2] = (val >> 16) & 255
  __mem[addr+3] = (val >> 24) & 255
end
local function memcpy(dst, src, n)
  for i = 0, n - 1 do
    __mem[dst + i] = __mem[src + i] or 0
  end
end
local function memset(dst, val, n)
  local b = val & 255
  for i = 0, n - 1 do
    __mem[dst + i] = b
  end
end
local function __bml_arr(base)
  if type(base) == "table" then return base end
  return setmetatable({}, {
    __index = function(_, i) return peek4(base + (i - 1) * 4) end,
    __newindex = function(_, i, v) poke4(base + (i - 1) * 4, v) end,
  })
end
"""

failed = 0


def fail(msg: str) -> None:
    global failed
    print("bml: FAIL " + msg)
    failed += 1


# ---------------------------------------------------------------- 1. Stage 1: negative error & line tests
BAD_PROGRAMS = [
    ("unexpected character",
     "function f(n: i32): i32\n  local x = 1\n  return x @ 2\nend\n", "line 3"),
    ("narrowing f32 literal to i32",
     "function f(n: i32): i32\n  local x: i32 = 1.5\n  return x\nend\n", "line 2"),
    ("unknown type in parameter",
     "function f(n: str): i32\n  return 0\nend\n", "line 1"),
    ("duplicate parameter name",
     "function f(a: i32, a: bool): i32\n  return a\nend\n", "line 1"),
    ("duplicate local declaration",
     "function f(a: i32): i32\n  local x = 1\n  local x = 2\n  return x\nend\n", "line 3"),
    ("local shadowing parameter",
     "function f(a: i32): i32\n  local a = 2\n  return a\nend\n", "line 2"),
    ("local explicit type mismatch",
     "function f(a: i32): i32\n  local flag: bool = a + 1\n  return a\nend\n", "line 2"),
    ("assignment to undeclared variable",
     "function f(a: i32): i32\n  b = a + 1\n  return a\nend\n", "line 2"),
    ("assignment of bool to i32 variable",
     "function f(a: i32): i32\n  local x = 10\n  x = true\n  return x\nend\n", "line 3"),
    ("non-bool condition in if",
     "function f(n: i32): i32\n  if n then return 1 end\n  return 0\nend\n", "line 2"),
    ("non-bool condition in elseif",
     "function f(n: i32): i32\n  if n == 0 then\n    return 0\n  elseif n + 1 then\n    return 1\n  end\n  return 2\nend\n", "line 4"),
    ("non-bool condition in while",
     "function f(n: i32): i32\n  while n do\n    n = n - 1\n  end\n  return n\nend\n", "line 2"),
    ("bool bound in numeric for",
     "function f(n: i32): i32\n  local s = 0\n  for i = true, n do\n    s = s + 1\n  end\n  return s\nend\n", "line 3"),
    ("break outside any loop",
     "function f(n: i32): i32\n  if n > 0 then\n    break\n  end\n  return n\nend\n", "line 3"),
    ("wrong return type",
     "function f(n: i32): i32\n  return n > 0\nend\n", "line 2"),
    ("empty return in value-returning function",
     "function f(n: i32): i32\n  if n < 0 then\n    return\n  end\n  return n\nend\n", "line 3"),
    ("missing return on some path",
     "function f(n: i32): i32\n  if n > 0 then\n    return 1\n  end\nend\n", "line 1"),
    ("arithmetic on bool",
     "function f(b: bool): i32\n  return b + 1\nend\n", "line 2"),
    ("logical 'and' on i32",
     "function f(a: i32, b: i32): bool\n  return a and b\nend\n", "line 2"),
    ("float division '/' on i32",
     "function f(a: i32, b: i32): i32\n  return a / b\nend\n", "line 2"),
    ("call to undefined function",
     "function f(a: i32): i32\n  return unknown_fn(a)\nend\n", "line 2"),
    ("call with wrong argument count",
     "function g(x: i32): i32\n  return x\nend\nfunction f(a: i32): i32\n  return g(a, 2)\nend\n", "line 5"),
    ("call with wrong argument type",
     "function g(x: i32): i32\n  return x\nend\nfunction f(a: i32): i32\n  return g(a > 0)\nend\n", "line 5"),
    ("indexing a scalar variable",
     "function f(a: i32): i32\n  return a[1]\nend\n", "line 2"),
    ("non-i32 array index",
     "function f(arr: i32[], b: bool): i32\n  return arr[b]\nend\n", "line 2"),
    ("unknown struct field",
     "struct S\n  x: i32\nend\nfunction f(s: S): i32\n  return s.y\nend\n", "line 5"),
    ("wrong type assigned to struct field",
     "struct S\n  x: i32\nend\nfunction f(s: S)\n  s.x = true\nend\n", "line 5"),
    ("mixing i32 and f32 in '+'",
     "function f(a: i32, b: f32): f32\n  return a + b\nend\n", "line 2"),
    ("using void kernel function cls in expression",
     "function f(c: i32): i32\n  return cls(c)\nend\n", "line 2"),
    ("wrong argument count to rectfill",
     "function f(c: i32)\n  rectfill(0, 0, 10, c)\nend\n", "line 2"),
    ("wrong argument type to btn",
     "function f(): bool\n  return btn(true)\nend\n", "line 2"),
]

for desc, bad_src, expected_line in BAD_PROGRAMS:
    try:
        bml.parse_and_check(bad_src)
        fail(f"accepted invalid program ({desc})")
    except bml.Error as e:
        if expected_line not in str(e):
            fail(f"wrong line for '{desc}': expected '{expected_line}', got '{e}'")

# ---------------------------------------------------------------- 2. Stage 1 & 2: sum(10) == 55 and Lua comparison
sum_path = os.path.join(HERE, "sum.b")
with open(sum_path, encoding="utf-8") as f:
    sum_src = f.read()

sum_mod = bml.compile_source(sum_src)
sum_vm = bml.StackVM(sum_mod)
res_10 = sum_vm.call("sum", [10])
if res_10 != 55:
    fail(f"sum(10) returned {res_10}, expected 55")

sum_lua_path = os.path.join(BUILD, "sum_test.lua")
with open(sum_lua_path, "w", encoding="utf-8") as f:
    f.write(bml.to_lua(sum_src))
    f.write("\nfor n = -5, 100 do print(sum(n)) end\n")

lua_sum_out = subprocess.check_output([LUAHOST, sum_lua_path], text=True).strip().splitlines()
vm_sum_out = [str(sum_vm.call("sum", [n])) for n in range(-5, 101)]
if lua_sum_out != vm_sum_out:
    fail("sum(-5..100) differs between StackVM and Lua")

# ---------------------------------------------------------------- 3. Stage 2: features.b against Lua
feat_path = os.path.join(HERE, "features.b")
with open(feat_path, encoding="utf-8") as f:
    feat_src = f.read()

feat_mod = bml.compile_source(feat_src)
feat_vm = bml.StackVM(feat_mod)

feat_calls = []
for x in range(-25, 26):
    feat_calls.append(("is_even", [x], "bool"))
    feat_calls.append(("clamp", [x * 7, -50, 50], "i32"))
    feat_calls.append(("short_circuit_ok", [x], "bool"))
    feat_calls.append(("countdown_sum", [x], "i32"))
for a in (-48, -17, 0, 12, 35, 105, 1071):
    for b in (-36, -1, 0, 6, 14, 462):
        feat_calls.append(("gcd", [a, b], "i32"))
        feat_calls.append(("arith_mix", [a, b], "i32"))
for n in range(0, 13):
    feat_calls.append(("fib", [n], "i32"))
for n in range(0, 40):
    feat_calls.append(("collatz_steps", [n], "i32"))
for lim in (0, 1, 2, 10, 30, 60):
    feat_calls.append(("count_primes", [lim], "i32"))

feat_lua_lines = [LUA_PRELUDE, bml.to_lua(feat_src)]
vm_feat_out = []
for fn_name, args, _ in feat_calls:
    res = feat_vm.call(fn_name, args)
    vm_feat_out.append("true" if res is True else ("false" if res is False else str(res)))
    arg_str = ", ".join(str(a) for a in args)
    feat_lua_lines.append(f"print(tostring({fn_name}({arg_str})))")

feat_lua_path = os.path.join(BUILD, "features_test.lua")
with open(feat_lua_path, "w", encoding="utf-8") as f:
    f.write("\n".join(feat_lua_lines) + "\n")

lua_feat_out = subprocess.check_output([LUAHOST, feat_lua_path], text=True).strip().splitlines()
if lua_feat_out != vm_feat_out:
    for idx, ((fn_name, args, _), lv, vv) in enumerate(zip(feat_calls, lua_feat_out, vm_feat_out)):
        if lv != vv:
            fail(f"features mismatch at #{idx} {fn_name}({args}): Lua={lv}, VM={vv}")
            break

# ---------------------------------------------------------------- 4. Stage 2: 200 random programs against Lua
rnd = random.Random(20261109)


def gen_int_expr(vars_i32: list, vars_bool: list, helper_name: str, depth: int) -> str:
    if depth <= 0 or rnd.random() < 0.35:
        if vars_i32 and rnd.random() < 0.65:
            return rnd.choice(vars_i32)
        return str(rnd.randint(-40, 40))
    choice = rnd.random()
    if choice < 0.50:
        op = rnd.choice(["+", "-", "*", "&"])
        l = gen_int_expr(vars_i32, vars_bool, helper_name, depth - 1)
        r = gen_int_expr(vars_i32, vars_bool, helper_name, depth - 1)
        if op == "*":
            r = str(rnd.randint(-6, 6))
        return f"({l} {op} {r})"
    if choice < 0.72:
        op = rnd.choice(["//", "%"])
        l = gen_int_expr(vars_i32, vars_bool, helper_name, depth - 1)
        div = rnd.choice([-9, -7, -5, -3, -2, 2, 3, 5, 7, 9, 11])
        return f"({l} {op} ({div}))"
    if choice < 0.85:
        fn = rnd.choice(["abs", "min", "max"])
        a = gen_int_expr(vars_i32, vars_bool, helper_name, depth - 1)
        if fn == "abs":
            return f"abs({a})"
        b = gen_int_expr(vars_i32, vars_bool, helper_name, depth - 1)
        return f"{fn}({a}, {b})"
    if helper_name and rnd.random() < 0.5:
        a = gen_int_expr(vars_i32, vars_bool, "", depth - 1)
        return f"{helper_name}({a})"
    e = gen_int_expr(vars_i32, vars_bool, helper_name, depth - 1)
    return f"(-({e}))"


def gen_bool_expr(vars_i32: list, vars_bool: list, helper_name: str, depth: int) -> str:
    if depth <= 0:
        if vars_bool and rnd.random() < 0.5:
            return rnd.choice(vars_bool)
        op = rnd.choice(["<", "<=", ">", ">=", "==", "~="])
        l = gen_int_expr(vars_i32, vars_bool, helper_name, 0)
        r = gen_int_expr(vars_i32, vars_bool, helper_name, 0)
        return f"({l} {op} {r})"
    choice = rnd.random()
    if choice < 0.50:
        op = rnd.choice(["<", "<=", ">", ">=", "==", "~="])
        l = gen_int_expr(vars_i32, vars_bool, helper_name, depth - 1)
        r = gen_int_expr(vars_i32, vars_bool, helper_name, depth - 1)
        return f"({l} {op} {r})"
    if choice < 0.80:
        op = rnd.choice(["and", "or"])
        l = gen_bool_expr(vars_i32, vars_bool, helper_name, depth - 1)
        r = gen_bool_expr(vars_i32, vars_bool, helper_name, depth - 1)
        return f"({l} {op} {r})"
    b = gen_bool_expr(vars_i32, vars_bool, helper_name, depth - 1)
    return f"(not {b})"


def gen_random_program(idx: int) -> str:
    h_op = rnd.choice(["+", "-", "^mix"])
    if h_op == "^mix":
        helper = (
            f"function h_{idx}(z: i32): i32\n"
            f"  local m = (z & 31) - (z // 3)\n"
            f"  if m < 0 then return -m else return m + 1 end\n"
            f"end\n"
        )
    else:
        c = rnd.randint(-15, 15)
        helper = (
            f"function h_{idx}(z: i32): i32\n"
            f"  return (z {h_op} ({c})) // 2\n"
            f"end\n"
        )

    vars_i32 = ["a", "b", "acc", "v1"]
    vars_bool = ["flag"]
    lines = [
        helper,
        f"function f_{idx}(a: i32, b: i32, flag: bool): i32",
        f"  local acc: i32 = {gen_int_expr(['a', 'b'], ['flag'], f'h_{idx}', 1)}",
        f"  local v1 = {gen_int_expr(['a', 'b', 'acc'], ['flag'], f'h_{idx}', 1)}",
        f"  local b1: bool = {gen_bool_expr(['a', 'b', 'acc', 'v1'], ['flag'], f'h_{idx}', 1)}",
    ]
    vars_bool.append("b1")

    for s_idx in range(4):
        kind = rnd.choice(["assign", "if", "for", "while"])
        if kind == "assign":
            target = rnd.choice(["acc", "v1"])
            expr = gen_int_expr(vars_i32, vars_bool, f"h_{idx}", 2)
            lines.append(f"  {target} = clamp_i({expr})")
        elif kind == "if":
            c1 = gen_bool_expr(vars_i32, vars_bool, f"h_{idx}", 2)
            c2 = gen_bool_expr(vars_i32, vars_bool, f"h_{idx}", 1)
            e1 = gen_int_expr(vars_i32, vars_bool, f"h_{idx}", 1)
            e2 = gen_int_expr(vars_i32, vars_bool, f"h_{idx}", 1)
            e3 = gen_int_expr(vars_i32, vars_bool, f"h_{idx}", 1)
            lines.extend([
                f"  if {c1} then",
                f"    acc = clamp_i(acc + ({e1}))",
                f"  elseif {c2} then",
                f"    acc = clamp_i(acc - ({e2}))",
                f"  else",
                f"    v1 = clamp_i({e3})",
                f"  end",
            ])
        elif kind == "for":
            iv = f"i{s_idx}"
            lo = rnd.randint(-3, 2)
            hi = rnd.randint(lo, lo + 6)
            body_e = gen_int_expr(vars_i32 + [iv], vars_bool, f"h_{idx}", 1)
            lines.extend([
                f"  for {iv} = {lo}, {hi} do",
                f"    acc = clamp_i(acc + ({body_e}))",
                f"  end",
            ])
        else:
            cnt = f"w{s_idx}"
            limit = rnd.randint(2, 6)
            cond = gen_bool_expr(vars_i32, vars_bool, f"h_{idx}", 1)
            step_e = gen_int_expr(vars_i32, vars_bool, f"h_{idx}", 1)
            brk_c = gen_bool_expr(vars_i32, vars_bool, f"h_{idx}", 1)
            lines.extend([
                f"  local {cnt} = 0",
                f"  while ({cnt} < {limit}) and ({cond}) do",
                f"    acc = clamp_i(acc + ({step_e}))",
                f"    {cnt} = {cnt} + 1",
                f"    if {brk_c} then break end",
                f"  end",
            ])

    ret_e = gen_int_expr(vars_i32, vars_bool, f"h_{idx}", 1)
    lines.append(f"  return clamp_i(acc + v1 + ({ret_e}))")
    lines.append("end")
    return "\n".join(lines)


CLAMP_HELPER = """\
function clamp_i(x: i32): i32
  return min(max(x, -1000000), 1000000)
end
"""

prog_chunks = [CLAMP_HELPER]
call_specs = []
for i in range(200):
    prog_chunks.append(gen_random_program(i))
    inputs = [
        (rnd.randint(-50, 50), rnd.randint(-50, 50), rnd.choice([True, False]))
        for _ in range(5)
    ]
    call_specs.append((f"f_{i}", inputs))

full_bml = "\n\n".join(prog_chunks) + "\n"
rand_mod = bml.compile_source(full_bml)
rand_vm = bml.StackVM(rand_mod)

vm_rand_out = []
lua_rand_lines = [LUA_PRELUDE, bml.to_lua(full_bml)]
for fn_name, inputs in call_specs:
    for a, b, flg in inputs:
        vm_res = rand_vm.call(fn_name, [a, b, flg])
        vm_rand_out.append(str(vm_res))
        flg_s = "true" if flg else "false"
        lua_rand_lines.append(f"print({fn_name}({a}, {b}, {flg_s}))")

rand_lua_path = os.path.join(BUILD, "random_test.lua")
with open(rand_lua_path, "w", encoding="utf-8") as f:
    f.write("\n".join(lua_rand_lines) + "\n")

lua_rand_out = subprocess.check_output([LUAHOST, rand_lua_path], text=True).strip().splitlines()
if lua_rand_out != vm_rand_out:
    for idx, (lv, vv) in enumerate(zip(lua_rand_out, vm_rand_out)):
        if lv != vv:
            fail(f"random program mismatch at call #{idx}: Lua={lv}, VM={vv}")
            break

# ---------------------------------------------------------------- 5. Stage 3: Kernel bmvm (src/bm/bmvm.c) against Lua
sum_bmv = os.path.join(BUILD, "sum.bmv")
feat_bmv = os.path.join(BUILD, "features.bmv")
open(sum_bmv, "wb").write(bml.compile_bmvm(sum_src))
open(feat_bmv, "wb").write(bml.compile_bmvm(feat_src))

# Split 200 random programs into 8 batches of 25 programs (each batch has 51 <= 64 functions, bmvm's limit)
batch_bmvs = []
for b_idx in range(8):
    sub_progs = [CLAMP_HELPER] + prog_chunks[1 + b_idx * 25 : 1 + (b_idx + 1) * 25]
    b_path = os.path.join(BUILD, f"rand_{b_idx}.bmv")
    open(b_path, "wb").write(bml.compile_bmvm("\n\n".join(sub_progs) + "\n"))
    batch_bmvs.append(b_path)

BMVM_HOST_C = r"""
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "bmvm.h"

static bmvm_mod *must_load(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); exit(1); }
    fseek(f, 0, SEEK_END);
    size_t len = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc(len);
    if (fread(buf, 1, len, f) != len) { fprintf(stderr, "short read\n"); exit(1); }
    fclose(f);
    bmvm_mod *m = NULL;
    const char *why = "";
    if (bmvm_load(&m, buf, len, &why) != 0) {
        fprintf(stderr, "bmvm_load(%s) failed: %s\n", path, why);
        exit(1);
    }
    free(buf);
    return m;
}

int main(int argc, char **argv)
{
    if (argc < 3) return 1;
    bmvm_mod *m = must_load(argv[1]);
    FILE *in = fopen(argv[2], "r");
    if (!in) return 1;
    char fname[64], rkind[16];
    int nargs;
    while (fscanf(in, "%63s %15s %d", fname, rkind, &nargs) == 3) {
        bmvm_val args[16], ret;
        for (int i = 0; i < nargs; i++) {
            int v = 0;
            if (fscanf(in, "%d", &v) != 1) return 1;
            args[i].i = v;
        }
        int fn = bmvm_find(m, fname);
        if (fn < 0) { fprintf(stderr, "missing fn %s\n", fname); return 1; }
        int err = bmvm_call(m, fn, args, nargs, NULL, 0, &ret, 20000000u);
        if (err != 0) { fprintf(stderr, "bmvm_call(%s) error %d\n", fname, err); return 1; }
        if (rkind[0] == 'b')
            printf("%s\n", ret.i ? "true" : "false");
        else
            printf("%d\n", (int)ret.i);
    }
    fclose(in);
    bmvm_free(m);
    return 0;
}
"""

bmvm_host_c_path = os.path.join(BUILD, "bmvm_host.c")
bmvm_host_bin = os.path.join(BUILD, "bmvm_host")
open(bmvm_host_c_path, "w", encoding="utf-8").write(BMVM_HOST_C)
subprocess.run(
    ["cc", "-O2", "-Wall", f"-I{os.path.join(ROOT, 'src', 'bm')}",
     "-o", bmvm_host_bin, bmvm_host_c_path, os.path.join(ROOT, "src", "bm", "bmvm.c"), "-lm"],
    check=True,
)

# 5a. sum in bmvm
sum_in = os.path.join(BUILD, "sum_bmvm.in")
open(sum_in, "w", encoding="utf-8").write("".join(f"sum i32 1 {n}\n" for n in range(-5, 101)))
bmvm_sum_out = subprocess.check_output([bmvm_host_bin, sum_bmv, sum_in], text=True).strip().splitlines()
if bmvm_sum_out != lua_sum_out:
    fail("Stage 3 bmvm sum(-5..100) differs from Lua")

# 5b. features in bmvm
feat_in = os.path.join(BUILD, "feat_bmvm.in")
open(feat_in, "w", encoding="utf-8").write(
    "".join(f"{fn_name} {rk} {len(args)} {' '.join(str(int(a)) for a in args)}\n" for fn_name, args, rk in feat_calls)
)
bmvm_feat_out = subprocess.check_output([bmvm_host_bin, feat_bmv, feat_in], text=True).strip().splitlines()
if bmvm_feat_out != lua_feat_out:
    fail("Stage 3 bmvm features.b differs from Lua")

# 5c. 200 random programs in bmvm
bmvm_rand_out = []
for b_idx, b_path in enumerate(batch_bmvs):
    b_in = os.path.join(BUILD, f"rand_{b_idx}.in")
    lines_in = []
    for fn_name, inputs in call_specs[b_idx * 25 : (b_idx + 1) * 25]:
        for a, b, flg in inputs:
            lines_in.append(f"{fn_name} i32 3 {a} {b} {1 if flg else 0}\n")
    open(b_in, "w", encoding="utf-8").write("".join(lines_in))
    bmvm_rand_out.extend(subprocess.check_output([bmvm_host_bin, b_path, b_in], text=True).strip().splitlines())

if bmvm_rand_out != lua_rand_out:
    fail("Stage 3 bmvm 200 random programs differ from Lua")

# ---------------------------------------------------------------- 6. Stage 4: ARMv6 A32 back end + qemu-system-arm
arm_sum_leaf = bml.compile_armv6(sum_src, leaf_opt=True)
if tuple(arm_sum_leaf.words) != bml.SUM_ARMV6_WORDS:
    fail(f"Stage 4 leaf sum words {[hex(w) for w in arm_sum_leaf.words]} != SUM_ARMV6_WORDS")

for n in range(-5, 101):
    if bml.run_armv6_words(tuple(arm_sum_leaf.words), n) != int(lua_sum_out[n + 5]):
        fail(f"Stage 4 simulated ARMv6 sum({n}) mismatch")
        break

objdump = shutil.which("arm-none-eabi-objdump")
if objdump:
    bin_path = os.path.join(BUILD, "sum_armv6.bin")
    with open(bin_path, "wb") as f:
        f.write(struct.pack("<9I", *arm_sum_leaf.words))
    dis = subprocess.check_output([objdump, "-D", "-b", "binary", "-m", "arm", bin_path], text=True)
    for expected_insn in ("cmp\tr2, r0", "bgt\t0x1c", "add\tr1, r1, r2", "b\t0x8", "bx\tlr"):
        if expected_insn not in dis:
            fail(f"ARMv6 objdump missing '{expected_insn}':\n{dis}")

arm_gcc = shutil.which("arm-none-eabi-gcc")
qemu_arm = shutil.which("qemu-system-arm")
if arm_gcc and qemu_arm:
    arm_sum_gen = bml.compile_armv6(sum_src, leaf_opt=False)
    arm_feat = bml.compile_armv6(feat_src)
    arm_rand = bml.compile_armv6(full_bml)

    def fmt_words(words: list) -> str:
        return ", ".join(f"0x{w:08X}u" for w in words)

    call_lines_c = []
    # 1. Leaf sum(-5..100)
    for n in range(-5, 101):
        call_lines_c.append(f"    put_i32(((fn1_t)&code_sum_leaf[0])({n}));")
    # 2. General allocator sum(-5..100)
    for n in range(-5, 101):
        call_lines_c.append(f"    put_i32(((fn1_t)&code_sum_gen[0])({n}));")
    # 3. features.b (347 calls)
    for fn_name, args, rk in feat_calls:
        w_off = arm_feat.func_offsets[fn_name] // 4
        c_args = ", ".join(str(int(a)) for a in args)
        fn_t = f"fn{len(args)}_t"
        if rk == "bool":
            call_lines_c.append(f"    put_bool((({fn_t})&code_feat[{w_off}])({c_args}));")
        else:
            call_lines_c.append(f"    put_i32((({fn_t})&code_feat[{w_off}])({c_args}));")
    # 4. 200 random programs (1000 calls)
    for fn_name, inputs in call_specs:
        w_off = arm_rand.func_offsets[fn_name] // 4
        for a, b, flg in inputs:
            call_lines_c.append(f"    put_i32(((fn3_t)&code_rand[{w_off}])({a}, {b}, {1 if flg else 0}));")

    QEMU_C = f"""\
#include <stdint.h>

#define UART0 ((volatile uint32_t *)0x101F1000u)

static void put_ch(char c) {{ *UART0 = (uint32_t)(uint8_t)c; }}
static void put_str(const char *s) {{ while (*s) put_ch(*s++); }}

static void put_bool(int32_t v)
{{
    put_str(v ? "true\\n" : "false\\n");
}}

static void put_i32(int32_t v)
{{
    char buf[16];
    uint32_t u = (v < 0) ? (0u - (uint32_t)v) : (uint32_t)v;
    if (v < 0) put_ch('-');
    int i = 0;
    if (u == 0) buf[i++] = '0';
    while (u > 0) {{
        buf[i++] = (char)('0' + (u % 10u));
        u /= 10u;
    }}
    while (i > 0) put_ch(buf[--i]);
    put_ch('\\n');
}}

typedef int32_t (*fn1_t)(int32_t);
typedef int32_t (*fn2_t)(int32_t, int32_t);
typedef int32_t (*fn3_t)(int32_t, int32_t, int32_t);

__attribute__((aligned(4)))
static const uint32_t code_sum_leaf[] = {{ {fmt_words(arm_sum_leaf.words)} }};

__attribute__((aligned(4)))
static const uint32_t code_sum_gen[] = {{ {fmt_words(arm_sum_gen.words)} }};

__attribute__((aligned(4)))
static const uint32_t code_feat[] = {{ {fmt_words(arm_feat.words)} }};

__attribute__((aligned(4)))
static const uint32_t code_rand[] = {{ {fmt_words(arm_rand.words)} }};

void c_main(void)
{{
{chr(10).join(call_lines_c)}
}}

__attribute__((naked, section(".text.boot")))
void _start(void)
{{
    __asm__ volatile (
        "ldr sp, =0x04000000\\n"
        /* Enable CP10 and CP11 in CP15 Coprocessor Access Control Register, then set FPEXC.EN */
        "mrc p15, 0, r0, c1, c0, 2\\n"
        "orr r0, r0, #0x00F00000\\n"
        "mcr p15, 0, r0, c1, c0, 2\\n"
        "mcr p15, 0, r0, c7, c5, 4\\n"
        "mov r0, #0x40000000\\n"
        ".word 0xEEE80A10\\n"
        "bl c_main\\n"
        "mov r0, #0x18\\n"
        "ldr r1, =0x20026\\n"
        "svc 0x00123456\\n"
        "1: b 1b\\n"
    );
}}
"""
    qemu_ld = os.path.join(BUILD, "armv6_test.ld")
    with open(qemu_ld, "w", encoding="utf-8") as f:
        f.write(
            "ENTRY(_start)\n"
            "SECTIONS {\n"
            "  . = 0x00010000;\n"
            "  .text : { *(.text.boot) *(.text*) *(.rodata*) }\n"
            "  .data : { *(.data*) }\n"
            "  .bss  : { *(.bss*) *(COMMON) }\n"
            "}\n"
        )
    qemu_c_path = os.path.join(BUILD, "armv6_test.c")
    qemu_elf = os.path.join(BUILD, "armv6_test.elf")
    with open(qemu_c_path, "w", encoding="utf-8") as f:
        f.write(QEMU_C)

    subprocess.run(
        [arm_gcc, "-O2", "-march=armv6", "-marm", "-ffreestanding", "-nostdlib",
         "-T", qemu_ld, "-o", qemu_elf, qemu_c_path, "-lgcc"],
        check=True,
    )
    qemu_out = subprocess.check_output(
        [qemu_arm, "-M", "versatilepb", "-cpu", "arm1176", "-nographic", "-semihosting", "-kernel", qemu_elf],
        text=True,
        stderr=subprocess.DEVNULL,
        timeout=30,
    ).replace("\r\n", "\n").strip().splitlines()

    expected_qemu = lua_sum_out + lua_sum_out + lua_feat_out + lua_rand_out
    if qemu_out != expected_qemu:
        fail(f"Stage 4 ARMv6 QEMU output count {len(qemu_out)} vs expected {len(expected_qemu)}")
        for idx, (ev, qv) in enumerate(zip(expected_qemu, qemu_out)):
            if ev != qv:
                fail(f"Stage 4 ARMv6 QEMU mismatch at line #{idx}: Lua={ev}, ARMv6={qv}")
                break

# ---------------------------------------------------------------- 7. Stage 5: native_load / native_fn / native_run + Pi cache maintenance
sum_bmn_bytes = bml.compile_native_blob(sum_src, leaf_opt=True)
sum_raw_bytes = struct.pack("<9I", *bml.SUM_ARMV6_WORDS)
feat_bmn_bytes = bml.compile_native_blob(feat_src)
rand_bmn_bytes = bml.compile_native_blob(full_bml)

sum_bmn_path = os.path.join(BUILD, "sum.bmn")
sum_raw_path = os.path.join(BUILD, "sum_raw.bin")
feat_bmn_path = os.path.join(BUILD, "features.bmn")
rand_bmn_path = os.path.join(BUILD, "rand.bmn")

for p, data in (
    (sum_bmn_path, sum_bmn_bytes),
    (sum_raw_path, sum_raw_bytes),
    (feat_bmn_path, feat_bmn_bytes),
    (rand_bmn_path, rand_bmn_bytes),
):
    with open(p, "wb") as f:
        f.write(data)

# 7a. Host Lua C API test: native_load, native_fn, native_run (src/bm/bmnative.c + src/bm/api_vm.c)
NATIVE_LUAHOST_C = """\
#include <stdio.h>
#include <stdlib.h>
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

int l_vm_load(lua_State *L);
int l_vm_fn(lua_State *L);
int l_vm_buf(lua_State *L);
int l_vm_buf_get(lua_State *L);
int l_vm_buf_fill(lua_State *L);
int l_vm_bind(lua_State *L);
int l_vm_run(lua_State *L);
int l_native_load(lua_State *L);
int l_native_fn(lua_State *L);
int l_native_run(lua_State *L);
int l_native_compile(lua_State *L);

int main(int argc, char **argv)
{
    if (argc < 2) return 1;
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);
    lua_register(L, "vm_load", l_vm_load);
    lua_register(L, "vm_fn", l_vm_fn);
    lua_register(L, "vm_buf", l_vm_buf);
    lua_register(L, "vm_buf_get", l_vm_buf_get);
    lua_register(L, "vm_buf_fill", l_vm_buf_fill);
    lua_register(L, "vm_bind", l_vm_bind);
    lua_register(L, "vm_run", l_vm_run);
    lua_register(L, "native_load", l_native_load);
    lua_register(L, "native_fn", l_native_fn);
    lua_register(L, "native_run", l_native_run);
    lua_register(L, "native_compile", l_native_compile);
    if (luaL_dofile(L, argv[1]) != LUA_OK) {
        fprintf(stderr, "%s\\n", lua_tostring(L, -1));
        lua_close(L);
        return 1;
    }
    lua_close(L);
    return 0;
}
"""
native_luahost_c = os.path.join(BUILD, "native_luahost.c")
native_luahost_bin = os.path.join(BUILD, "native_luahost")
with open(native_luahost_c, "w", encoding="utf-8") as f:
    f.write(NATIVE_LUAHOST_C)

lua_dir = os.path.join(ROOT, "third_party", "lua")
lua_c_files = [
    os.path.join(lua_dir, fn)
    for fn in sorted(os.listdir(lua_dir))
    if fn.endswith(".c") and fn not in ("lua.c", "luac.c")
]
subprocess.run(
    [
        "gcc", "-O2", "-std=c11", "-DLUA_USE_POSIX",
        f"-I{os.path.join(ROOT, 'src')}",
        f"-I{os.path.join(ROOT, 'src', 'bm')}",
        f"-I{lua_dir}",
        "-o", native_luahost_bin,
        native_luahost_c,
        os.path.join(ROOT, "src", "bm", "bmlc.c"),
        os.path.join(ROOT, "src", "bm", "bmnative.c"),
        os.path.join(ROOT, "src", "bm", "api_vm.c"),
        os.path.join(ROOT, "src", "bm", "bmvm.c"),
        *lua_c_files,
        "-lm",
    ],
    check=True,
)

stage5_lua_script = os.path.join(BUILD, "stage5_test.lua")
with open(stage5_lua_script, "w", encoding="utf-8") as f:
    f.write(f"""\
local function read_blob(path)
  local fh = assert(io.open(path, "rb"))
  local d = fh:read("*a")
  fh:close()
  return d
end

-- 1. Negative checks on native_load
assert(native_load("") == nil, "empty blob should fail")
assert(native_load("BMN1short") == nil, "truncated header should fail")
assert(native_load("\\x00\\x00\\x00\\xEF") == nil, "SVC instruction should fail")
assert(native_load("\\x15\\x0F\\x07\\xEE") == nil, "coprocessor MCR should fail")
assert(native_load("\\x70\\x00\\x20\\xE1") == nil, "BKPT should fail")
assert(native_load("\\x00\\x01\\x00\\xEA") == nil, "out-of-range branch should fail")

-- 2. Raw ARMv6 blob (sum_raw.bin) + BMN1 container (sum.bmn)
local m_raw = assert(native_load(read_blob({sum_raw_path!r})))
local m_sum = assert(native_load(read_blob({sum_bmn_path!r})))
local f_sum = assert(native_fn(m_sum, "sum"))
assert(native_fn(m_sum, "nonexistent") == nil)

for n = -5, 100 do
  local r1 = native_run(m_sum, f_sum, n)
  local r2 = native_run(m_sum, "sum", n)
  local r3 = native_run(m_raw, 0, n)
  assert(r1 == r2 and r1 == r3, "sum mismatch at " .. tostring(n))
  print(tostring(r1))
end

-- 3. features.bmn
local m_feat = assert(native_load(read_blob({feat_bmn_path!r})))
""")
    for fn_name, args, _ in feat_calls:
        lua_args = ", ".join(("true" if a else "false") if isinstance(a, bool) else str(a) for a in args)
        sep = ", " if lua_args else ""
        f.write(f'print(tostring(native_run(m_feat, {fn_name!r}{sep}{lua_args})))\n')

    f.write(f'\nlocal m_rand = assert(native_load(read_blob({rand_bmn_path!r})))\n')
    for fn_name, inputs in call_specs:
        f.write(f'do local fn = assert(native_fn(m_rand, {fn_name!r}))\n')
        for a, b, flg in inputs:
            f.write(f'  print(tostring(native_run(m_rand, fn, {a}, {b}, {"true" if flg else "false"})))\n')
        f.write('end\n')

stage5_lua_out = subprocess.check_output([native_luahost_bin, stage5_lua_script], text=True).strip().splitlines()
expected_stage5 = lua_sum_out + lua_feat_out + lua_rand_out
if stage5_lua_out != expected_stage5:
    fail(f"Stage 5 native_load/native_run host output differs from Lua ({len(stage5_lua_out)} vs {len(expected_stage5)})")

# 7b. Bare-metal ARM1176 QEMU test of bmnative_load + bmnative_sync_cache (D-cache clean & I-cache invalidate) + bmnative_call
if arm_gcc and qemu_arm:
    def fmt_bytes(b: bytes) -> str:
        return ", ".join(f"0x{x:02X}u" for x in b)

    s5_lines_c = []
    for n in range(-5, 101):
        s5_lines_c.append(f"    {{ int32_t a[1] = {{{n}}}, r = 0; if (bmnative_call(m_sum, 0, a, 1, &r) || bmnative_call(m_raw, 0, a, 1, &r)) put_str(\"ERR\\n\"); else put_i32(r); }}")
    for fn_name, args, rk in feat_calls:
        c_arr = ", ".join(str(1 if a else 0) if isinstance(a, bool) else str(int(a)) for a in args)
        arr_decl = f"int32_t a[{max(1, len(args))}] = {{{c_arr if c_arr else '0'}}};"
        out_fn = "put_bool" if rk == "bool" else "put_i32"
        s5_lines_c.append(
            f"    {{ {arr_decl} int32_t r = 0; int fn = bmnative_find(m_feat, \"{fn_name}\"); "
            f"if (bmnative_call(m_feat, fn, a, {len(args)}, &r)) put_str(\"ERR\\n\"); else {out_fn}(r); }}"
        )
    for fn_name, inputs in call_specs:
        s5_lines_c.append(f"    {{ int fn = bmnative_find(m_rand, \"{fn_name}\");")
        for a, b, flg in inputs:
            s5_lines_c.append(
                f"      {{ int32_t args[3] = {{{a}, {b}, {1 if flg else 0}}}, r = 0; "
                f"if (bmnative_call(m_rand, fn, args, 3, &r)) put_str(\"ERR\\n\"); else put_i32(r); }}"
            )
        s5_lines_c.append("    }")

    QEMU_S5_C = f"""\
#include <stddef.h>
#include <stdint.h>
#include "bm/bmnative.h"

#define UART0 ((volatile uint32_t *)0x101F1000u)

static void put_ch(char c) {{ *UART0 = (uint32_t)(uint8_t)c; }}
static void put_str(const char *s) {{ while (*s) put_ch(*s++); }}
static void put_bool(int32_t v) {{ put_str(v ? "true\\n" : "false\\n"); }}

static void put_i32(int32_t v)
{{
    char buf[16];
    uint32_t u = (v < 0) ? (0u - (uint32_t)v) : (uint32_t)v;
    if (v < 0) put_ch('-');
    int i = 0;
    if (u == 0) buf[i++] = '0';
    while (u > 0) {{
        buf[i++] = (char)('0' + (u % 10u));
        u /= 10u;
    }}
    while (i > 0) put_ch(buf[--i]);
    put_ch('\\n');
}}

/* Minimal freestanding heap and string helpers for bmnative.c + cache.c */
static uint8_t heap_buf[512 * 1024] __attribute__((aligned(32)));
static size_t heap_off = 0;

void *calloc(size_t nmemb, size_t size)
{{
    size_t total = (nmemb * size + 31u) & ~31u;
    if (heap_off + total > sizeof(heap_buf)) return NULL;
    uint8_t *p = &heap_buf[heap_off];
    heap_off += total;
    for (size_t i = 0; i < total; i++) p[i] = 0;
    return p;
}}

void free(void *p) {{ (void)p; }}

void *memcpy(void *dst, const void *src, size_t n)
{{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}}

void *memset(void *dst, int c, size_t n)
{{
    uint8_t *d = (uint8_t *)dst;
    for (size_t i = 0; i < n; i++) d[i] = (uint8_t)c;
    return dst;
}}

int strcmp(const char *a, const char *b)
{{
    while (*a && *a == *b) {{ a++; b++; }}
    return (uint8_t)*a - (uint8_t)*b;
}}

static const uint8_t blob_sum[]  = {{ {fmt_bytes(sum_bmn_bytes)} }};
static const uint8_t blob_raw[]  = {{ {fmt_bytes(sum_raw_bytes)} }};
static const uint8_t blob_feat[] = {{ {fmt_bytes(feat_bmn_bytes)} }};
static const uint8_t blob_rand[] = {{ {fmt_bytes(rand_bmn_bytes)} }};

void c_main(void)
{{
    const char *why = "";
    bmnative_mod *m_sum = NULL, *m_raw = NULL, *m_feat = NULL, *m_rand = NULL;
    if (bmnative_load(&m_sum, blob_sum, sizeof(blob_sum), &why) != BMN_OK ||
        bmnative_load(&m_raw, blob_raw, sizeof(blob_raw), &why) != BMN_OK ||
        bmnative_load(&m_feat, blob_feat, sizeof(blob_feat), &why) != BMN_OK ||
        bmnative_load(&m_rand, blob_rand, sizeof(blob_rand), &why) != BMN_OK) {{
        put_str("LOAD_ERR\\n");
        return;
    }}
{chr(10).join(s5_lines_c)}
    /* Test self-modifying / newly written instruction cache flush on ARM1176:
     * Replace 'add r1, r1, r2' (0xE0811002) at word 4 of m_raw with 'sub r1, r1, r2' (0xE0411002),
     * clean D-cache + invalidate I-cache via bmnative_sync_cache, and check sum(10) becomes -55. */
    m_raw->code[4] = 0xE0411002u;
    bmnative_sync_cache(m_raw->code, m_raw->ncode * 4u);
    {{
        int32_t a10[1] = {{10}}, r = 0;
        bmnative_call(m_raw, 0, a10, 1, &r);
        put_i32(r);
    }}
}}

__attribute__((naked, section(".text.boot")))
void _start(void)
{{
    __asm__ volatile (
        "ldr sp, =0x04000000\\n"
        /* Enable CP10 and CP11 in CP15 Coprocessor Access Control Register, then set FPEXC.EN */
        "mrc p15, 0, r0, c1, c0, 2\\n"
        "orr r0, r0, #0x00F00000\\n"
        "mcr p15, 0, r0, c1, c0, 2\\n"
        "mcr p15, 0, r0, c7, c5, 4\\n"
        "mov r0, #0x40000000\\n"
        ".word 0xEEE80A10\\n"
        "bl c_main\\n"
        "mov r0, #0x18\\n"
        "ldr r1, =0x20026\\n"
        "svc 0x00123456\\n"
        "1: b 1b\\n"
    );
}}
"""
    qemu_s5_c = os.path.join(BUILD, "armv6_stage5.c")
    qemu_s5_elf = os.path.join(BUILD, "armv6_stage5.elf")
    with open(qemu_s5_c, "w", encoding="utf-8") as f:
        f.write(QEMU_S5_C)

    subprocess.run(
        [
            arm_gcc, "-O2", "-mcpu=arm1176jzf-s", "-marm", "-ffreestanding", "-nostdlib",
            f"-I{os.path.join(ROOT, 'src')}",
            f"-I{os.path.join(ROOT, 'src', 'arch')}",
            "-T", qemu_ld, "-o", qemu_s5_elf,
            qemu_s5_c,
            os.path.join(ROOT, "src", "bm", "bmnative.c"),
            os.path.join(ROOT, "src", "arch", "cache.c"),
            "-lgcc",
        ],
        check=True,
    )
    qemu_s5_out = subprocess.check_output(
        [qemu_arm, "-M", "versatilepb", "-cpu", "arm1176", "-nographic", "-semihosting", "-kernel", qemu_s5_elf],
        text=True,
        stderr=subprocess.DEVNULL,
        timeout=30,
    ).replace("\r\n", "\n").strip().splitlines()

    expected_s5_qemu = expected_stage5 + ["-55"]
    if qemu_s5_out != expected_s5_qemu:
        fail(f"Stage 5 ARM1176 bmnative_load+cache QEMU output differs ({len(qemu_s5_out)} vs {len(expected_s5_qemu)})")

# ---------------------------------------------------------------- 8. Stage 6: i32 overflow, arrays, structs, and f32 VFPv2
tm_path = os.path.join(HERE, "types_mem.b")
with open(tm_path, encoding="utf-8") as f:
    tm_src = f.read()

tm_mod = bml.compile_source(tm_src)
tm_vm = bml.StackVM(tm_mod)
tm_bmv_path = os.path.join(BUILD, "types_mem.bmv")
tm_bmn_path = os.path.join(BUILD, "types_mem.bmn")
tm_bmn_bytes = bml.compile_native_blob(tm_src)
open(tm_bmv_path, "wb").write(bml.compile_bmvm(tm_src))
open(tm_bmn_path, "wb").write(tm_bmn_bytes)

# 8a. StackVM checks (including 32-bit two's complement wraparound)
vm_s6_out = []
vm_s6_out.append(str(tm_vm.call("wrap_add", [2147483647, 1])))
vm_s6_out.append(str(tm_vm.call("wrap_add", [-2147483648, -1])))
vm_s6_out.append(str(tm_vm.call("wrap_mul", [100000, 100000])))

arr_i = [3, -2, 7, 10, -5]
vm_s6_out.append(str(tm_vm.call("array_sum_scale", [arr_i, 5, 3])))
vm_s6_out.extend(str(x) for x in arr_i)

arr_b = [True, False, True, True, False, False]
vm_s6_out.append(str(tm_vm.call("count_true", [arr_b, 6])))
vm_s6_out.extend("true" if x else "false" for x in arr_b)

body = {"x": 10, "y": -15, "vx": 7, "vy": -9}
vm_s6_out.append(str(tm_vm.call("step_body", [body, 12, 25])))
vm_s6_out.extend(str(body[k]) for k in ("x", "y", "vx", "vy"))

vm_s6_out.append(str(tm_vm.call("f32_poly", [3.0, 4.0])))
vm_s6_out.append(str(tm_vm.call("f32_poly", [0.0, 0.5])))
vm_s6_out.append("true" if tm_vm.call("f32_cmp", [-2.5, 1.25]) else "false")
vm_s6_out.append("true" if tm_vm.call("f32_cmp", [3.0, 1.0]) else "false")

xs_f = [1.5, -2.0, 4.0, 0.5]
ys_f = [2.0, 3.0, -0.5, 6.0]
vm_s6_out.append(str(tm_vm.call("dot_scale_f32", [xs_f, ys_f, 4])))
vm_s6_out.extend(str(int(round(x * 100))) for x in xs_f)

vec_f = {"x": 1.5, "y": -2.0}
vm_s6_out.append(str(tm_vm.call("step_vec2f", [vec_f, 1.5, 6.0])))
vm_s6_out.append(str(int(round(vec_f["x"] * 100))))
vm_s6_out.append(str(int(round(vec_f["y"] * 100))))

vec_m = {"x": 2.0, "y": 3.0}
vm_s6_out.append(str(tm_vm.call("test_struct_methods", [vec_m, 1.5])))
vm_s6_out.append(str(int(round(vec_m["x"] * 100))))
vm_s6_out.append(str(int(round(vec_m["y"] * 100))))
vm_s6_out.append(str(tm_vm.call("test_bitwise", [0x1234, 0x5678])))
vm_s6_out.append(str(tm_vm.call("test_memory_ops", [100])))
vec_c = {"x": 2.0, "y": 3.0}
arr_c = [5, 6]
vm_s6_out.append(str(tm_vm.call("test_compound_ops", [10])))
vm_s6_out.append(str(tm_vm.call("test_compound_struct_array", [vec_c, arr_c, 1])))
vm_s6_out.append(str(tm_vm.call("test_struct_ptr", [100])))
vm_s6_out.append(str(tm_vm.call("test_arr_ptr", [200, 5])))

# 8b. Plain Lua reference oracle (with 32-bit wrap helper for wrap_add/wrap_mul)
s6_lua_ref_path = os.path.join(BUILD, "stage6_ref.lua")
with open(s6_lua_ref_path, "w", encoding="utf-8") as f:
    f.write(LUA_PRELUDE)
    f.write(bml.to_lua(tm_src))
    f.write("""
local function i32_wrap(x)
  local u = x & 0xFFFFFFFF
  if u >= 0x80000000 then return u - 0x100000000 end
  return u
end
print(i32_wrap(wrap_add(2147483647, 1)))
print(i32_wrap(wrap_add(-2147483648, -1)))
print(i32_wrap(wrap_mul(100000, 100000)))

local arr_i = {3, -2, 7, 10, -5}
print(array_sum_scale(arr_i, 5, 3))
for i = 1, 5 do print(arr_i[i]) end

local arr_b = {true, false, true, true, false, false}
print(count_true(arr_b, 6))
for i = 1, 6 do print(tostring(arr_b[i])) end

local body = {x = 10, y = -15, vx = 7, vy = -9}
print(step_body(body, 12, 25))
print(body.x) print(body.y) print(body.vx) print(body.vy)

print(f32_poly(3.0, 4.0))
print(f32_poly(0.0, 0.5))
print(tostring(f32_cmp(-2.5, 1.25)))
print(tostring(f32_cmp(3.0, 1.0)))

local xs_f = {1.5, -2.0, 4.0, 0.5}
local ys_f = {2.0, 3.0, -0.5, 6.0}
print(dot_scale_f32(xs_f, ys_f, 4))
for i = 1, 4 do print(math.floor(xs_f[i] * 100 + 0.5)) end

local vec_f = {x = 1.5, y = -2.0}
print(step_vec2f(vec_f, 1.5, 6.0))
print(math.floor(vec_f.x * 100 + 0.5))
print(math.floor(vec_f.y * 100 + 0.5))

local vec_m = {x = 2.0, y = 3.0}
print(test_struct_methods(vec_m, 1.5))
print(math.floor(vec_m.x * 100 + 0.5))
print(math.floor(vec_m.y * 100 + 0.5))
print(test_bitwise(0x1234, 0x5678))
print(test_memory_ops(100))
local vec_c = {x = 2.0, y = 3.0}
local arr_c = {5, 6}
print(test_compound_ops(10))
print(test_compound_struct_array(vec_c, arr_c, 1))
print(test_struct_ptr(100))
print(test_arr_ptr(200, 5))
""")

lua_s6_out = subprocess.check_output([LUAHOST, s6_lua_ref_path], text=True).strip().splitlines()
if vm_s6_out != lua_s6_out:
    fail(f"Stage 6 StackVM differs from Lua: {vm_s6_out} vs {lua_s6_out}")

# 8c. Host Lua C API: both bmvm (vm_load/vm_buf/vm_run) and native (native_load/native_run with Lua tables + vm_buf)
s6_luahost_script = os.path.join(BUILD, "stage6_luahost.lua")
with open(s6_luahost_script, "w", encoding="utf-8") as f:
    f.write(f"""\
local function read_blob(path)
  local fh = assert(io.open(path, "rb"))
  local d = fh:read("*a")
  fh:close()
  return d
end

local mv = assert(vm_load(read_blob({tm_bmv_path!r})))
local mn = assert(native_load(read_blob({tm_bmn_path!r})))

local function check_both(fn_name, ...)
  local rn = native_run(mn, fn_name, ...)
  return rn
end

print(native_run(mn, "wrap_add", 2147483647, 1))
print(native_run(mn, "wrap_add", -2147483648, -1))
print(native_run(mn, "wrap_mul", 100000, 100000))
assert(vm_run(mv, vm_fn(mv, "wrap_add"), 2147483647, 1) == -2147483648)
assert(vm_run(mv, vm_fn(mv, "wrap_add"), -2147483648, -1) == 2147483647)
assert(vm_run(mv, vm_fn(mv, "wrap_mul"), 100000, 100000) == 1410065408)

local arr_i = {{3, -2, 7, 10, -5}}
print(native_run(mn, "array_sum_scale", arr_i, 5, 3))
for i = 1, 5 do print(arr_i[i]) end

-- Also verify vm_buf with both vm_run and native_run
local bi = vm_buf("i32", 5)
vm_buf_fill(bi, {{3, -2, 7, 10, -5}})
assert(vm_run(mv, vm_fn(mv, "array_sum_scale"), bi, 5, 3) == 39)
assert(vm_buf_get(bi, 1) == 9 and vm_buf_get(bi, 4) == 30)
vm_buf_fill(bi, {{3, -2, 7, 10, -5}})
assert(native_run(mn, "array_sum_scale", bi, 5, 3) == 39)
assert(vm_buf_get(bi, 1) == 9 and vm_buf_get(bi, 4) == 30)

local arr_b = {{true, false, true, true, false, false}}
print(native_run(mn, "count_true", arr_b, 6))
for i = 1, 6 do print(tostring(arr_b[i])) end

local body = {{x = 10, y = -15, vx = 7, vy = -9}}
print(native_run(mn, "step_body", body, 12, 25))
print(body.x) print(body.y) print(body.vx) print(body.vy)

print(native_run(mn, "f32_poly", 3.0, 4.0))
print(native_run(mn, "f32_poly", 0.0, 0.5))
assert(vm_run(mv, vm_fn(mv, "f32_poly"), 3.0, 4.0) == 26)
assert(vm_run(mv, vm_fn(mv, "f32_poly"), 0.0, 0.5) == 16)
print(tostring(native_run(mn, "f32_cmp", -2.5, 1.25)))
print(tostring(native_run(mn, "f32_cmp", 3.0, 1.0)))

local xs_f = {{1.5, -2.0, 4.0, 0.5}}
local ys_f = {{2.0, 3.0, -0.5, 6.0}}
print(native_run(mn, "dot_scale_f32", xs_f, ys_f, 4))
for i = 1, 4 do print(math.floor(xs_f[i] * 100 + 0.5)) end

local vec_f = {{x = 1.5, y = -2.0}}
print(native_run(mn, "step_vec2f", vec_f, 1.5, 6.0))
print(math.floor(vec_f.x * 100 + 0.5))
print(math.floor(vec_f.y * 100 + 0.5))

local vec_m = {{x = 2.0, y = 3.0}}
print(native_run(mn, "test_struct_methods", vec_m, 1.5))
print(math.floor(vec_m.x * 100 + 0.5))
print(math.floor(vec_m.y * 100 + 0.5))
print(native_run(mn, "test_bitwise", 0x1234, 0x5678))
print(native_run(mn, "test_memory_ops", 100))
local vec_c = {{x = 2.0, y = 3.0}}
local arr_c = {{5, 6}}
print(native_run(mn, "test_compound_ops", 10))
print(native_run(mn, "test_compound_struct_array", vec_c, arr_c, 1))
print(native_run(mn, "test_struct_ptr", 100))
print(native_run(mn, "test_arr_ptr", 200, 5))
""")

s6_luahost_out = subprocess.check_output([native_luahost_bin, s6_luahost_script], text=True).strip().splitlines()
if s6_luahost_out != lua_s6_out:
    fail(f"Stage 6 native_run/vm_run differs from Lua: {s6_luahost_out} vs {lua_s6_out}")

# 8d. Bare-metal ARM1176 QEMU with hardware VFPv2 enabled
if arm_gcc and qemu_arm:
    QEMU_S6_C = f"""\
#include <stddef.h>
#include <stdint.h>
#include "bm/bmnative.h"

#define UART0 ((volatile uint32_t *)0x101F1000u)

static void put_ch(char c) {{ *UART0 = (uint32_t)(uint8_t)c; }}
static void put_str(const char *s) {{ while (*s) put_ch(*s++); }}
static void put_bool(int32_t v) {{ put_str(v ? "true\\n" : "false\\n"); }}

static void put_i32(int32_t v)
{{
    char buf[16];
    uint32_t u = (v < 0) ? (0u - (uint32_t)v) : (uint32_t)v;
    if (v < 0) put_ch('-');
    int i = 0;
    if (u == 0) buf[i++] = '0';
    while (u > 0) {{
        buf[i++] = (char)('0' + (u % 10u));
        u /= 10u;
    }}
    while (i > 0) put_ch(buf[--i]);
    put_ch('\\n');
}}

static uint32_t f2u(float f)
{{
    union {{ float f; uint32_t u; }} u;
    u.f = f;
    return u.u;
}}

static float u2f(uint32_t bits)
{{
    union {{ uint32_t u; float f; }} u;
    u.u = bits;
    return u.f;
}}

static uint8_t heap_buf[128 * 1024] __attribute__((aligned(32)));
static size_t heap_off = 0;

void *calloc(size_t nmemb, size_t size)
{{
    size_t total = (nmemb * size + 31u) & ~31u;
    if (heap_off + total > sizeof(heap_buf)) return NULL;
    uint8_t *p = &heap_buf[heap_off];
    heap_off += total;
    for (size_t i = 0; i < total; i++) p[i] = 0;
    return p;
}}
void free(void *p) {{ (void)p; }}
void *memcpy(void *dst, const void *src, size_t n)
{{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}}
void *memset(void *dst, int c, size_t n)
{{
    uint8_t *d = (uint8_t *)dst;
    for (size_t i = 0; i < n; i++) d[i] = (uint8_t)c;
    return dst;
}}
int strcmp(const char *a, const char *b)
{{
    while (*a && *a == *b) {{ a++; b++; }}
    return (uint8_t)*a - (uint8_t)*b;
}}

static const uint8_t blob_tm[] = {{ {fmt_bytes(tm_bmn_bytes)} }};

void c_main(void)
{{
    const char *why = "";
    bmnative_mod *m = NULL;
    if (bmnative_load(&m, blob_tm, sizeof(blob_tm), &why) != BMN_OK) {{
        put_str("LOAD_ERR\\n");
        return;
    }}
    int32_t r = 0;

    /* 1. wrap_add / wrap_mul */
    {{ int32_t a[2] = {{2147483647, 1}}; bmnative_call(m, bmnative_find(m, "wrap_add"), a, 2, &r); put_i32(r); }}
    {{ int32_t a[2] = {{-2147483648, -1}}; bmnative_call(m, bmnative_find(m, "wrap_add"), a, 2, &r); put_i32(r); }}
    {{ int32_t a[2] = {{100000, 100000}}; bmnative_call(m, bmnative_find(m, "wrap_mul"), a, 2, &r); put_i32(r); }}

    /* 2. array_sum_scale */
    {{
        uint32_t arr_i[5] = {{3u, (uint32_t)-2, 7u, 10u, (uint32_t)-5}};
        int32_t a[3] = {{(int32_t)(uintptr_t)arr_i, 5, 3}};
        bmnative_call(m, bmnative_find(m, "array_sum_scale"), a, 3, &r);
        put_i32(r);
        for (int i = 0; i < 5; i++) put_i32((int32_t)arr_i[i]);
    }}

    /* 3. count_true */
    {{
        uint32_t arr_b[6] = {{1, 0, 1, 1, 0, 0}};
        int32_t a[2] = {{(int32_t)(uintptr_t)arr_b, 6}};
        bmnative_call(m, bmnative_find(m, "count_true"), a, 2, &r);
        put_i32(r);
        for (int i = 0; i < 6; i++) put_bool((int32_t)arr_b[i]);
    }}

    /* 4. step_body */
    {{
        int32_t body[4] = {{10, -15, 7, -9}};
        int32_t a[3] = {{(int32_t)(uintptr_t)body, 12, 25}};
        bmnative_call(m, bmnative_find(m, "step_body"), a, 3, &r);
        put_i32(r);
        for (int i = 0; i < 4; i++) put_i32(body[i]);
    }}

    /* 5. f32_poly & f32_cmp */
    {{ int32_t a[2] = {{(int32_t)f2u(3.0f), (int32_t)f2u(4.0f)}}; bmnative_call(m, bmnative_find(m, "f32_poly"), a, 2, &r); put_i32(r); }}
    {{ int32_t a[2] = {{(int32_t)f2u(0.0f), (int32_t)f2u(0.5f)}}; bmnative_call(m, bmnative_find(m, "f32_poly"), a, 2, &r); put_i32(r); }}
    {{ int32_t a[2] = {{(int32_t)f2u(-2.5f), (int32_t)f2u(1.25f)}}; bmnative_call(m, bmnative_find(m, "f32_cmp"), a, 2, &r); put_bool(r); }}
    {{ int32_t a[2] = {{(int32_t)f2u(3.0f), (int32_t)f2u(1.0f)}}; bmnative_call(m, bmnative_find(m, "f32_cmp"), a, 2, &r); put_bool(r); }}

    /* 6. dot_scale_f32 */
    {{
        uint32_t xs[4] = {{f2u(1.5f), f2u(-2.0f), f2u(4.0f), f2u(0.5f)}};
        uint32_t ys[4] = {{f2u(2.0f), f2u(3.0f), f2u(-0.5f), f2u(6.0f)}};
        int32_t a[3] = {{(int32_t)(uintptr_t)xs, (int32_t)(uintptr_t)ys, 4}};
        bmnative_call(m, bmnative_find(m, "dot_scale_f32"), a, 3, &r);
        put_i32(r);
        for (int i = 0; i < 4; i++) {{
            float v = u2f(xs[i]) * 100.0f;
            put_i32((int32_t)(v >= 0.0f ? (v + 0.5f) : (v - 0.5f)));
        }}
    }}

    /* 7. step_vec2f */
    {{
        uint32_t vf[2] = {{f2u(1.5f), f2u(-2.0f)}};
        int32_t a[3] = {{(int32_t)(uintptr_t)vf, (int32_t)f2u(1.5f), (int32_t)f2u(6.0f)}};
        bmnative_call(m, bmnative_find(m, "step_vec2f"), a, 3, &r);
        put_i32(r);
        for (int i = 0; i < 2; i++) {{
            float v = u2f(vf[i]) * 100.0f;
            put_i32((int32_t)(v >= 0.0f ? (v + 0.5f) : (v - 0.5f)));
        }}
    }}

    /* 8. test_struct_methods */
    {{
        uint32_t vm[2] = {{f2u(2.0f), f2u(3.0f)}};
        int32_t a[2] = {{(int32_t)(uintptr_t)vm, (int32_t)f2u(1.5f)}};
        bmnative_call(m, bmnative_find(m, "test_struct_methods"), a, 2, &r);
        put_i32(r);
        for (int i = 0; i < 2; i++) {{
            float v = u2f(vm[i]) * 100.0f;
            put_i32((int32_t)(v >= 0.0f ? (v + 0.5f) : (v - 0.5f)));
        }}
    }}

    /* 9. test_bitwise */
    {{
        int32_t a[2] = {{0x1234, 0x5678}};
        bmnative_call(m, bmnative_find(m, "test_bitwise"), a, 2, &r);
        put_i32(r);
    }}

    /* 10. test_memory_ops */
    {{
        static uint8_t s_mem_buf[1024];
        int32_t a[1] = {{(int32_t)(uintptr_t)s_mem_buf}};
        bmnative_call(m, bmnative_find(m, "test_memory_ops"), a, 1, &r);
        put_i32(r);
    }}

    /* 11. test_compound_ops */
    {{
        int32_t a[1] = {{10}};
        bmnative_call(m, bmnative_find(m, "test_compound_ops"), a, 1, &r);
        put_i32(r);
    }}

    /* 12. test_compound_struct_array */
    {{
        uint32_t vc[2] = {{f2u(2.0f), f2u(3.0f)}};
        int32_t arrc[2] = {{5, 6}};
        int32_t a[3] = {{(int32_t)(uintptr_t)vc, (int32_t)(uintptr_t)arrc, 1}};
        bmnative_call(m, bmnative_find(m, "test_compound_struct_array"), a, 3, &r);
        put_i32(r);
    }}

    /* 13. test_struct_ptr */
    {{
        static uint8_t s_pkt_buf[256];
        int32_t a[1] = {{(int32_t)(uintptr_t)s_pkt_buf}};
        bmnative_call(m, bmnative_find(m, "test_struct_ptr"), a, 1, &r);
        put_i32(r);
    }}

    /* 14. test_arr_ptr */
    {{
        static int32_t s_arr_buf[64];
        int32_t a[2] = {{(int32_t)(uintptr_t)s_arr_buf, 5}};
        bmnative_call(m, bmnative_find(m, "test_arr_ptr"), a, 2, &r);
        put_i32(r);
    }}
}}

__attribute__((naked, section(".text.boot")))
void _start(void)
{{
    __asm__ volatile (
        "ldr sp, =0x04000000\\n"
        /* Enable CP10 and CP11 in CP15 Coprocessor Access Control Register, then set FPEXC.EN */
        "mrc p15, 0, r0, c1, c0, 2\\n"
        "orr r0, r0, #0x00F00000\\n"
        "mcr p15, 0, r0, c1, c0, 2\\n"
        "mcr p15, 0, r0, c7, c5, 4\\n"
        "mov r0, #0x40000000\\n"
        ".word 0xEEE80A10\\n"
        "bl c_main\\n"
        "mov r0, #0x18\\n"
        "ldr r1, =0x20026\\n"
        "svc 0x00123456\\n"
        "1: b 1b\\n"
    );
}}
"""
    qemu_s6_c = os.path.join(BUILD, "armv6_stage6.c")
    qemu_s6_elf = os.path.join(BUILD, "armv6_stage6.elf")
    with open(qemu_s6_c, "w", encoding="utf-8") as f:
        f.write(QEMU_S6_C)

    subprocess.run(
        [
            arm_gcc, "-O2", "-mcpu=arm1176jzf-s", "-marm", "-ffreestanding", "-nostdlib",
            f"-I{os.path.join(ROOT, 'src')}",
            f"-I{os.path.join(ROOT, 'src', 'arch')}",
            "-T", qemu_ld, "-o", qemu_s6_elf,
            qemu_s6_c,
            os.path.join(ROOT, "src", "bm", "bmnative.c"),
            os.path.join(ROOT, "src", "arch", "cache.c"),
            "-lgcc",
        ],
        check=True,
    )
    qemu_s6_out = subprocess.check_output(
        [qemu_arm, "-M", "versatilepb", "-cpu", "arm1176", "-nographic", "-semihosting", "-kernel", qemu_s6_elf],
        text=True,
        stderr=subprocess.DEVNULL,
        timeout=30,
    ).replace("\r\n", "\n").strip().splitlines()

    if qemu_s6_out != lua_s6_out:
        fail(f"Stage 6 ARM1176 VFPv2 QEMU output differs: {qemu_s6_out} vs {lua_s6_out}")

# ---------------------------------------------------------------- 9. Stage 7: Standard functions & bm kernel function table
kf_path = os.path.join(HERE, "kernel_fn.b")
with open(kf_path, encoding="utf-8") as f:
    kf_src = f.read()

kf_mod = bml.compile_source(kf_src)
kf_bmn_bytes = bml.compile_native_blob(kf_src)
kf_bmn_path = os.path.join(BUILD, "kernel_fn.bmn")
open(kf_bmn_path, "wb").write(kf_bmn_bytes)

s7_cases = [
    (100, 120, 60, 100, 0b0101),  # left + up
    (7,   235, 25, 100, 0b1001),  # left + down (clamped at 8 and 232)
    (311, 9,   0,  100, 0b0110),  # right + up (clamped at 312 and 8, 0 bar)
    (160, 100, 100, 100, 0b0000), # no buttons, full bar
]

# 9a. StackVM with host_fns table
vm_s7_out = []
for px, py, hp, max_hp, btn_mask in s7_cases:
    draw_log = []
    host_table = {
        "cls": lambda c, lg=draw_log: lg.append(f"cls:{c}"),
        "pix": lambda x, y, c, lg=draw_log: lg.append(f"pix:{x},{y},{c}"),
        "rectfill": lambda x, y, w, h, c, lg=draw_log: lg.append(f"rectfill:{x},{y},{w},{h},{c}"),
        "spr": lambda idx, x, y, lg=draw_log: lg.append(f"spr:{idx},{x},{y}"),
        "btn": lambda b, m=btn_mask: ((m >> b) & 1) != 0,
        "btnp": lambda b, m=btn_mask: ((m >> b) & 1) != 0,
        "line": lambda x0, y0, x1, y1, c, lg=draw_log: lg.append(f"line:{x0},{y0},{x1},{y1},{c}"),
        "circ": lambda x, y, r, c, lg=draw_log: lg.append(f"circ:{x},{y},{r},{c}"),
        "circfill": lambda x, y, r, c, lg=draw_log: lg.append(f"circfill:{x},{y},{r},{c}"),
        "rect": lambda x, y, w, h, c, lg=draw_log: lg.append(f"rect:{x},{y},{w},{h},{c}"),
        "sfx": lambda sid, lg=draw_log: lg.append(f"sfx:{sid}"),
        "music": lambda mid, lg=draw_log: lg.append(f"music:{mid}"),
        "time": lambda: 1.5,
        "print": lambda s, *extra, lg=draw_log: (lg.append("print:" + str(s) + (" " + " ".join(str(x) for x in extra) if extra else "")), 0)[1],
    }
    kf_vm = bml.StackVM(kf_mod, host_fns=host_table)
    res = kf_vm.call("step_and_draw_hud", [px, py, hp, max_hp])
    vm_s7_out.extend(draw_log)
    vm_s7_out.append(f"ret:{res}")
    draw_log.clear()
    res_ext = kf_vm.call("test_extended_hud", [px, py, hp])
    vm_s7_out.extend(draw_log)
    vm_s7_out.append(f"ret_ext:{res_ext}")
    draw_log.clear()
    res_banner = kf_vm.call("test_print_banner", [hp])
    vm_s7_out.extend(draw_log)
    vm_s7_out.append(f"ret_banner:{res_banner}")

# 9b. Plain Lua reference oracle
s7_lua_ref_path = os.path.join(BUILD, "stage7_ref.lua")
with open(s7_lua_ref_path, "w", encoding="utf-8") as f:
    f.write(LUA_PRELUDE)
    f.write("""
local _orig_print = print
function print(s, ...)
  local parts = { "print:" .. tostring(s) }
  local extra = {...}
  for i = 1, #extra do table.insert(parts, tostring(extra[i])) end
  _orig_print(table.concat(parts, " "))
  return 0
end
local btn_mask = 0
function cls(c) _orig_print("cls:" .. c) end
function pix(x, y, c) _orig_print("pix:" .. x .. "," .. y .. "," .. c) end
function rectfill(x, y, w, h, c) _orig_print("rectfill:" .. x .. "," .. y .. "," .. w .. "," .. h .. "," .. c) end
function spr(id, x, y) _orig_print("spr:" .. id .. "," .. x .. "," .. y) end
function btn(b) return ((btn_mask >> b) & 1) ~= 0 end
function btnp(b) return ((btn_mask >> b) & 1) ~= 0 end
function line(x0, y0, x1, y1, c) _orig_print("line:" .. x0 .. "," .. y0 .. "," .. x1 .. "," .. y1 .. "," .. c) end
function rect(x, y, w, h, c) _orig_print("rect:" .. x .. "," .. y .. "," .. w .. "," .. h .. "," .. c) end
function circ(x, y, r, c) _orig_print("circ:" .. x .. "," .. y .. "," .. r .. "," .. c) end
function circfill(x, y, r, c) _orig_print("circfill:" .. x .. "," .. y .. "," .. r .. "," .. c) end
function sfx(id) _orig_print("sfx:" .. id) end
function music(id) _orig_print("music:" .. id) end
function time() return 1.5 end
""")
    f.write(bml.to_lua(kf_src))
    for px, py, hp, max_hp, btn_mask in s7_cases:
        f.write(f"\nbtn_mask = {btn_mask}\n_orig_print('ret:' .. step_and_draw_hud({px}, {py}, {hp}, {max_hp}))\n")
        f.write(f"_orig_print('ret_ext:' .. test_extended_hud({px}, {py}, {hp}))\n")
        f.write(f"_orig_print('ret_banner:' .. test_print_banner({hp}))\n")

lua_s7_out = subprocess.check_output([LUAHOST, s7_lua_ref_path], text=True).strip().splitlines()
if vm_s7_out != lua_s7_out:
    fail(f"Stage 7 StackVM differs from Lua: {vm_s7_out} vs {lua_s7_out}")

# 9c. Host Lua C API: native_run dispatching __bml_call_host back to Lua globals
s7_luahost_script = os.path.join(BUILD, "stage7_luahost.lua")
with open(s7_luahost_script, "w", encoding="utf-8") as f:
    f.write(f"""\
local fh = assert(io.open({kf_bmn_path!r}, "rb"))
local blob = fh:read("*a")
fh:close()
local mn = assert(native_load(blob))

local _orig_print = print
function print(s, ...)
  local parts = {{ "print:" .. tostring(s) }}
  local extra = {{...}}
  for i = 1, #extra do table.insert(parts, tostring(extra[i])) end
  _orig_print(table.concat(parts, " "))
  return 0
end
local btn_mask = 0
function cls(c) _orig_print("cls:" .. c) end
function pix(x, y, c) _orig_print("pix:" .. x .. "," .. y .. "," .. c) end
function rectfill(x, y, w, h, c) _orig_print("rectfill:" .. x .. "," .. y .. "," .. w .. "," .. h .. "," .. c) end
function spr(id, x, y) _orig_print("spr:" .. id .. "," .. x .. "," .. y) end
function btn(b) return ((btn_mask >> b) & 1) ~= 0 end
function btnp(b) return ((btn_mask >> b) & 1) ~= 0 end
function line(x0, y0, x1, y1, c) _orig_print("line:" .. x0 .. "," .. y0 .. "," .. x1 .. "," .. y1 .. "," .. c) end
function rect(x, y, w, h, c) _orig_print("rect:" .. x .. "," .. y .. "," .. w .. "," .. h .. "," .. c) end
function circ(x, y, r, c) _orig_print("circ:" .. x .. "," .. y .. "," .. r .. "," .. c) end
function circfill(x, y, r, c) _orig_print("circfill:" .. x .. "," .. y .. "," .. r .. "," .. c) end
function sfx(id) _orig_print("sfx:" .. id) end
function music(id) _orig_print("music:" .. id) end
function time() return 1.5 end
""")
    for px, py, hp, max_hp, btn_mask in s7_cases:
        f.write(f"btn_mask = {btn_mask}\n_orig_print('ret:' .. native_run(mn, 'step_and_draw_hud', {px}, {py}, {hp}, {max_hp}))\n")
        f.write(f"_orig_print('ret_ext:' .. native_run(mn, 'test_extended_hud', {px}, {py}, {hp}))\n")
        f.write(f"_orig_print('ret_banner:' .. native_run(mn, 'test_print_banner', {hp}))\n")

s7_luahost_out = subprocess.check_output([native_luahost_bin, s7_luahost_script], text=True).strip().splitlines()
if s7_luahost_out != lua_s7_out:
    fail(f"Stage 7 native_run host callback differs from Lua: {s7_luahost_out} vs {lua_s7_out}")

# 9d. Bare-metal ARM1176 QEMU: native ARMv6 BL __bml_call_host -> bmnative_set_host_fn C table
if arm_gcc and qemu_arm:
    s7_calls_c = []
    for px, py, hp, max_hp, btn_mask in s7_cases:
        s7_calls_c.append(
            f"    g_btn_mask = {btn_mask}; "
            f"{{ int32_t a[4] = {{{px}, {py}, {hp}, {max_hp}}}, r = 0; "
            f"bmnative_call(m, fn, a, 4, &r); put_str(\"ret:\"); put_i32(r); put_ch('\\n'); "
            f"int32_t a_ext[3] = {{{px}, {py}, {hp}}}, r_ext = 0; "
            f"bmnative_call(m, fn_ext, a_ext, 3, &r_ext); put_str(\"ret_ext:\"); put_i32(r_ext); put_ch('\\n'); "
            f"int32_t a_ban[1] = {{{hp}}}, r_ban = 0; "
            f"bmnative_call(m, fn_banner, a_ban, 1, &r_ban); put_str(\"ret_banner:\"); put_i32(r_ban); put_ch('\\n'); }}"
        )

    QEMU_S7_C = f"""\
#include <stddef.h>
#include <stdint.h>
#include "bm/bmnative.h"

#define UART0 ((volatile uint32_t *)0x101F1000u)

static void put_ch(char c) {{ *UART0 = (uint32_t)(uint8_t)c; }}
static void put_str(const char *s) {{ while (*s) put_ch(*s++); }}

static void put_i32(int32_t v)
{{
    char buf[16];
    uint32_t u = (v < 0) ? (0u - (uint32_t)v) : (uint32_t)v;
    if (v < 0) put_ch('-');
    int i = 0;
    if (u == 0) buf[i++] = '0';
    while (u > 0) {{
        buf[i++] = (char)('0' + (u % 10u));
        u /= 10u;
    }}
    while (i > 0) put_ch(buf[--i]);
}}

static int32_t g_btn_mask = 0;

static int32_t kernel_host_table(int32_t host_id, const int32_t *args, int32_t nargs)
{{
    (void)nargs;
    switch (host_id) {{
    case BMN_HOST_CLS:
        put_str("cls:"); put_i32(args[0]); put_ch('\\n');
        return 0;
    case BMN_HOST_PIX:
        put_str("pix:"); put_i32(args[0]); put_ch(','); put_i32(args[1]); put_ch(','); put_i32(args[2]); put_ch('\\n');
        return 0;
    case BMN_HOST_RECTFILL:
        put_str("rectfill:");
        for (int i = 0; i < 5; i++) {{ if (i) put_ch(','); put_i32(args[i]); }}
        put_ch('\\n');
        return 0;
    case BMN_HOST_SPR:
        put_str("spr:"); put_i32(args[0]); put_ch(','); put_i32(args[1]); put_ch(','); put_i32(args[2]); put_ch('\\n');
        return 0;
    case BMN_HOST_BTN:
        return (g_btn_mask >> args[0]) & 1;
    case BMN_HOST_BTNP:
        return (g_btn_mask >> args[0]) & 1;
    case BMN_HOST_LINE:
        put_str("line:");
        for (int i = 0; i < 5; i++) {{ if (i) put_ch(','); put_i32(args[i]); }}
        put_ch('\\n');
        return 0;
    case BMN_HOST_RECT:
        put_str("rect:");
        for (int i = 0; i < 5; i++) {{ if (i) put_ch(','); put_i32(args[i]); }}
        put_ch('\\n');
        return 0;
    case BMN_HOST_CIRC:
        put_str("circ:");
        for (int i = 0; i < 4; i++) {{ if (i) put_ch(','); put_i32(args[i]); }}
        put_ch('\\n');
        return 0;
    case BMN_HOST_CIRCFILL:
        put_str("circfill:");
        for (int i = 0; i < 4; i++) {{ if (i) put_ch(','); put_i32(args[i]); }}
        put_ch('\\n');
        return 0;
    case BMN_HOST_SFX:
        put_str("sfx:"); put_i32(args[0]); put_ch('\\n');
        return 0;
    case BMN_HOST_MUSIC:
        put_str("music:"); put_i32(args[0]); put_ch('\\n');
        return 0;
    case BMN_HOST_TIME: {{
        float t = 1.5f;
        uint32_t bits = 0;
        for (size_t i = 0; i < 4; i++) ((char*)&bits)[i] = ((char*)&t)[i];
        return (int32_t)bits;
    }}
    case BMN_HOST_PRINT: {{
        put_str("print:");
        put_str((const char *)args[0]);
        for (int i = 1; i < nargs; i++) {{
            put_ch(' ');
            put_i32(args[i]);
        }}
        put_ch('\\n');
        return 0;
    }}
    default:
        return 0;
    }}
}}

static uint8_t heap_buf[64 * 1024] __attribute__((aligned(32)));
static size_t heap_off = 0;
void *calloc(size_t nmemb, size_t size)
{{
    size_t total = (nmemb * size + 31u) & ~31u;
    if (heap_off + total > sizeof(heap_buf)) return NULL;
    uint8_t *p = &heap_buf[heap_off];
    heap_off += total;
    for (size_t i = 0; i < total; i++) p[i] = 0;
    return p;
}}
void free(void *p) {{ (void)p; }}
void *memcpy(void *dst, const void *src, size_t n)
{{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}}
void *memset(void *dst, int c, size_t n)
{{
    uint8_t *d = (uint8_t *)dst;
    for (size_t i = 0; i < n; i++) d[i] = (uint8_t)c;
    return dst;
}}
int strcmp(const char *a, const char *b)
{{
    while (*a && *a == *b) {{ a++; b++; }}
    return (uint8_t)*a - (uint8_t)*b;
}}

static const uint8_t blob_kf[] = {{ {fmt_bytes(kf_bmn_bytes)} }};

void c_main(void)
{{
    bmnative_set_host_fn(kernel_host_table);
    const char *why = "";
    bmnative_mod *m = NULL;
    if (bmnative_load(&m, blob_kf, sizeof(blob_kf), &why) != BMN_OK) {{
        put_str("LOAD_ERR\\n");
        return;
    }}
    int fn = bmnative_find(m, "step_and_draw_hud");
    int fn_ext = bmnative_find(m, "test_extended_hud");
    int fn_banner = bmnative_find(m, "test_print_banner");
{chr(10).join(s7_calls_c)}
}}

__attribute__((naked, section(".text.boot")))
void _start(void)
{{
    __asm__ volatile (
        "ldr sp, =0x04000000\\n"
        /* Enable CP10 and CP11 in CP15 Coprocessor Access Control Register, then set FPEXC.EN */
        "mrc p15, 0, r0, c1, c0, 2\\n"
        "orr r0, r0, #0x00F00000\\n"
        "mcr p15, 0, r0, c1, c0, 2\\n"
        "mcr p15, 0, r0, c7, c5, 4\\n"
        "mov r0, #0x40000000\\n"
        ".word 0xEEE80A10\\n"
        "bl c_main\\n"
        "mov r0, #0x18\\n"
        "ldr r1, =0x20026\\n"
        "svc 0x00123456\\n"
        "1: b 1b\\n"
    );
}}
"""
    qemu_s7_c = os.path.join(BUILD, "armv6_stage7.c")
    qemu_s7_elf = os.path.join(BUILD, "armv6_stage7.elf")
    with open(qemu_s7_c, "w", encoding="utf-8") as f:
        f.write(QEMU_S7_C)

    subprocess.run(
        [
            arm_gcc, "-O2", "-mcpu=arm1176jzf-s", "-marm", "-ffreestanding", "-nostdlib",
            f"-I{os.path.join(ROOT, 'src')}",
            f"-I{os.path.join(ROOT, 'src', 'arch')}",
            "-T", qemu_ld, "-o", qemu_s7_elf,
            qemu_s7_c,
            os.path.join(ROOT, "src", "bm", "bmnative.c"),
            os.path.join(ROOT, "src", "arch", "cache.c"),
            "-lgcc",
        ],
        check=True,
    )
    qemu_s7_out = subprocess.check_output(
        [qemu_arm, "-M", "versatilepb", "-cpu", "arm1176", "-nographic", "-semihosting", "-kernel", qemu_s7_elf],
        text=True,
        stderr=subprocess.DEVNULL,
        timeout=30,
    ).replace("\r\n", "\n").strip().splitlines()

    if qemu_s7_out != lua_s7_out:
        fail(f"Stage 7 ARM1176 QEMU kernel function table output differs: {qemu_s7_out} vs {lua_s7_out}")

# ---------------------------------------------------------------- 10. Stage 8: Dark Night enemy movement & separation (darknight_enemy.b)
dn_path = os.path.join(HERE, "darknight_enemy.b")
with open(dn_path, encoding="utf-8") as f:
    dn_src = f.read()

dn_mod = bml.compile_source(dn_src)
dn_vm = bml.StackVM(dn_mod)
dn_bmv_path = os.path.join(BUILD, "darknight_enemy.bmv")
dn_bmn_path = os.path.join(BUILD, "darknight_enemy.bmn")
dn_bmn_bytes = bml.compile_native_blob(dn_src)
open(dn_bmv_path, "wb").write(bml.compile_bmvm(dn_src))
open(dn_bmn_path, "wb").write(dn_bmn_bytes)

# Initial scene of 17 enemies with exact binary32 coordinates (multiples of 1/64) so f32 and f64 are bit-identical
DN_N = 17
DN_FRAMES = 60
dn_init_x  = [0.5, 0.875, 1.25, 1.5, 2.0, 2.375, 2.75, 3.125, 3.5, 3.875, 4.25, 4.625, 0.75, 1.625, 2.5, 3.375, 4.125]
dn_init_z  = [0.5, 0.625, 1.125, 1.75, 2.125, 2.25, 2.875, 3.25, 3.625, 3.75, 4.375, 4.5, 0.875, 1.875, 2.625, 3.5, 4.25]
dn_init_vx = [0.0625, -0.0625, 0.03125, -0.03125, 0.0625, -0.03125, 0.03125, -0.0625, 0.0625, -0.0625, 0.03125, -0.03125, 0.0625, -0.0625, 0.03125, -0.03125, 0.0625]
dn_init_vz = [0.03125, 0.0625, -0.0625, 0.03125, -0.03125, 0.0625, -0.0625, 0.03125, -0.03125, 0.0625, -0.0625, 0.03125, -0.03125, 0.0625, -0.0625, 0.03125, -0.0625]
dn_init_alive = [True, True, True, False, True, True, True, True, True, False, True, True, True, True, True, True, True]

# 10a. StackVM simulation of 60 frames + cam_blocked
sx = list(dn_init_x)
sz = list(dn_init_z)
svx = list(dn_init_vx)
svz = list(dn_init_vz)
salive = list(dn_init_alive)
sout = [0] * (DN_N * DN_N)
scounts = [0] * DN_N
vm_pairs_total = 0
vm_hit_sum = 0
for _ in range(DN_FRAMES):
    vm_pairs_total += dn_vm.call("move_and_separate", [DN_N, sx, sz, svx, svz, salive, sout, scounts])
    for e_idx in range(DN_N):
        for k_idx in range(scounts[e_idx]):
            vm_hit_sum += sout[e_idx * DN_N + k_idx]

doors_ox = [1.5, 3.0, 4.5]
doors_oz = [1.5, 3.0, 4.5]
doors_r  = [0.5, 0.5, 0.5]
vm_cam1 = dn_vm.call("cam_blocked", [1.6, 1.6, 3, doors_ox, doors_oz, doors_r])
vm_cam2 = dn_vm.call("cam_blocked", [0.2, 0.2, 3, doors_ox, doors_oz, doors_r])
vm_s8_out = [
    str(vm_pairs_total),
    str(vm_hit_sum),
    "true" if vm_cam1 else "false",
    "true" if vm_cam2 else "false",
    str(int(round(sx[0] * 64))),
    str(int(round(sz[0] * 64))),
    str(int(round(sx[16] * 64))),
    str(int(round(sz[16] * 64))),
]

# 10b. Plain Lua reference oracle
s8_lua_ref_path = os.path.join(BUILD, "stage8_ref.lua")
with open(s8_lua_ref_path, "w", encoding="utf-8") as f:
    f.write(LUA_PRELUDE)
    f.write(bml.to_lua(dn_src))
    f.write(f"""
local N, FRAMES = {DN_N}, {DN_FRAMES}
local x = {{{", ".join(str(v) for v in dn_init_x)}}}
local z = {{{", ".join(str(v) for v in dn_init_z)}}}
local vx = {{{", ".join(str(v) for v in dn_init_vx)}}}
local vz = {{{", ".join(str(v) for v in dn_init_vz)}}}
local alive = {{{", ".join("true" if v else "false" for v in dn_init_alive)}}}
local out, counts = {{}}, {{}}
for i = 1, N * N do out[i] = 0 end
for i = 1, N do counts[i] = 0 end

local pairs_total, hit_sum = 0, 0
for _ = 1, FRAMES do
  pairs_total = pairs_total + move_and_separate(N, x, z, vx, vz, alive, out, counts)
  for e = 1, N do
    for k = 1, counts[e] do
      hit_sum = hit_sum + out[(e - 1) * N + k]
    end
  end
end

local ox = {{1.5, 3.0, 4.5}}
local oz = {{1.5, 3.0, 4.5}}
local r  = {{0.5, 0.5, 0.5}}
print(pairs_total)
print(hit_sum)
print(tostring(cam_blocked(1.6, 1.6, 3, ox, oz, r)))
print(tostring(cam_blocked(0.2, 0.2, 3, ox, oz, r)))
print(math.floor(x[1] * 64 + 0.5))
print(math.floor(z[1] * 64 + 0.5))
print(math.floor(x[17] * 64 + 0.5))
print(math.floor(z[17] * 64 + 0.5))
""")

lua_s8_out = subprocess.check_output([LUAHOST, s8_lua_ref_path], text=True).strip().splitlines()
if vm_s8_out != lua_s8_out:
    fail(f"Stage 8 Dark Night StackVM differs from Lua: {vm_s8_out} vs {lua_s8_out}")

# 10c. Host Lua C API: both bmvm (vm_run with vm_buf) and native (native_run)
s8_luahost_script = os.path.join(BUILD, "stage8_luahost.lua")
with open(s8_luahost_script, "w", encoding="utf-8") as f:
    f.write(f"""\
local function read_blob(path)
  local fh = assert(io.open(path, "rb"))
  local d = fh:read("*a")
  fh:close()
  return d
end

local mv = assert(vm_load(read_blob({dn_bmv_path!r})))
local mn = assert(native_load(read_blob({dn_bmn_path!r})))
local fn_nat = assert(native_fn(mn, "move_and_separate"))
local fn_vm  = assert(vm_fn(mv, "move_and_separate"))

local N, FRAMES = {DN_N}, {DN_FRAMES}
local x = {{{", ".join(str(v) for v in dn_init_x)}}}
local z = {{{", ".join(str(v) for v in dn_init_z)}}}
local vx = {{{", ".join(str(v) for v in dn_init_vx)}}}
local vz = {{{", ".join(str(v) for v in dn_init_vz)}}}
local alive = {{{", ".join("true" if v else "false" for v in dn_init_alive)}}}
local out, counts = {{}}, {{}}
for i = 1, N * N do out[i] = 0 end
for i = 1, N do counts[i] = 0 end

local pairs_total, hit_sum = 0, 0
for _ = 1, FRAMES do
  pairs_total = pairs_total + native_run(mn, fn_nat, N, x, z, vx, vz, alive, out, counts)
  for e = 1, N do
    for k = 1, counts[e] do
      hit_sum = hit_sum + out[(e - 1) * N + k]
    end
  end
end

-- Also verify bmvm on the exact same 60 frames using vm_buf
local bx = vm_buf("f64", N); vm_buf_fill(bx, {{{", ".join(str(v) for v in dn_init_x)}}})
local bz = vm_buf("f64", N); vm_buf_fill(bz, {{{", ".join(str(v) for v in dn_init_z)}}})
local bvx = vm_buf("f64", N); vm_buf_fill(bvx, {{{", ".join(str(v) for v in dn_init_vx)}}})
local bvz = vm_buf("f64", N); vm_buf_fill(bvz, {{{", ".join(str(v) for v in dn_init_vz)}}})
local balive = vm_buf("i32", N); vm_buf_fill(balive, {{{", ".join("true" if v else "false" for v in dn_init_alive)}}})
local bout = vm_buf("i32", N * N)
local bcounts = vm_buf("i32", N)
local vm_pairs = 0
for _ = 1, FRAMES do
  vm_pairs = vm_pairs + vm_run(mv, fn_vm, N, bx, bz, bvx, bvz, balive, bout, bcounts)
end
assert(vm_pairs == pairs_total, "bmvm vs native_run pairs mismatch")

local ox = {{1.5, 3.0, 4.5}}
local oz = {{1.5, 3.0, 4.5}}
local r  = {{0.5, 0.5, 0.5}}
print(pairs_total)
print(hit_sum)
print(tostring(native_run(mn, "cam_blocked", 1.6, 1.6, 3, ox, oz, r)))
print(tostring(native_run(mn, "cam_blocked", 0.2, 0.2, 3, ox, oz, r)))
print(math.floor(x[1] * 64 + 0.5))
print(math.floor(z[1] * 64 + 0.5))
print(math.floor(x[17] * 64 + 0.5))
print(math.floor(z[17] * 64 + 0.5))
""")

s8_luahost_out = subprocess.check_output([native_luahost_bin, s8_luahost_script], text=True).strip().splitlines()
if s8_luahost_out != lua_s8_out:
    fail(f"Stage 8 Dark Night native_run/vm_run differs from Lua: {s8_luahost_out} vs {lua_s8_out}")

# 10d. Bare-metal ARM1176 VFPv2 QEMU: 60 frames of Dark Night move_and_separate + cam_blocked
if arm_gcc and qemu_arm:
    c_init_x = ", ".join(f"{v}f" for v in dn_init_x)
    c_init_z = ", ".join(f"{v}f" for v in dn_init_z)
    c_init_vx = ", ".join(f"{v}f" for v in dn_init_vx)
    c_init_vz = ", ".join(f"{v}f" for v in dn_init_vz)
    c_init_alive = ", ".join("1u" if v else "0u" for v in dn_init_alive)

    QEMU_S8_C = f"""\
#include <stddef.h>
#include <stdint.h>
#include "bm/bmnative.h"

#define UART0 ((volatile uint32_t *)0x101F1000u)

static void put_ch(char c) {{ *UART0 = (uint32_t)(uint8_t)c; }}
static void put_str(const char *s) {{ while (*s) put_ch(*s++); }}
static void put_bool(int32_t v) {{ put_str(v ? "true\\n" : "false\\n"); }}

static void put_i32(int32_t v)
{{
    char buf[16];
    uint32_t u = (v < 0) ? (0u - (uint32_t)v) : (uint32_t)v;
    if (v < 0) put_ch('-');
    int i = 0;
    if (u == 0) buf[i++] = '0';
    while (u > 0) {{
        buf[i++] = (char)('0' + (u % 10u));
        u /= 10u;
    }}
    while (i > 0) put_ch(buf[--i]);
    put_ch('\\n');
}}

static uint32_t f2u(float f)
{{
    union {{ float f; uint32_t u; }} u;
    u.f = f;
    return u.u;
}}

static float u2f(uint32_t bits)
{{
    union {{ uint32_t u; float f; }} u;
    u.u = bits;
    return u.f;
}}

static uint8_t heap_buf[128 * 1024] __attribute__((aligned(32)));
static size_t heap_off = 0;
void *calloc(size_t nmemb, size_t size)
{{
    size_t total = (nmemb * size + 31u) & ~31u;
    if (heap_off + total > sizeof(heap_buf)) return NULL;
    uint8_t *p = &heap_buf[heap_off];
    heap_off += total;
    for (size_t i = 0; i < total; i++) p[i] = 0;
    return p;
}}
void free(void *p) {{ (void)p; }}
void *memcpy(void *dst, const void *src, size_t n)
{{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}}
void *memset(void *dst, int c, size_t n)
{{
    uint8_t *d = (uint8_t *)dst;
    for (size_t i = 0; i < n; i++) d[i] = (uint8_t)c;
    return dst;
}}
int strcmp(const char *a, const char *b)
{{
    while (*a && *a == *b) {{ a++; b++; }}
    return (uint8_t)*a - (uint8_t)*b;
}}

static const uint8_t blob_dn[] = {{ {fmt_bytes(dn_bmn_bytes)} }};

void c_main(void)
{{
    const char *why = "";
    bmnative_mod *m = NULL;
    if (bmnative_load(&m, blob_dn, sizeof(blob_dn), &why) != BMN_OK) {{
        put_str("LOAD_ERR\\n");
        return;
    }}
    const float init_x[{DN_N}]  = {{ {c_init_x} }};
    const float init_z[{DN_N}]  = {{ {c_init_z} }};
    const float init_vx[{DN_N}] = {{ {c_init_vx} }};
    const float init_vz[{DN_N}] = {{ {c_init_vz} }};
    uint32_t x[{DN_N}], z[{DN_N}], vx[{DN_N}], vz[{DN_N}];
    uint32_t alive[{DN_N}] = {{ {c_init_alive} }};
    int32_t out[{DN_N * DN_N}];
    int32_t counts[{DN_N}];

    for (int i = 0; i < {DN_N}; i++) {{
        x[i] = f2u(init_x[i]);
        z[i] = f2u(init_z[i]);
        vx[i] = f2u(init_vx[i]);
        vz[i] = f2u(init_vz[i]);
        counts[i] = 0;
    }}
    for (int i = 0; i < {DN_N * DN_N}; i++) out[i] = 0;

    int fn_move = bmnative_find(m, "move_and_separate");
    int fn_cam  = bmnative_find(m, "cam_blocked");
    int32_t pairs_total = 0, hit_sum = 0;
    int32_t a[8] = {{
        {DN_N},
        (int32_t)(uintptr_t)x,
        (int32_t)(uintptr_t)z,
        (int32_t)(uintptr_t)vx,
        (int32_t)(uintptr_t)vz,
        (int32_t)(uintptr_t)alive,
        (int32_t)(uintptr_t)out,
        (int32_t)(uintptr_t)counts
    }};
    for (int f = 0; f < {DN_FRAMES}; f++) {{
        int32_t r = 0;
        bmnative_call(m, fn_move, a, 8, &r);
        pairs_total += r;
        for (int e = 0; e < {DN_N}; e++) {{
            for (int k = 0; k < counts[e]; k++)
                hit_sum += out[e * {DN_N} + k];
        }}
    }}

    uint32_t ox[3] = {{ f2u(1.5f), f2u(3.0f), f2u(4.5f) }};
    uint32_t oz[3] = {{ f2u(1.5f), f2u(3.0f), f2u(4.5f) }};
    uint32_t rad[3] = {{ f2u(0.5f), f2u(0.5f), f2u(0.5f) }};
    int32_t cam1 = 0, cam2 = 0;
    {{ int32_t ca[6] = {{ (int32_t)f2u(1.6f), (int32_t)f2u(1.6f), 3, (int32_t)(uintptr_t)ox, (int32_t)(uintptr_t)oz, (int32_t)(uintptr_t)rad }}; bmnative_call(m, fn_cam, ca, 6, &cam1); }}
    {{ int32_t ca[6] = {{ (int32_t)f2u(0.2f), (int32_t)f2u(0.2f), 3, (int32_t)(uintptr_t)ox, (int32_t)(uintptr_t)oz, (int32_t)(uintptr_t)rad }}; bmnative_call(m, fn_cam, ca, 6, &cam2); }}

    put_i32(pairs_total);
    put_i32(hit_sum);
    put_bool(cam1);
    put_bool(cam2);
    put_i32((int32_t)(u2f(x[0]) * 64.0f + 0.5f));
    put_i32((int32_t)(u2f(z[0]) * 64.0f + 0.5f));
    put_i32((int32_t)(u2f(x[16]) * 64.0f + 0.5f));
    put_i32((int32_t)(u2f(z[16]) * 64.0f + 0.5f));
}}

__attribute__((naked, section(".text.boot")))
void _start(void)
{{
    __asm__ volatile (
        "ldr sp, =0x04000000\\n"
        "mrc p15, 0, r0, c1, c0, 2\\n"
        "orr r0, r0, #0x00F00000\\n"
        "mcr p15, 0, r0, c1, c0, 2\\n"
        "mcr p15, 0, r0, c7, c5, 4\\n"
        "mov r0, #0x40000000\\n"
        ".word 0xEEE80A10\\n"
        "bl c_main\\n"
        "mov r0, #0x18\\n"
        "ldr r1, =0x20026\\n"
        "svc 0x00123456\\n"
        "1: b 1b\\n"
    );
}}
"""
    qemu_s8_c = os.path.join(BUILD, "armv6_stage8.c")
    qemu_s8_elf = os.path.join(BUILD, "armv6_stage8.elf")
    with open(qemu_s8_c, "w", encoding="utf-8") as f:
        f.write(QEMU_S8_C)

    subprocess.run(
        [
            arm_gcc, "-O2", "-mcpu=arm1176jzf-s", "-marm", "-ffreestanding", "-nostdlib",
            f"-I{os.path.join(ROOT, 'src')}",
            f"-I{os.path.join(ROOT, 'src', 'arch')}",
            "-T", qemu_ld, "-o", qemu_s8_elf,
            qemu_s8_c,
            os.path.join(ROOT, "src", "bm", "bmnative.c"),
            os.path.join(ROOT, "src", "arch", "cache.c"),
            "-lgcc",
        ],
        check=True,
    )
    qemu_s8_out = subprocess.check_output(
        [qemu_arm, "-M", "versatilepb", "-cpu", "arm1176", "-nographic", "-semihosting", "-kernel", qemu_s8_elf],
        text=True,
        stderr=subprocess.DEVNULL,
        timeout=30,
    ).replace("\r\n", "\n").strip().splitlines()

    if qemu_s8_out != lua_s8_out:
        fail(f"Stage 8 Dark Night ARM1176 VFPv2 QEMU output differs: {qemu_s8_out} vs {lua_s8_out}")

# ---------------------------------------------------------------- 11. Stage 9: On-console compiler in C (src/bm/bmlc.c / native_compile)
s9_luahost_script = os.path.join(BUILD, "stage9_luahost.lua")
with open(s9_luahost_script, "w", encoding="utf-8") as f:
    f.write(f"""\
local function read_text(path)
  local fh = assert(io.open(path, "r"))
  local d = fh:read("*a")
  fh:close()
  return d
end

-- 1. Negative checks on native_compile: all BAD_PROGRAMS must return nil + "line N: ..."
""")
    for bad_idx, (_, bad_src, _) in enumerate(BAD_PROGRAMS):
        f.write(f"""\
do
  local blob, err = native_compile({bad_src!r})
  assert(blob == nil and type(err) == "string" and err:find("^line %d+:"),
         "bad program #{bad_idx} should fail in native_compile, got: " .. tostring(err))
end
""")
    f.write(f"""
-- 2. Compile sum.b in C via native_compile, load, and verify sum(-5..100)
local sum_blob = assert(native_compile(read_text({sum_path!r})))
local m_sum = assert(native_load(sum_blob))
for n = -5, 100 do
  print(tostring(native_run(m_sum, "sum", n)))
end

-- 3. Compile features.b in C via native_compile, load, and verify all feat_calls
local feat_blob = assert(native_compile(read_text({feat_path!r})))
local m_feat = assert(native_load(feat_blob))
""")
    for fn_name, args, _ in feat_calls:
        lua_args = ", ".join(("true" if a else "false") if isinstance(a, bool) else str(a) for a in args)
        sep = ", " if lua_args else ""
        f.write(f'print(tostring(native_run(m_feat, {fn_name!r}{sep}{lua_args})))\n')

    f.write(f"""
-- 4. Compile types_mem.b (Stage 6: i32 wrap, arrays, structs, f32 VFPv2) in C via native_compile
local tm_blob = assert(native_compile(read_text({tm_path!r})))
local m_tm = assert(native_load(tm_blob))
print(native_run(m_tm, "wrap_add", 2147483647, 1))
print(native_run(m_tm, "wrap_add", -2147483648, -1))
print(native_run(m_tm, "wrap_mul", 100000, 100000))

local ai = {{3, -2, 7, 10, -5}}
print(native_run(m_tm, "array_sum_scale", ai, 5, 3))
for i = 1, 5 do print(ai[i]) end

local ab = {{true, false, true, true, false, false}}
print(native_run(m_tm, "count_true", ab, 6))
for i = 1, 6 do print(tostring(ab[i])) end

local body = {{x = 10, y = -15, vx = 7, vy = -9}}
print(native_run(m_tm, "step_body", body, 12, 25))
print(body.x)
print(body.y)
print(body.vx)
print(body.vy)

print(native_run(m_tm, "f32_poly", 3.0, 4.0))
print(native_run(m_tm, "f32_poly", 0.0, 0.5))
print(tostring(native_run(m_tm, "f32_cmp", -2.5, 1.25)))
print(tostring(native_run(m_tm, "f32_cmp", 3.0, 1.0)))

local xs = {{1.5, -2.0, 4.0, 0.5}}
local ys = {{2.0, 3.0, -0.5, 6.0}}
print(native_run(m_tm, "dot_scale_f32", xs, ys, 4))
for i = 1, 4 do print(math.floor(xs[i] * 100 + 0.5)) end

local vec = {{x = 1.5, y = -2.0}}
print(native_run(m_tm, "step_vec2f", vec, 1.5, 6.0))
print(math.floor(vec.x * 100 + 0.5))
print(math.floor(vec.y * 100 + 0.5))

local vec_m = {{x = 2.0, y = 3.0}}
print(native_run(m_tm, "test_struct_methods", vec_m, 1.5))
print(math.floor(vec_m.x * 100 + 0.5))
print(math.floor(vec_m.y * 100 + 0.5))
print(native_run(m_tm, "test_bitwise", 0x1234, 0x5678))
print(native_run(m_tm, "test_memory_ops", 100))
local vec_c = {{x = 2.0, y = 3.0}}
local arr_c = {{5, 6}}
print(native_run(m_tm, "test_compound_ops", 10))
print(native_run(m_tm, "test_compound_struct_array", vec_c, arr_c, 1))
print(native_run(m_tm, "test_struct_ptr", 100))
print(native_run(m_tm, "test_arr_ptr", 200, 5))

-- 5. Compile kernel_fn.b (Stage 7: cls, pix, rectfill, spr, btn) in C via native_compile
local kf_blob = assert(native_compile(read_text({kf_path!r})))
local m_kf = assert(native_load(kf_blob))
local g_mask = 0
local _orig_print = print
function print(s, ...)
  local parts = {{ "print:" .. tostring(s) }}
  local extra = {{...}}
  for i = 1, #extra do table.insert(parts, tostring(extra[i])) end
  _orig_print(table.concat(parts, " "))
  return 0
end
function cls(c) _orig_print(string.format("cls:%d", c)) end
function pix(x, y, c) _orig_print(string.format("pix:%d,%d,%d", x, y, c)) end
function rectfill(x, y, w, h, c) _orig_print(string.format("rectfill:%d,%d,%d,%d,%d", x, y, w, h, c)) end
function spr(id, x, y) _orig_print(string.format("spr:%d,%d,%d", id, x, y)) end
function btn(b) return ((g_mask >> b) & 1) ~= 0 end
function btnp(b) return ((g_mask >> b) & 1) ~= 0 end
function line(x0, y0, x1, y1, c) _orig_print(string.format("line:%d,%d,%d,%d,%d", x0, y0, x1, y1, c)) end
function rect(x, y, w, h, c) _orig_print(string.format("rect:%d,%d,%d,%d,%d", x, y, w, h, c)) end
function circ(x, y, r, c) _orig_print(string.format("circ:%d,%d,%d,%d", x, y, r, c)) end
function circfill(x, y, r, c) _orig_print(string.format("circfill:%d,%d,%d,%d", x, y, r, c)) end
function sfx(id) _orig_print(string.format("sfx:%d", id)) end
function music(id) _orig_print(string.format("music:%d", id)) end
function time() return 1.5 end
""")
    for px, py, hp, max_hp, mask in s7_cases:
        f.write(f"g_mask = {mask}\n")
        f.write(f'_orig_print(string.format("ret:%d", native_run(m_kf, "step_and_draw_hud", {px}, {py}, {hp}, {max_hp})))\n')
        f.write(f'_orig_print(string.format("ret_ext:%d", native_run(m_kf, "test_extended_hud", {px}, {py}, {hp})))\n')
        f.write(f'_orig_print(string.format("ret_banner:%d", native_run(m_kf, "test_print_banner", {hp})))\n')

    f.write(f"""
-- Restore print for darknight_enemy
print = _orig_print

-- 6. Compile darknight_enemy.b (Stage 8: 60 frames of 17 enemies) in C via native_compile
local dn_blob = assert(native_compile(read_text({dn_path!r})))
local m_dn = assert(native_load(dn_blob))
local N, FRAMES = {DN_N}, {DN_FRAMES}
local x = {{{", ".join(str(v) for v in dn_init_x)}}}
local z = {{{", ".join(str(v) for v in dn_init_z)}}}
local vx = {{{", ".join(str(v) for v in dn_init_vx)}}}
local vz = {{{", ".join(str(v) for v in dn_init_vz)}}}
local alive = {{{", ".join("true" if v else "false" for v in dn_init_alive)}}}
local out, counts = {{}}, {{}}
for i = 1, N * N do out[i] = 0 end
for i = 1, N do counts[i] = 0 end

local pairs_total, hit_sum = 0, 0
for _ = 1, FRAMES do
  pairs_total = pairs_total + native_run(m_dn, "move_and_separate", N, x, z, vx, vz, alive, out, counts)
  for e = 1, N do
    for k = 1, counts[e] do
      hit_sum = hit_sum + out[(e - 1) * N + k]
    end
  end
end

local ox = {{1.5, 3.0, 4.5}}
local oz = {{1.5, 3.0, 4.5}}
local r  = {{0.5, 0.5, 0.5}}
print(pairs_total)
print(hit_sum)
print(tostring(native_run(m_dn, "cam_blocked", 1.6, 1.6, 3, ox, oz, r)))
print(tostring(native_run(m_dn, "cam_blocked", 0.2, 0.2, 3, ox, oz, r)))
print(math.floor(x[1] * 64 + 0.5))
print(math.floor(z[1] * 64 + 0.5))
print(math.floor(x[17] * 64 + 0.5))
print(math.floor(z[17] * 64 + 0.5))
""")

s9_luahost_out = subprocess.check_output([native_luahost_bin, s9_luahost_script], text=True).strip().splitlines()
expected_s9 = lua_sum_out + lua_feat_out + lua_s6_out + lua_s7_out + lua_s8_out
if s9_luahost_out != expected_s9:
    for idx, (got, exp) in enumerate(zip(s9_luahost_out, expected_s9)):
        if got != exp:
            fail(f"Stage 9 native_compile output mismatch at line #{idx + 1}: got={got}, expected={exp}")
            break
    else:
        fail(f"Stage 9 native_compile line count mismatch: {len(s9_luahost_out)} vs {len(expected_s9)}")

# 11b. Bare-metal ARM1176 VFPv2 QEMU: compile .b source strings directly on the simulated Pi CPU via bml_compile_native()
if arm_gcc and qemu_arm:
    def c_str_bytes(s: str) -> str:
        b = s.encode("utf-8")
        return ", ".join(f"0x{x:02X}u" for x in b) + ", 0x00u"

    QEMU_S9_C = f"""\
#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>
#include "bm/bmlc.h"
#include "bm/bmnative.h"

#define UART0 ((volatile uint32_t *)0x101F1000u)

static void put_ch(char c) {{ *UART0 = (uint32_t)(uint8_t)c; }}
static void put_str(const char *s) {{ while (*s) put_ch(*s++); }}
static void put_bool(int32_t v) {{ put_str(v ? "true\\n" : "false\\n"); }}

static void put_i32(int32_t v)
{{
    char buf[16];
    uint32_t u = (v < 0) ? (0u - (uint32_t)v) : (uint32_t)v;
    if (v < 0) put_ch('-');
    int i = 0;
    if (u == 0) buf[i++] = '0';
    while (u > 0) {{
        buf[i++] = (char)('0' + (u % 10u));
        u /= 10u;
    }}
    while (i > 0) put_ch(buf[--i]);
    put_ch('\\n');
}}

static uint32_t f2u(float f)
{{
    union {{ float f; uint32_t u; }} u;
    u.f = f;
    return u.u;
}}

static float u2f(uint32_t bits)
{{
    union {{ uint32_t u; float f; }} u;
    u.u = bits;
    return u.f;
}}

/* Minimal freestanding heap, string, and setjmp/longjmp for bmlc.c + bmnative.c + cache.c */
static uint8_t heap_buf[2048 * 1024] __attribute__((aligned(32)));
static size_t heap_off = 0;

void *calloc(size_t nmemb, size_t size)
{{
    size_t total = (nmemb * size + 31u) & ~31u;
    if (heap_off + total > sizeof(heap_buf)) return NULL;
    uint8_t *p = &heap_buf[heap_off];
    heap_off += total;
    for (size_t i = 0; i < total; i++) p[i] = 0;
    return p;
}}

void free(void *p) {{ (void)p; }}

void *memcpy(void *dst, const void *src, size_t n)
{{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}}

void *memset(void *dst, int c, size_t n)
{{
    uint8_t *d = (uint8_t *)dst;
    for (size_t i = 0; i < n; i++) d[i] = (uint8_t)c;
    return dst;
}}

int memcmp(const void *a, const void *b, size_t n)
{{
    const uint8_t *pa = (const uint8_t *)a;
    const uint8_t *pb = (const uint8_t *)b;
    for (size_t i = 0; i < n; i++) {{
        if (pa[i] != pb[i]) return (int)pa[i] - (int)pb[i];
    }}
    return 0;
}}

size_t strlen(const char *s)
{{
    size_t n = 0;
    while (s[n]) n++;
    return n;
}}

int strcmp(const char *a, const char *b)
{{
    while (*a && *a == *b) {{ a++; b++; }}
    return (uint8_t)*a - (uint8_t)*b;
}}

__attribute__((naked))
int setjmp(jmp_buf env)
{{
    __asm__ volatile (
        "stmia r0, {{r4-r11, sp, lr}}\\n"
        "mov r0, #0\\n"
        "bx lr\\n"
    );
}}

__attribute__((naked))
void longjmp(jmp_buf env, int val)
{{
    __asm__ volatile (
        "ldmia r0, {{r4-r11, sp, lr}}\\n"
        "movs r0, r1\\n"
        "moveq r0, #1\\n"
        "bx lr\\n"
    );
}}

static const uint8_t src_sum[] = {{ {c_str_bytes(sum_src)} }};
static const uint8_t src_dn[]  = {{ {c_str_bytes(dn_src)} }};

static bmnative_mod *compile_and_load_on_pi(const uint8_t *src, size_t len)
{{
    uint8_t *blob = NULL;
    size_t blob_len = 0;
    char err[128];
    if (bml_compile_native((const char *)src, len, &blob, &blob_len, err, sizeof err) != 0) {{
        put_str("COMPILE_ERR: ");
        put_str(err);
        put_ch('\\n');
        return NULL;
    }}
    const char *why = "";
    bmnative_mod *m = NULL;
    if (bmnative_load(&m, blob, blob_len, &why) != BMN_OK) {{
        put_str("LOAD_ERR: ");
        put_str(why);
        put_ch('\\n');
        return NULL;
    }}
    return m;
}}

void c_main(void)
{{
    /* 1. Verify negative error reporting on-console */
    {{
        const char *bad = "function f(x: i32): i32\\n  return true\\nend\\n";
        uint8_t *blob = NULL;
        size_t blob_len = 0;
        char err[128];
        if (bml_compile_native(bad, strlen(bad), &blob, &blob_len, err, sizeof err) == 0) {{
            put_str("BAD_ACCEPTED\\n");
            return;
        }}
    }}

    /* 2. Compile sum.b on the ARM1176 CPU and run sum(10) = 55 */
    bmnative_mod *m_sum = compile_and_load_on_pi(src_sum, sizeof(src_sum) - 1u);
    if (!m_sum) return;
    {{
        int32_t a10[1] = {{10}}, r = 0;
        bmnative_call(m_sum, 0, a10, 1, &r);
        put_i32(r);
    }}

    /* 3. Compile darknight_enemy.b on the ARM1176 CPU and run 60 frames of 17 enemies */
    bmnative_mod *m = compile_and_load_on_pi(src_dn, sizeof(src_dn) - 1u);
    if (!m) return;

    const float init_x[{DN_N}]  = {{ {c_init_x} }};
    const float init_z[{DN_N}]  = {{ {c_init_z} }};
    const float init_vx[{DN_N}] = {{ {c_init_vx} }};
    const float init_vz[{DN_N}] = {{ {c_init_vz} }};
    uint32_t x[{DN_N}], z[{DN_N}], vx[{DN_N}], vz[{DN_N}];
    uint32_t alive[{DN_N}] = {{ {c_init_alive} }};
    int32_t out[{DN_N * DN_N}];
    int32_t counts[{DN_N}];

    for (int i = 0; i < {DN_N}; i++) {{
        x[i] = f2u(init_x[i]);
        z[i] = f2u(init_z[i]);
        vx[i] = f2u(init_vx[i]);
        vz[i] = f2u(init_vz[i]);
        counts[i] = 0;
    }}
    for (int i = 0; i < {DN_N * DN_N}; i++) out[i] = 0;

    int fn_move = bmnative_find(m, "move_and_separate");
    int fn_cam  = bmnative_find(m, "cam_blocked");
    int32_t pairs_total = 0, hit_sum = 0;
    int32_t a[8] = {{
        {DN_N},
        (int32_t)(uintptr_t)x,
        (int32_t)(uintptr_t)z,
        (int32_t)(uintptr_t)vx,
        (int32_t)(uintptr_t)vz,
        (int32_t)(uintptr_t)alive,
        (int32_t)(uintptr_t)out,
        (int32_t)(uintptr_t)counts
    }};
    for (int f = 0; f < {DN_FRAMES}; f++) {{
        int32_t r = 0;
        bmnative_call(m, fn_move, a, 8, &r);
        pairs_total += r;
        for (int e = 0; e < {DN_N}; e++) {{
            for (int k = 0; k < counts[e]; k++)
                hit_sum += out[e * {DN_N} + k];
        }}
    }}

    uint32_t ox[3] = {{ f2u(1.5f), f2u(3.0f), f2u(4.5f) }};
    uint32_t oz[3] = {{ f2u(1.5f), f2u(3.0f), f2u(4.5f) }};
    uint32_t rad[3] = {{ f2u(0.5f), f2u(0.5f), f2u(0.5f) }};
    int32_t cam1 = 0, cam2 = 0;
    {{ int32_t ca[6] = {{ (int32_t)f2u(1.6f), (int32_t)f2u(1.6f), 3, (int32_t)(uintptr_t)ox, (int32_t)(uintptr_t)oz, (int32_t)(uintptr_t)rad }}; bmnative_call(m, fn_cam, ca, 6, &cam1); }}
    {{ int32_t ca[6] = {{ (int32_t)f2u(0.2f), (int32_t)f2u(0.2f), 3, (int32_t)(uintptr_t)ox, (int32_t)(uintptr_t)oz, (int32_t)(uintptr_t)rad }}; bmnative_call(m, fn_cam, ca, 6, &cam2); }}

    put_i32(pairs_total);
    put_i32(hit_sum);
    put_bool(cam1);
    put_bool(cam2);
    put_i32((int32_t)(u2f(x[0]) * 64.0f + 0.5f));
    put_i32((int32_t)(u2f(z[0]) * 64.0f + 0.5f));
    put_i32((int32_t)(u2f(x[16]) * 64.0f + 0.5f));
    put_i32((int32_t)(u2f(z[16]) * 64.0f + 0.5f));
}}

__attribute__((naked, section(".text.boot")))
void _start(void)
{{
    __asm__ volatile (
        "ldr sp, =0x04000000\\n"
        "mrc p15, 0, r0, c1, c0, 2\\n"
        "orr r0, r0, #0x00F00000\\n"
        "mcr p15, 0, r0, c1, c0, 2\\n"
        "mcr p15, 0, r0, c7, c5, 4\\n"
        "mov r0, #0x40000000\\n"
        ".word 0xEEE80A10\\n"
        "bl c_main\\n"
        "mov r0, #0x18\\n"
        "ldr r1, =0x20026\\n"
        "svc 0x00123456\\n"
        "1: b 1b\\n"
    );
}}
"""
    qemu_s9_c = os.path.join(BUILD, "armv6_stage9.c")
    qemu_s9_elf = os.path.join(BUILD, "armv6_stage9.elf")
    with open(qemu_s9_c, "w", encoding="utf-8") as f:
        f.write(QEMU_S9_C)

    subprocess.run(
        [
            arm_gcc, "-O2", "-mcpu=arm1176jzf-s", "-marm", "-mfpu=vfpv2", "-mfloat-abi=softfp",
            "-ffreestanding", "-nostdlib",
            f"-I{os.path.join(ROOT, 'src')}",
            f"-I{os.path.join(ROOT, 'src', 'arch')}",
            "-T", qemu_ld, "-o", qemu_s9_elf,
            qemu_s9_c,
            os.path.join(ROOT, "src", "bm", "bmlc.c"),
            os.path.join(ROOT, "src", "bm", "bmnative.c"),
            os.path.join(ROOT, "src", "arch", "cache.c"),
            "-lgcc",
        ],
        check=True,
    )
    qemu_s9_out = subprocess.check_output(
        [qemu_arm, "-M", "versatilepb", "-cpu", "arm1176", "-nographic", "-semihosting", "-kernel", qemu_s9_elf],
        text=True,
        stderr=subprocess.DEVNULL,
        timeout=30,
    ).replace("\r\n", "\n").strip().splitlines()

    expected_s9_qemu = ["55"] + lua_s8_out
    if qemu_s9_out != expected_s9_qemu:
        fail(f"Stage 9 ARM1176 on-console bml_compile_native QEMU output differs: {qemu_s9_out} vs {expected_s9_qemu}")

if failed:
    sys.exit(1)

print(
    f"bml: stages 1-9 passed ({len(BAD_PROGRAMS)} error checks, sum(10)=55, "
    f"{len(feat_calls)} feature calls, 200 random programs [1000 calls], Stage 6 types_mem.b, Stage 7 kernel_fn.b, Stage 8 darknight_enemy.b [60 frames, 17 enemies], and Stage 9 on-console C compiler native_compile matched Lua across StackVM, C bmvm, native_run, and ARM1176 VFPv2 QEMU)"
)
