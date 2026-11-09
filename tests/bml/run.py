#!/usr/bin/env python3
"""make test-bml: verifies stages 1 and 2 of the language of bm (tools/bml/bml.py).

  run.py LUAHOST BUILD_DIR

1. Stage 1 (Front end): lexer, parser, AST, and type checker reject invalid programs and name the exact line.
2. Stage 1 & 2 (sum): tests/bml/sum.bml compiles to stack bytecode, sum(10) == 55, and matches Lua on -5..100.
3. ARMv6 preview check: verifies the 9 hand-encoded A32 words of sum in docs/progress/typed.md via objdump and simulation.
4. Stage 2 (features): tests/bml/features.bml matches Lua across all language constructs and recursion.
5. Stage 2 (differential fuzzing): 200 random well-typed BML programs run in StackVM and in Lua with identical results.
"""
import os
import random
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools", "bml"))
import bml  # noqa: E402

LUAHOST = sys.argv[1]
BUILD = sys.argv[2]
os.makedirs(BUILD, exist_ok=True)

LUA_PRELUDE = """\
local abs, min, max = math.abs, math.min, math.max
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
    ("floating-point literal in stage 1",
     "function f(n: i32): i32\n  local x = 1.5\n  return x\nend\n", "line 2"),
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
    ("float division '/' in stage 1",
     "function f(a: i32, b: i32): i32\n  return a / b\nend\n", "line 2"),
    ("call to undefined function",
     "function f(a: i32): i32\n  return unknown_fn(a)\nend\n", "line 2"),
    ("call with wrong argument count",
     "function g(x: i32): i32\n  return x\nend\nfunction f(a: i32): i32\n  return g(a, 2)\nend\n", "line 5"),
    ("call with wrong argument type",
     "function g(x: i32): i32\n  return x\nend\nfunction f(a: i32): i32\n  return g(a > 0)\nend\n", "line 5"),
]

for desc, bad_src, expected_line in BAD_PROGRAMS:
    try:
        bml.parse_and_check(bad_src)
        fail(f"accepted invalid program ({desc})")
    except bml.Error as e:
        if expected_line not in str(e):
            fail(f"wrong line for '{desc}': expected '{expected_line}', got '{e}'")

# ---------------------------------------------------------------- 2. Stage 1 & 2: sum(10) == 55 and Lua comparison
sum_path = os.path.join(HERE, "sum.bml")
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

# ---------------------------------------------------------------- 3. ARMv6 hand-encoded words of sum verification
for n in range(-5, 101):
    arm_val = bml.run_armv6_words(bml.SUM_ARMV6_WORDS, n)
    vm_val = sum_vm.call("sum", [n])
    if arm_val != vm_val:
        fail(f"ARMv6 sum({n}) = {arm_val}, expected {vm_val}")
        break

objdump = shutil.which("arm-none-eabi-objdump")
if objdump:
    import struct
    bin_path = os.path.join(BUILD, "sum_armv6.bin")
    with open(bin_path, "wb") as f:
        f.write(struct.pack("<9I", *bml.SUM_ARMV6_WORDS))
    dis = subprocess.check_output([objdump, "-D", "-b", "binary", "-m", "arm", bin_path], text=True)
    for expected_insn in ("cmp\tr2, r0", "bgt\t0x1c", "add\tr1, r1, r2", "b\t0x8", "bx\tlr"):
        if expected_insn not in dis:
            fail(f"ARMv6 objdump missing '{expected_insn}':\n{dis}")

# ---------------------------------------------------------------- 4. Stage 2: features.bml against Lua
feat_path = os.path.join(HERE, "features.bml")
with open(feat_path, encoding="utf-8") as f:
    feat_src = f.read()

feat_mod = bml.compile_source(feat_src)
feat_vm = bml.StackVM(feat_mod)

feat_calls = []
for x in range(-25, 26):
    feat_calls.append(("is_even", [x]))
    feat_calls.append(("clamp", [x * 7, -50, 50]))
    feat_calls.append(("short_circuit_ok", [x]))
    feat_calls.append(("countdown_sum", [x]))
for a in (-48, -17, 0, 12, 35, 105, 1071):
    for b in (-36, -1, 0, 6, 14, 462):
        feat_calls.append(("gcd", [a, b]))
        feat_calls.append(("arith_mix", [a, b]))
for n in range(0, 13):
    feat_calls.append(("fib", [n]))
for n in range(0, 40):
    feat_calls.append(("collatz_steps", [n]))
for lim in (0, 1, 2, 10, 30, 60):
    feat_calls.append(("count_primes", [lim]))

feat_lua_lines = [LUA_PRELUDE, bml.to_lua(feat_src)]
vm_feat_out = []
for fn_name, args in feat_calls:
    res = feat_vm.call(fn_name, args)
    vm_feat_out.append("true" if res is True else ("false" if res is False else str(res)))
    arg_str = ", ".join(str(a) for a in args)
    feat_lua_lines.append(f"print(tostring({fn_name}({arg_str})))")

feat_lua_path = os.path.join(BUILD, "features_test.lua")
with open(feat_lua_path, "w", encoding="utf-8") as f:
    f.write("\n".join(feat_lua_lines) + "\n")

lua_feat_out = subprocess.check_output([LUAHOST, feat_lua_path], text=True).strip().splitlines()
if lua_feat_out != vm_feat_out:
    for idx, ((fn_name, args), lv, vv) in enumerate(zip(feat_calls, lua_feat_out, vm_feat_out)):
        if lv != vv:
            fail(f"features mismatch at #{idx} {fn_name}({args}): Lua={lv}, VM={vv}")
            break

# ---------------------------------------------------------------- 5. Stage 2: 200 random programs against Lua
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
            # Keep products bounded so 64-bit Lua and 32-bit ints never overflow
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

if failed:
    sys.exit(1)

print(
    f"bml: {len(BAD_PROGRAMS)} error checks, sum(10)=55, ARMv6 words of sum, "
    f"{len(feat_calls)} feature calls, and 200 random programs ({len(vm_rand_out)} calls) matched Lua"
)
