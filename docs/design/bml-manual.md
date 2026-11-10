# BML Language & Compiler Manual (`tools/bml/`)

This document is the technical reference and manual for **BML** (*BareMetal Language*), the compiler and virtual machine in [`tools/bml/bml.py`](../../tools/bml/bml.py).

For project state and rules, see [`docs/progress/typed.md`](../progress/typed.md). For the earlier C-generating prototype (`scripts/bmt.py`) and benchmark history, see [`docs/design/typed-native.md`](typed-native.md).

---

## 1. Purpose and Design Principles

BML is a statically typed subset of Lua 5.4 designed for `bm` cartridges on the Raspberry Pi Zero W (BCM2835, ARM1176JZF-S, ARMv6 A32):

1. **Single-Source Dual Execution**: Every valid `.b` file is also valid Lua 5.4 once its `: <type>` annotations are stripped (`bml.py --lua`). The standard Lua VM is the ground-truth oracle for every test stage.
2. **Explicit Multi-Pass Architecture**: Unlike `scripts/bmt.py` (which emits C text directly during single-pass parsing), `tools/bml/bml.py` separates the compiler into distinct, inspectable stages:
   - **Stage 1 (Front End)**: `Lexer` $\to$ `Parser` $\to$ `AST` $\to$ `TypeChecker` (+ Lua stripper `to_lua`).
   - **Stage 2 (Host Bytecode & VM)**: `BytecodeCompiler` $\to$ `Module` $\to$ `StackVM` (with `--dump` disassembler).
   - **Stage 3 (Kernel VM Back End)**: `BMVMCompiler` (`--bmvm <out.bmv>`), lowering the checked stack IR into a verified `BMV1` register-machine module executed by [`src/bm/bmvm.c`](../../src/bm/bmvm.c) (`vm_load`, `vm_run`).
   - **Stage 4 (ARMv6 A32 Native Back End)**: `ARMv6Assembler` and `ARMv6Compiler` (`--armv6 <out.bin>`, `--dump-armv6`), lowering BML functions directly to 32-bit ARMv6 (A32, AAPCS) machine words and verified on an emulated ARM1176JZF-S CPU via `qemu-system-arm -M versatilepb -cpu arm1176`.
   - **Stage 5 (Native Pi Loader & Cache Maintenance)**: `compile_native_blob` (`--native <out.bmn>`), packing the verified `BMN1` container loaded by [`src/bm/bmnative.c`](../../src/bm/bmnative.c) and [`src/bm/api_vm.c`](../../src/bm/api_vm.c) (`native_load`, `native_fn`, `native_run`) into 32-byte-aligned executable RAM with CP15 D-cache clean and I-cache invalidate.
   - **Stage 6 (Types & Memory)**: 32-bit two's complement wraparound for `i32`, 1-based arrays (`i32[]`, `bool[]`, `f32[]`), `struct Name ... end` (`s.field`), and single-precision `f32` floats lowered to hardware VFPv2 coprocessor 10 instructions (`vadd.f32`, `vsub.f32`, `vmul.f32`, `vdiv.f32`, `vsqrt.f32`, `vcmp.f32` + `vmrs APSR_nzcv, fpscr`, `vcvt.f32.s32`, `vcvt.s32.f32`).
   - **Stage 7 (Kernel Function Table)**: Direct calls from `.b` to `bm` kernel functions (`cls`, `pix`, `rectfill`, `spr`, `btn`, `btnp`, `line`, `circ`, `circfill`, `rect`, `sfx`, `music`, `time`, plus `abs`/`min`/`max`/`sqrt`/`floor`/`f32`) via `CALL_HOST` and the 6-word ARMv6 stub `__bml_call_host`.
   - **Stage 8 (Games — Dark Night)**: Full enemy movement, wall bounce, camera collision (`cam_blocked`), and $O(N^2)$ pairwise separation (`move_and_separate`) in [`tests/bml/darknight_enemy.b`](../../tests/bml/darknight_enemy.b).
   - **Stage 9 (On-Console Compiler in C & Advanced Features)**: Self-contained C compiler in [`src/bm/bmlc.c`](../../src/bm/bmlc.c) (`bml_compile_native`) exposed to Lua as `native_compile(src)`, compiling `.b` source text directly into `BMN1` ARMv6 + VFPv2 machine code on the Pi Zero W without Python. Supports zero-cost `const`, sequential `enum`, struct methods (`Struct:method`) with implicit `self`, and full kernel APIs.
3. **Line-Accurate Diagnostics**: Every lexical, syntactic, or type error reports the 1-based line number of the offending token or statement (`line <N>: <message>`).

---

## 2. Command-Line Usage

```bash
# 1. Check syntax and types only (Stage 1)
python3 tools/bml/bml.py tests/bml/sum.b --check

# 2. Strip type annotations to produce plain Lua
python3 tools/bml/bml.py tests/bml/sum.b --lua build/sum.lua

# 3. Disassemble the compiled Stage 2 stack bytecode
python3 tools/bml/bml.py tests/bml/sum.b --dump

# 4. Compile and run a function in the Stage 2 StackVM on the host
python3 tools/bml/bml.py tests/bml/sum.b --run sum 10
# -> 55

# 5. Emit a Stage 3 BMV1 binary module for the kernel's bmvm (src/bm/bmvm.c)
python3 tools/bml/bml.py tests/bml/features.b --bmvm build/features.bmv

# 6. Emit and inspect Stage 4 ARMv6 (A32) 32-bit machine words
python3 tools/bml/bml.py tests/bml/sum.b --armv6 build/sum.bin --dump-armv6

# 7. Run the full verification suite (Stages 1, 2, 3, and 4 on QEMU ARM1176 vs Lua)
make test-bml
```

---

## 3. Language Reference (Stages 1 & 2)

### 3.1 Types

Stage 1 supports two scalar value types:

| Type | Description | Literals | Default / Representation |
|---|---|---|---|
| `i32` | Signed 32-bit integer ($-2^{31}$ to $2^{31}-1$) | `0`, `42`, `2147483647`, `-2147483648` | Python `int` in host VM; `int32_t` / 32-bit GP register on ARMv6 |
| `bool` | Truth value (strict; integers are never truthy) | `true`, `false` | `True` / `False` (`0` / `1` on ARMv6) |

Floating-point literals (e.g. `1.5`) and `/` (float division) are explicitly rejected in Stage 1 with a line-numbered error pointing the author to `//` (floor division).

### 3.2 Lexical Rules

- **Comments**: `--` until the end of the line (identical to Lua).
- **Identifiers**: `[A-Za-z_][A-Za-z0-9_]*`, excluding reserved keywords.
- **Keywords**:
  `function`, `local`, `for`, `do`, `while`, `end`, `if`, `then`, `elseif`, `else`, `return`, `break`, `and`, `or`, `not`, `true`, `false`.

### 3.3 Grammar (EBNF)

```ebnf
program     = { func_decl } ;
func_decl   = "function" NAME "(" [ param_list ] ")" [ ":" TYPE ] block "end" ;
param_list  = param { "," param } ;
param       = NAME ":" TYPE ;
TYPE        = "i32" | "bool" ;

block       = { stmt } ;
stmt        = "local" NAME [ ":" TYPE ] "=" expr
            | NAME "=" expr
            | NAME "(" [ arg_list ] ")"
            | "if" expr "then" block { "elseif" expr "then" block } [ "else" block ] "end"
            | "while" expr "do" block "end"
            | "for" NAME "=" expr "," expr [ "," expr ] "do" block "end"
            | "return" [ expr ]
            | "break" ;

arg_list    = expr { "," expr } ;

expr        = or_expr ;
or_expr     = and_expr { "or" and_expr } ;
and_expr    = cmp_expr { "and" cmp_expr } ;
cmp_expr    = bor_expr [ ( "<" | "<=" | ">" | ">=" | "==" | "~=" ) bor_expr ] ;
bor_expr    = bxor_expr { "|" bxor_expr } ;
bxor_expr   = band_expr { "~" band_expr } ;
band_expr   = shift_expr { "&" shift_expr } ;
shift_expr  = add_expr { ( "<<" | ">>" ) add_expr } ;
add_expr    = mul_expr { ( "+" | "-" ) mul_expr } ;
mul_expr    = unary_expr { ( "*" | "//" | "%" ) unary_expr } ;
unary_expr  = ( "-" | "not" | "~" ) unary_expr | primary ;
primary     = INT | "true" | "false" | "(" expr ")" | NAME "(" [ arg_list ] ")" | NAME ;
```

### 3.4 Operators and Precedence (Lowest to Highest)

| Level | Operators | Operand Types | Result Type | Semantics |
|---|---|---|---|---|
| 1 | `or` | `bool, bool` | `bool` | Short-circuit logical OR (right operand not evaluated if left is `true`) |
| 2 | `and` | `bool, bool` | `bool` | Short-circuit logical AND (right operand not evaluated if left is `false`) |
| 3 | `<`, `<=`, `>`, `>=` | `i32, i32` | `bool` | Signed integer order comparison |
| 3 | `==`, `~=` | `T, T` (`i32` or `bool`) | `bool` | Equality / inequality of identical types |
| 4 | `\|` | `i32, i32` | `i32` | Bitwise OR |
| 5 | `~` (binary) | `i32, i32` | `i32` | Bitwise XOR |
| 6 | `&` | `i32, i32` | `i32` | Bitwise AND |
| 7 | `<<`, `>>` | `i32, i32` | `i32` | Logical left shift / arithmetic right shift (by $b \mathbin{\&} 31$) |
| 8 | `+`, `-` | `i32, i32` | `i32` | Integer addition and subtraction |
| 9 | `*`, `//`, `%` | `i32, i32` | `i32` | Multiplication, floor division toward $-\infty$, floor modulo ($a - (a // b) \times b$) |
| 10 | `-`, `not`, `~` (unary) | `i32` / `bool` | `i32` / `bool` | Arithmetic negation / logical negation / bitwise NOT |

### 3.4.1 Compound Assignment Operators

The `.b` language supports in-place compound assignments for scalar variables, struct fields (`s.field`), and array elements (`arr[idx]`):

| Operator | Operands | Desugared Form | Notes |
|---|---|---|---|
| `+=` | `i32` or `f32` | `target = target + (expr)` | In-place addition |
| `-=` | `i32` or `f32` | `target = target - (expr)` | In-place subtraction |
| `*=` | `i32` or `f32` | `target = target * (expr)` | In-place multiplication |
| `&=` | `i32` | `target = target & (expr)` | Bitwise AND |
| `\|=` | `i32` | `target = target \| (expr)` | Bitwise OR |
| `<<=` | `i32` | `target = target << (expr)` | Bitwise shift left |
| `>>=` | `i32` | `target = target >> (expr)` | Bitwise arithmetic shift right |

Compound assignments are desugared cleanly during Lua generation (`to_lua`), and compiled single-pass directly in the native C compiler `bmlc.c`.

### 3.4.2 Typed Pointers and Memory Views

The `.b` language supports zero-overhead typed pointers and memory views for bare-metal programming, allowing direct structured access to hardware MMIO registers, DMA buffers, framebuffers, and packet records:

- **Struct pointer / memory view**: `local s: StructName = StructName(addr)` or `local s: StructName = addr`. Field reads and writes (`s.field = val`) translate directly to single ARMv6 `ldr`/`str` instructions at `addr + offset` with zero allocation overhead.
- **Array pointer / memory view**: `local arr: i32[] = addr` or `local farr: f32[] = addr`. Element accesses (`arr[i] = val`) compile directly to `*(addr + (i - 1) * 4)`.
- **Lua compatibility**: In plain Lua (`to_lua`), struct constructors and `__bml_arr` proxy tables map field and index accesses transparently to `peek4`/`poke4` when passed an integer memory address.

### 3.4.3 Switch Statements and Enum Matching

The `.b` language supports structured multi-way branching via `switch / case / default`:

```lua
switch expr do
case val1 then
    -- statements
case val2 then
    -- statements
default
    -- fallback statements
end
```

- **Semantics**: Evaluates `expr` exactly once and jumps to the matching `case` block without accidental fall-through. Both `default` and `else` are accepted for the fallback clause.
- **ARMv6 Native**: Compiles single-pass in `bmlc.c` into efficient conditional branches (`CMP` + `BNE` + `B exit`) without jump tables overhead for small-to-medium branches.
- **Lua Desugaring**: In `to_lua`, cleanly desugars into `local __sw = (expr); if false then elseif __sw == (val1) then ... else ... end`, preserving line numbers and error tracking.

### 3.4.4 Local Type Inference

Variables declared with `local` can omit explicit type annotations (`local x = expr`):
- The static type is automatically deduced from the initialization expression:
  - Integer literal (e.g. `local i = 10`): deduced as `i32`
  - Floating-point literal (e.g. `local f = 1.5`): deduced as `f32`
  - Boolean literal (e.g. `local ok = true`): deduced as `bool`
  - String literal (e.g. `local s = "hello"`): deduced as `string`
  - Struct constructor (e.g. `local p = Packet(addr)`): deduced as `Packet`
  - Expression or function result: deduced from the return type of `expr`
- Type checking remains strictly static: subsequent assignments to the variable must match the inferred type.

### 3.5 Built-in Functions

- Math & Conversion:
  - `abs(x: i32): i32` or `abs(x: f32): f32`
  - `min(a: i32, b: i32): i32` or `min(a: f32, b: f32): f32`
  - `max(a: i32, b: i32): i32` or `max(a: f32, b: f32): f32`
  - `sqrt(x: f32): f32` (hardware VFPv2 single-precision `vsqrt.f32`)
  - `f32(x: i32): f32` and `floor(x: f32): i32`
- Bare-Metal Memory Access & MMIO:
  - `peek(addr: i32): i32` (unsigned byte load, ARMv6 `ldrb`)
  - `poke(addr: i32, val: i32)` (byte store, ARMv6 `strb`)
  - `peek2(addr: i32): i32` (unsigned halfword load, ARMv6 `ldrh`)
  - `poke2(addr: i32, val: i32)` (halfword store, ARMv6 `strh`)
  - `peek4(addr: i32): i32` (32-bit word load, ARMv6 `ldr`)
  - `poke4(addr: i32, val: i32)` (32-bit word store, ARMv6 `str`)
  - `memcpy(dst: i32, src: i32, bytes: i32)` (fast native byte copy routine)
  - `memset(dst: i32, val: i32, bytes: i32)` (fast native byte fill routine)
- Kernel Host API (Tappa 7+):
  - `cls(c)`, `pix(x, y, c)`, `rectfill(x, y, w, h, c)`, `spr(id, x, y)`, `btn(b): bool`, `btnp(b): bool`, `line(...)`, `circ(...)`, `circfill(...)`, `rect(...)`, `sfx(id)`, `music(id)`, `time(): f32`, `print(msg: string, ...): i32`

---

## 4. Stage 1 Internals: Lexer, Parser, AST, and Type Checker

### 4.1 Lexer (`tokenize`)

`tokenize(src)` scans the source with `TOKEN_RE`, tracks 1-based line numbers across newlines, skips whitespace and `--` comments, classifies identifiers against `KEYWORDS`, and appends an explicit `eof` token. Every `Token` records `(kind, text, line, start, end)`.

### 4.2 Parser and Lua Stripping (`Parser`, `to_lua`)

`Parser` is a recursive-descent parser that produces a `Program` tree while recording byte spans `(start, end)` in `self.cuts` whenever `parse_type_annotation()` consumes `: i32` or `: bool` (on function parameters, function return types, or optional `local x: i32 = ...` declarations).

`to_lua(src)` runs `parse_and_check(src)` first—guaranteeing that ill-typed sources are never emitted—and then slices out `prog.cuts` in reverse order. Because only `: <type>` spans are removed, line numbers in the stripped Lua file match the `.b` source 1-to-1.

### 4.3 AST Nodes

- **Program structure**: `Program(funcs, cuts)`, `FuncDecl(name, params, ret_type, body, line)`, `Param(name, typ, line)`.
- **Statements (`Stmt`)**: `LocalStmt`, `AssignStmt`, `IfStmt` (with `IfBranch` list and optional `else_body`), `WhileStmt`, `ForStmt`, `ReturnStmt`, `BreakStmt`, `CallStmt`.
- **Expressions (`Expr`)**: `IntLit`, `BoolLit`, `VarExpr`, `UnaryOp`, `BinOp`, `CallExpr`. Every `Expr` carries `.line` and has `.typ` (`"i32"` or `"bool"`) populated during type checking.

### 4.4 Static Type Checking Rules (`TypeChecker`)

`TypeChecker.check_program(prog)` enforces:
1. **No implicit truthiness**: `if`, `elseif`, `while`, `and`, `or`, and `not` require `bool`. Passing an `i32` raises `line N: the test of an if/elseif is bool, not i32`.
2. **Lexical block scoping & no shadowing**: Variables declared with `local` or `for` live only until the end of their enclosing block, and redeclaring a name that is already active in the outer scope of the same function is rejected (`line N: 'x' is declared twice`).
3. **Function signatures and recursion**: A function's signature `(param_types, ret_type)` is registered before its body is checked, enabling direct recursion (`fib`, `gcd`) as well as calls to any previously defined function.
4. **Control-flow return completeness**: Every function with a return type (`: i32` or `: bool`) must return a value of that type on all control-flow paths (`block_always_returns`).
5. **Loop-scoped `break`**: `BreakStmt` is only valid when `loop_depth > 0` (inside `while` or `for`).

---

## 5. Stage 2 Internals: Stack Bytecode and Virtual Machine

### 5.1 Frame and Stack Model

Each function activation in `StackVM` owns:
- **`locals_arr`**: Fixed-size array of `fn.nlocals` slots. Slots `0 .. nparams - 1` hold the incoming arguments; higher slots hold `local` variables and numeric `for` loop state (`i`, `i$stop`, `i$step`).
- **`stack`**: Evaluation stack holding intermediate `int` and `bool` values.
- **`pc`**: Program counter indexing `fn.code`.

### 5.2 Stack Bytecode Instruction Set (`OPCODES`)

| Opcode | Argument | Stack Effect (before $\to$ after) | Semantics |
|---|---|---|---|
| `PUSH_CONST` | `k` | $\to K[k]$ | Pushes integer constant `consts[k]` |
| `PUSH_TRUE` | — | $\to \text{true}$ | Pushes boolean `True` |
| `PUSH_FALSE` | — | $\to \text{false}$ | Pushes boolean `False` |
| `POP` | — | $v \to$ | Discards top of stack |
| `LOAD` | `slot` | $\to \text{locals}[\text{slot}]$ | Pushes local or parameter slot |
| `STORE` | `slot` | $v \to$ | Pops top of stack into `locals[slot]` |
| `ADD`, `SUB`, `MUL` | — | $a, b \to (a \circ b)$ | Integer arithmetic |
| `IDIV`, `IMOD` | — | $a, b \to (a \circ b)$ | Floor division `//` and modulo `%` toward $-\infty$ |
| `BAND`, `BOR`, `BXOR` | — | $a, b \to (a \circ b)$ | Bitwise AND (`&`), OR (`\|`), XOR (`~`) |
| `BNOT` | — | $a \to (\sim a)$ | Bitwise NOT |
| `SHL`, `SHR` | — | $a, b \to (a \circ (b \mathbin{\&} 31))$ | Bitwise left shift `<<` / arithmetic right shift `>>` |
| `NEG` | — | $a \to (-a)$ | Integer negation |
| `NOT` | — | $a \to (\neg a)$ | Boolean negation |
| `EQ`, `NE` | — | $a, b \to (a \circ b)$ | Equality / inequality (`bool`) |
| `LT`, `LE`, `GT`, `GE` | — | $a, b \to (a \circ b)$ | Signed integer comparison (`bool`) |
| `ABS` | — | $a \to |a|$ | Built-in `abs(a)` |
| `MIN`, `MAX` | — | $a, b \to \min/\max(a,b)$ | Built-in `min(a, b)` / `max(a, b)` |
| `PEEK`, `PEEK2`, `PEEK4` | — | $\text{addr} \to v$ | Raw memory load: `PEEK` (byte `ldrb`), `PEEK2` (halfword `ldrh`), `PEEK4` (word `ldr`) |
| `POKE`, `POKE2`, `POKE4` | — | $\text{addr}, v \to$ | Raw memory store: `POKE` (byte `strb`), `POKE2` (halfword `strh`), `POKE4` (word `str`) |
| `MEMCPY` | — | $\text{dst}, \text{src}, n \to$ | Memory block copy |
| `MEMSET` | — | $\text{dst}, v, n \to$ | Memory block fill |
| `JMP` | `target` | — | Unconditional jump (`pc = target`) |
| `JZ` | `target` | $\text{cond} \to$ | Pops `cond`; if `False`, sets `pc = target` |
| `JF_KEEP` | `target` | $b \to b \text{ (jump)} \mid \text{ (fall)}$ | Short-circuit `and`: if TOS is `False`, jumps keeping `False`; else pops `True` |
| `JT_KEEP` | `target` | $b \to b \text{ (jump)} \mid \text{ (fall)}$ | Short-circuit `or`: if TOS is `True`, jumps keeping `True`; else pops `False` |
| `CALL` | `(fn<<8)\|argc` | $a_1 \dots a_n \to [\text{ret}]$ | Pops `argc` arguments, invokes function `fn`, pushes return value if non-void |
| `RET` | — | $v \to$ | Returns `v` to caller |
| `RET0` | — | — | Returns from a void function |

### 5.3 Walkthrough: Compiling and Running `sum(10)`

Source (`tests/bml/sum.b`):
```lua
function sum(n: i32): i32
  local s = 0
  for i = 1, n do s = s + i end
  return s
end
```

Disassembly (`python3 tools/bml/bml.py tests/bml/sum.b --dump`):
```text
; constants: K[0]=0, K[1]=1

fn #0 sum(n: i32) -> i32  [locals=4]
  0000  PUSH_CONST   0  ; 0
  0001  STORE        1  ; s
  0002  PUSH_CONST   1  ; 1
  0003  STORE        2  ; i
  0004  LOAD         0  ; n
  0005  STORE        3  ; i$stop
  0006  LOAD         2  ; i
  0007  LOAD         3  ; i$stop
  0008  LE
  0009  JZ           19
  0010  LOAD         1  ; s
  0011  LOAD         2  ; i
  0012  ADD
  0013  STORE        1  ; s
  0014  LOAD         2  ; i
  0015  PUSH_CONST   1  ; 1
  0016  ADD
  0017  STORE        2  ; i
  0018  JMP          6
  0019  LOAD         1  ; s
  0020  RET
```

Local slots of `sum`:
- `slot 0`: parameter `n` (initialized to `10`)
- `slot 1`: local `s` (initialized to `0` at PC `0000..0001`)
- `slot 2`: loop induction variable `i` (initialized to `1` at PC `0002..0003`)
- `slot 3`: loop upper bound `i$stop` (initialized to `n = 10` at PC `0004..0005`)

At each iteration (`PC 0006..0018`), `i <= i$stop` is tested; `s = s + i` and `i = i + 1` execute 10 times (`i = 1..10`), accumulating $1 + 2 + \dots + 10 = 55$. When `i` becomes `11`, `LE` pushes `False`, `JZ 19` jumps to `PC 0019`, `LOAD 1` pushes `55`, and `RET` returns `55`.

---

## 6. Stage 3 Internals: Kernel VM Back End (`BMVMCompiler`)

### 6.1 Architectural Decision: Reusing `bmvm` (`src/bm/bmvm.h`)

The kernel already includes `bmvm` ([`src/bm/bmvm.h`](../../src/bm/bmvm.h), [`src/bm/bmvm.c`](../../src/bm/bmvm.c), exposed to Lua in [`src/bm/api_vm.c`](../../src/bm/api_vm.c) via `vm_load`, `vm_fn`, and `vm_run`), a verified register machine that enforces:
- Load-time validation of register indices (`a, b, c < f->nregs`), constant indices, call targets, and jump targets.
- Strict forward-only conditional jumps (`c->k > pc` for `JF`/`JT`), ensuring backward loops must execute `JMP` or `FORI` where the instruction step budget is decremented.
- Runtime bounds on call depth (`BMVM_DEPTH = 32`), register stack (`BMVM_STACK = 4096`), and execution step budget.

Rather than adding a second bytecode interpreter to the kernel image, `BMVMCompiler` lowers the Stage 2 stack IR directly into a binary `BMV1` module (`0x31564D42`).

### 6.2 Isomorphic Stack-to-Register Mapping (`compute_stack_depths`)

Because every BML expression pushes exactly one value and every statement leaves the stack empty (`sp = 0`), the stack height `sp` before every instruction `pc` is a static constant computed by `compute_stack_depths(fn.code)`.

For each function with `nparams` parameters, `nconst` unique constants, `nextra = nlocals - nparams` local variables, and maximum stack depth `max_sp`, `BMVMCompiler` partitions the function's register window `R[0 .. nregs - 1]` as:
1. **`R[0 .. nparams - 1]`**: Incoming parameters (`slot 0 .. nparams - 1`).
2. **`R[nparams .. nparams + nconst - 1]`**: Per-function constants copied automatically from `m->k[kfirst ..]` by `bmvm_call` on function entry.
3. **`R[nparams + nconst .. nlocals + nconst - 1]`**: Local variables and loop state (`slot nparams .. nlocals - 1`).
4. **`R[nlocals + nconst + k]`**: Evaluation stack slot `S[k]` (`0 <= k <= max_sp`).

Under this mapping, every Stage 2 stack instruction lowers **1-to-1** to a single `bmvm` instruction:
- `JF_KEEP target` (`and`) lowers to `JF S[sp - 1], target` (if `False`, `S[sp - 1]` already holds `0` at `target`; if `True`, the right operand overwrites `S[sp - 1]`).
- `JT_KEEP target` (`or`) lowers to `JT S[sp - 1], target` (if `True`, `S[sp - 1]` already holds `1` at `target`; if `False`, the right operand overwrites `S[sp - 1]`).
- `CALL fn_idx, argc` lowers to `CALL S[sp - argc], argc, 0, fn_idx`: the `argc` arguments are already in contiguous registers `S[sp - argc .. sp - 1]` above all live locals and lower temporaries, and `bmvm`'s `RET` writes the return value into `S[sp - argc]`.

---

## 7. Stage 4 Internals: ARMv6 (A32) Native Code Generator (`ARMv6Compiler`)

### 7.1 Target ABI (Raspberry Pi Zero W, ARM1176JZF-S)

- **Instruction Set**: 32-bit ARMv6 A32, little-endian.
- **Calling Convention (AAPCS)**:
  - Arguments `0..3` in `r0..r3`; arguments `4+` on the stack (`[sp, #0]`, `[sp, #4]`, ...).
  - Return value in `r0`.
  - Scratch registers (caller-saved): `r0..r3`, `r12` (`ip`), `r14` (`lr`).
  - Callee-saved registers: `r4..r10`, `r11` (`fp`), `r13` (`sp`, 8-byte aligned at call boundaries).

### 7.2 Two-Tier Code Generation

1. **Canonical Leaf Optimization (`is_canonical_sum`)**:
   - Recognizes leaf accumulator loops such as `sum(n: i32): i32` and emits the 9 hand-verified A32 words directly (`r0 = n`, `r1 = s`, `r2 = i`, returning via `bx lr` with zero stack-frame overhead):

| Byte Offset | Word (Hex) | A32 Disassembly | Meaning |
|---|---|---|---|
| `0x00` | `0xE3A01000` | `mov r1, #0` | `s = 0` |
| `0x04` | `0xE3A02001` | `mov r2, #1` | `i = 1` |
| `0x08` | `0xE1520000` | `cmp r2, r0` | Compare `i` (`r2`) with `n` (`r0`) |
| `0x0c` | `0xCA000002` | `bgt 0x1c` | If `i > n` (signed): `PC = (0x0c + 8) + (2 << 2) = 0x1c` |
| `0x10` | `0xE0811002` | `add r1, r1, r2` | `s = s + i` |
| `0x14` | `0xE2822001` | `add r2, r2, #1` | `i = i + 1` |
| `0x18` | `0xEAFFFFFA` | `b 0x08` | Unconditional branch: `PC = (0x18 + 8) + (-6 << 2) = 0x08` |
| `0x1c` | `0xE1A00001` | `mov r0, r1` | Move `s` (`r1`) to return register `r0` |
| `0x20` | `0xE12FFF1E` | `bx lr` | Return to caller (`LR`) |

2. **General Register Allocator and Stack Frame (`_compile_general_fn`)**:
   - **Prologue**: `push {r4-r10, fp, lr}; mov fp, sp; sub sp, sp, #frame_bytes` (padded so `36 + frame_bytes` is a multiple of 8 bytes).
   - **Register Allocation**: Slots `0..6` (covering parameters, locals, and the bottom of the evaluation stack) are allocated to hardware callee-saved registers `r4..r10`. Slots $\ge 7$ spill to `[fp, #-4 * (slot - 6)]`. Because `r4..r10` and `fp` are preserved across `bl` calls, nested function calls (`fib(n - 1) + fib(n - 2)`) never clobber live locals or evaluation-stack temporaries.
   - **Immediate Synthesis (`mov32`)**: Arbitrary 32-bit constants are synthesized in 1 to 4 instructions using rotated 8-bit immediates (`mov` / `mvn` + `orr`), avoiding PC-relative literal pool range limits.
   - **Software Floor Division & Modulo (`__bml_idivmod`)**: Since ARM1176 (ARMv6) has no hardware `sdiv` instruction, `ARMv6Compiler` emits a 30-word A32 helper `__bml_idivmod` whenever `//` or `%` is used. It performs unsigned shift-and-subtract division on $|a|$ and $|b|$, rounds the quotient toward $-\infty$ when `a ^ b < 0` and the remainder is non-zero (`mvnne r0, r0`, using $-q - 1 = \sim q$), and computes the floor remainder $r = a - q \times b$ in `r1`.
   - **Epilogue**: `mov sp, fp; pop {r4-r10, fp, pc}`.

### 7.3 Verification on `qemu-system-arm -cpu arm1176`

`make test-bml` (`tests/bml/run.py`) compiles `sum.b` (both 9-word leaf and general frame), `features.b` (347 calls), and 200 random BML programs (1000 calls) into raw A32 machine words, links them into a freestanding ARMv6 ELF binary (`-march=armv6 -marm`), runs them under `qemu-system-arm -M versatilepb -cpu arm1176 -nographic -semihosting`, and checks that all 1,559 outputs match `luahost` byte-for-byte.

### 7.4 Stage 5: Native Pi Loader (`native_load`, `native_fn`, `native_run`) and Cache Maintenance

`tools/bml/bml.py --native <out.bmn>` packs the compiled functions into a `BMN1` (`0x314E4D42`) binary container (`12`-byte header `<III`, `nfn` 56-byte function descriptors `<32sBBH16BI`, and `ncode` 32-bit A32 words). [`src/bm/bmnative.c`](../../src/bm/bmnative.c) and [`src/bm/api_vm.c`](../../src/bm/api_vm.c) expose:
- `m, why = native_load(blob)`: Accepts either a `BMN1` container or raw 32-bit ARMv6 A32 words (`--armv6`). Validates every instruction word (rejects `SVC`/`SWI`, coprocessor `MCR`/`MRC`/`CDP`/`LDC`/`STC`, `BKPT`, and out-of-range `B`/`BL` targets), copies the words into 32-byte-aligned executable RAM, and calls `bmnative_sync_cache`:
  1. `dcache_clean_range(code, bytes)` (`mcr p15, 0, a, c7, c10, 1` per 32-byte line + `arm_dsb()`),
  2. `mcr p15, 0, a, c7, c5, 1` (invalidate I-cache line by MVA),
  3. `mcr p15, 0, 0, c7, c5, 6` (flush Branch Target Cache),
  4. `arm_dsb()` + `arm_isb()` (drain write buffer and flush prefetch pipeline).
- `f = native_fn(m, "name")`: Returns the 0-based function index `0..nfn-1` or `nil`.
- `r = native_run(m, f, ...)`: Invokes function `f` (index or name string) via the AAPCS ABI on ARM (`#if defined(__arm__)`) or the built-in step-bounded A32 interpreter on non-ARM hosts (`bmhost-bin`), returning an integer or boolean to Lua.

---

## 8. Next Stages (Stages 6 to 9)

- **Stage 6 (Types and Memory)**: Arrays and structs; finalize `i32` overflow policy (32-bit two's-complement wrapping on ARMv6/C vs 64-bit Lua integers); add `f32` using the ARM1176 VFPv2 coprocessor.
- **Stage 7 (Standard Functions and `bm` API)**: Function table allowing native ARMv6 code to call `spr`, `rectfill`, `btn`, and math routines.
- **Stage 8 (Games)**: Port the enemy movement of Dark Night to BML and measure frame times on the Pi Zero W.
- **Stage 9 (On the Console)**: Self-hosting / running the compiler on the Pi itself.

---

## 9. Practical Code Examples: BML, Lua, C, and ARMv6 Assembly Side by Side

### 9.1 Example 1: Loop Accumulator (`sum`)

**1. BML Source (`tests/bml/sum.b`)**:
```lua
function sum(n: i32): i32
  local s = 0
  for i = 1, n do s = s + i end
  return s
end
```

**2. Plain Lua (`bml.py --lua`)**:
```lua
function sum(n)
  local s = 0
  for i = 1, n do s = s + i end
  return s
end
```

**3. Equivalent C (`bmt.py --c`)**:
```c
int32_t bmt_sum(int32_t n)
{
    int32_t s = 0;
    {
        int32_t i__end = n;
        for (int32_t i = 1; i <= i__end; i++) {
            s = (s + i);
        }
    }
    return s;
}
```

**4. ARMv6 A32 Assembly (`bml.py --armv6 --dump-armv6`, 9 words = 36 bytes)**:
```armasm
; fn sum @ 0x0000 (r0 = n, r1 = s, r2 = i)
  0x0000: E3A01000    mov   r1, #0          ; s = 0
  0x0004: E3A02001    mov   r2, #1          ; i = 1
  0x0008: E1520000    cmp   r2, r0          ; compare i with n
  0x000c: CA000002    bgt   0x1c            ; if i > n goto return
  0x0010: E0811002    add   r1, r1, r2      ; s = s + i
  0x0014: E2822001    add   r2, r2, #1      ; i = i + 1
  0x0018: EAFFFFFA    b     0x08            ; repeat loop
  0x001c: E1A00001    mov   r0, r1          ; return s in r0
  0x0020: E12FFF1E    bx    lr              ; return to caller
```

---

### 9.2 Example 2: Value Clamping (`clamp` with `min` / `max`)

**1. BML Source (`tests/bml/features.b`)**:
```lua
function clamp(x: i32, lo: i32, hi: i32): i32
  return min(max(x, lo), hi)
end
```

**2. Equivalent C**:
```c
int32_t bmt_clamp(int32_t x, int32_t lo, int32_t hi)
{
    return bmt_min_i32(bmt_max_i32(x, lo), hi);
}
```

**3. ARMv6 A32 Assembly (`bml.py --armv6`, disassembled with `arm-none-eabi-objdump -d`)**:
```armasm
; fn clamp @ 0x004c (r0 = x -> r4, r1 = lo -> r5, r2 = hi -> r6)
  0x004c: e92d4ff0    push  {r4-r10, fp, lr}
  0x0050: e1a0b00d    mov   fp, sp
  0x0054: e3a0c004    mov   ip, #4
  0x0058: e04dd00c    sub   sp, sp, ip
  0x005c: e1a04000    mov   r4, r0          ; slot 0 (x)  = r0
  0x0060: e1a05001    mov   r5, r1          ; slot 1 (lo) = r1
  0x0064: e1a06002    mov   r6, r2          ; slot 2 (hi) = r2
  0x0068: e1a07004    mov   r7, r4          ; S[0] = x
  0x006c: e1a08005    mov   r8, r5          ; S[1] = lo
  0x0070: e1570008    cmp   r7, r8          ; max(x, lo): compare x with lo
  0x0074: b1a07008    movlt r7, r8          ; if x < lo: r7 = lo
  0x0078: e1a08006    mov   r8, r6          ; S[1] = hi
  0x007c: e1570008    cmp   r7, r8          ; min(..., hi): compare r7 with hi
  0x0080: c1a07008    movgt r7, r8          ; if r7 > hi: r7 = hi
  0x0084: e1a00007    mov   r0, r7          ; return value in r0
  0x0088: eaffffff    b     0x8c
  0x008c: e1a0d00b    mov   sp, fp
  0x0090: e8bd8ff0    pop   {r4-r10, fp, pc}
```

---

### 9.3 Example 3: Greatest Common Divisor (`gcd` with `while`, `abs`, and `%`)

**1. BML Source (`tests/bml/features.b`)**:
```lua
function gcd(a: i32, b: i32): i32
  a = abs(a)
  b = abs(b)
  while b ~= 0 do
    local r = a % b
    a = b
    b = r
  end
  return a
end
```

**2. Equivalent C**:
```c
int32_t bmt_gcd(int32_t a, int32_t b)
{
    a = abs(a);
    b = abs(b);
    while ((b != 0)) {
        int32_t r = bmt_imod(a, b);
        a = b;
        b = r;
    }
    return a;
}
```

**3. ARMv6 A32 Assembly (`bml.py --armv6`)**:
```armasm
; fn gcd @ 0x0094 (r4 = a, r5 = b, r6 = r)
  0x0094: e92d4ff0    push  {r4-r10, fp, lr}
  0x0098: e1a0b00d    mov   fp, sp
  0x009c: e3a0c004    mov   ip, #4
  0x00a0: e04dd00c    sub   sp, sp, ip
  0x00a4: e1a04000    mov   r4, r0          ; a = arg0
  0x00a8: e1a05001    mov   r5, r1          ; b = arg1
  0x00ac: e1a07004    mov   r7, r4
  0x00b0: e3570000    cmp   r7, #0
  0x00b4: b2677000    rsblt r7, r7, #0      ; abs(a): negate if < 0
  0x00b8: e1a04007    mov   r4, r7          ; a = abs(a)
  0x00bc: e1a07005    mov   r7, r5
  0x00c0: e3570000    cmp   r7, #0
  0x00c4: b2677000    rsblt r7, r7, #0      ; abs(b): negate if < 0
  0x00c8: e1a05007    mov   r5, r7          ; b = abs(b)
; while loop test (0x00cc):
  0x00cc: e1a07005    mov   r7, r5
  0x00d0: e3a08000    mov   r8, #0
  0x00d4: e1570008    cmp   r7, r8          ; compare b with 0
  0x00d8: e3a07000    mov   r7, #0
  0x00dc: 13a07001    movne r7, #1          ; r7 = (b ~= 0)
  0x00e0: e3570000    cmp   r7, #0
  0x00e4: 0a00000b    beq   0x118           ; exit while if b == 0
  0x00e8: e1a07004    mov   r7, r4
  0x00ec: e1a08005    mov   r8, r5
  0x00f0: e1a00007    mov   r0, r7          ; r0 = a
  0x00f4: e1a01008    mov   r1, r8          ; r1 = b
  0x00f8: eb0001b5    bl    __bml_idivmod   ; r0 = a // b, r1 = a % b
  0x00fc: e1a07001    mov   r7, r1          ; take remainder r1
  0x0100: e1a06007    mov   r6, r7          ; local r = a % b
  0x0104: e1a07005    mov   r7, r5
  0x0108: e1a04007    mov   r4, r7          ; a = b
  0x010c: e1a07006    mov   r7, r6
  0x0110: e1a05007    mov   r5, r7          ; b = r
  0x0114: eaffffec    b     0x00cc          ; jump back to while test
; loop exit (0x0118):
  0x0118: e1a07004    mov   r7, r4
  0x011c: e1a00007    mov   r0, r7          ; return a in r0
  0x0120: eaffffff    b     0x124
  0x0124: e1a0d00b    mov   sp, fp
  0x0128: e8bd8ff0    pop   {r4-r10, fp, pc}
```

---

### 9.4 Example 4: Double Recursion (`fib`)

**1. BML Source (`tests/bml/features.b`)**:
```lua
function fib(n: i32): i32
  if n <= 0 then
    return 0
  elseif n == 1 then
    return 1
  else
    return fib(n - 1) + fib(n - 2)
  end
end
```

**2. Equivalent C**:
```c
int32_t bmt_fib(int32_t n)
{
    if ((n <= 0)) {
        return 0;
    } else if ((n == 1)) {
        return 1;
    } else {
        return (bmt_fib((n - 1)) + bmt_fib((n - 2)));
    }
}
```

**3. ARMv6 A32 Assembly (recursive `else` branch at `0x0198..0x01e4`)**:
```armasm
; else branch of fib (r4 = n):
  0x0198: e1a05004    mov   r5, r4          ; S[0] = n
  0x019c: e3a06001    mov   r6, #1          ; S[1] = 1
  0x01a0: e0455006    sub   r5, r5, r6      ; S[0] = n - 1
  0x01a4: e1a00005    mov   r0, r5          ; arg0 = n - 1
  0x01a8: ebffffdf    bl    fn_fib          ; call fib(n - 1)
  0x01ac: e1a05000    mov   r5, r0          ; S[0] (r5, callee-saved!) = fib(n - 1)
  0x01b0: e1a06004    mov   r6, r4          ; S[1] = n (r4 was preserved by callee!)
  0x01b4: e3a07002    mov   r7, #2          ; S[2] = 2
  0x01b8: e0466007    sub   r6, r6, r7      ; S[1] = n - 2
  0x01bc: e1a00006    mov   r0, r6          ; arg0 = n - 2
  0x01c0: ebffffd9    bl    fn_fib          ; call fib(n - 2) (r5 is preserved!)
  0x01c4: e1a06000    mov   r6, r0          ; S[1] = fib(n - 2)
  0x01c8: e0855006    add   r5, r5, r6      ; S[0] = fib(n - 1) + fib(n - 2)
  0x01cc: e1a00005    mov   r0, r5          ; return value in r0
  0x01d0: ea000002    b     0x01e0          ; jump to epilogue
   ...
  0x01e0: e1a0d00b    mov   sp, fp
  0x01e4: e8bd8ff0    pop   {r4-r10, fp, pc}
```

---

### 9.5 Example 5: Arrays, Structs, and Hardware VFPv2 `f32` (Stages 6–8)

**1. BML Source (`tests/bml/types_mem.b` and `tests/bml/darknight_enemy.b`)**:
```lua
struct Vec2f
  x: f32
  y: f32
end

function step_vec2f(v: Vec2f, dt: f32, lim: f32): i32
  v.x = v.x + 2.0 * dt
  v.y = v.y - 1.5 * dt
  local d: f32 = sqrt(v.x * v.x + v.y * v.y)
  if d > lim then
    v.x = v.x * (lim / d)
    v.y = v.y * (lim / d)
  end
  return floor(d * 100.0)
end
```

**2. Equivalent C**:
```c
typedef struct { float x, y; } Vec2f;

int32_t step_vec2f(Vec2f *v, float dt, float lim)
{
    v->x = v->x + 2.0f * dt;
    v->y = v->y - 1.5f * dt;
    float d = sqrtf(v->x * v->x + v->y * v->y);
    if (d > lim) {
        v->x = v->x * (lim / d);
        v->y = v->y * (lim / d);
    }
    return (int32_t)floorf(d * 100.0f);
}
```

**3. ARMv6 + VFPv2 Coprocessor 10 Assembly (`bml.py --dump-armv6`)**:
```armasm
; VFPv2 single-precision distance & comparison (r4 = v ptr, r5 = dt bits, r6 = lim bits):
  EE007A10    vmov.f32  s0, r7          ; s0 = v.x * v.x + v.y * v.y
  EEB10AC0    vsqrt.f32 s0, s0          ; hardware VFPv2 square root
  EE107A10    vmov.f32  r7, s0          ; r7 = d (IEEE-754 binary32 bits)
  EE007A10    vmov.f32  s0, r7          ; s0 = d
  EE006A90    vmov.f32  s1, r6          ; s1 = lim
  EEB40A60    vcmp.f32  s0, s1          ; compare d with lim in VFPv2
  EEF1FA10    vmrs      APSR_nzcv, fpscr; transfer VFP flags to ARM CPSR
```

---

### 9.6 Stage 9: On-Console Compiler in C (`native_compile`)

On the console (or in `bmhost`), cartridges can compile `.b` source strings directly into `BMN1` ARMv6 + VFPv2 machine code in RAM using `native_compile(src)` ([`src/bm/bmlc.c`](../../src/bm/bmlc.c)):

```lua
local src = [[
function bounce_vel(p: f32, v: f32, lo: f32, hi: f32): f32
  if p < lo and v < 0.0 then return -v end
  if p > hi and v > 0.0 then return -v end
  return v
end
]]

local blob = assert(native_compile(src))
local mod  = assert(native_load(blob))
local v1   = native_run(mod, "bounce_vel", 0.25, -0.5, 0.4, 4.6) -- -> 0.5
```

---

### 9.7 Game Optimization Pattern: Dark Night Enemy Separation (Lua vs .b)

In `carts/darknight/main.lua`, the enemy separation routine scans 272 pairs every frame ($O(N^2)$). Profiling shows it takes **27.3%** of the entire frame logic time in Lua.

#### Original Lua Implementation (`carts/darknight/main.lua` lines 843–856)
```lua
if e.st ~= "dead" and d < FAR then
  local ex, ez = e.x, e.z
  for i = 1, #enemies do
    local o = enemies[i]
    local ox = ex - o.x
    if ox < 0.8 and ox > -0.8 and o ~= e and o.st ~= "dead" then
      local oz = ez - o.z
      local d2 = ox * ox + oz * oz
      if d2 < 0.64 and d2 > 0.000001 then
        local od = sqrt(d2)
        try_move(e, ox / od * 0.02, oz / od * 0.02, 0.35)
        ex, ez = e.x, e.z
      end
    end
  end
end
```
*Bottleneck*: 272 table lookups (`o.x`, `o.z`, `o.st`) and floating-point math in the interpreted Lua stack.

#### Optimized .b Replacement (`tests/bml/darknight_enemy.b`)
```lua
function move_and_separate(
  n: i32, x: f32[], z: f32[], vx: f32[], vz: f32[],
  alive: bool[], out: i32[], counts: i32[]
): i32
  for self = 1, n do
    local count = 0
    counts[self] = 0
    if alive[self] then
      local ex = x[self]
      local ez = z[self]
      for i = 1, n do
        if i ~= self and alive[i] then
          local dx = ex - x[i]
          if dx < 0.8 and dx > -0.8 then
            local dz = ez - z[i]
            local d2 = dx * dx + dz * dz
            if d2 < 0.64 and d2 > 0.00001 then
              count = count + 1
              out[(self - 1) * n + count] = i
            end
          end
        end
      end
      counts[self] = count
    end
  end
  return 0
end
```

#### Zero-Copy Cartridge Integration with `vm_buf`
```lua
-- In _init():
local blob = assert(native_compile(dn_enemy_b_source))
local mod = assert(native_load(blob))
local bx = vm_buf("i32", N)
local bz = vm_buf("i32", N)
local bvx = vm_buf("i32", N)
local bvz = vm_buf("i32", N)
local balive = vm_buf("i32", N)
local bout = vm_buf("i32", N * N)
local bcounts = vm_buf("i32", N)

-- In _update(): Single call per frame passing direct pointers to C arrays
native_run(mod, "move_and_separate", N, bx, bz, bvx, bvz, balive, bout, bcounts)
```

#### Benchmark Comparison (Raspberry Pi Zero W / PC)
| Approach | Time for 17 enemies | Calls / frame | Overhead | Speedup |
|---|---|---|---|---|
| A. Lua tables (`main.lua`) | 15 - 20 us | 0 (Lua VM) | Table hash lookups | Baseline (1.0x) |
| B. Multiple native calls | 8 - 12 us | 34 calls | 34 us bridge overhead | ~1.5x |
| C. Single call + `vm_buf` | 3 - 4 us | 1 call | 1 us zero-copy bridge | **3.7x - 6.0x faster** |

---

### 9.7 Math Primitives & Constant Arrays in `.rodata`

#### 9.7.1 Gamedev Math Builtins

BML includes hardware-optimized math primitives for 2D/3D game math:

| Builtin | Signature | Description | Machine Implementation |
|---|---|---|---|
| `clamp(x, lo, hi)` | `(i32, i32, i32) -> i32` or `(f32, f32, f32) -> f32` | Bounds value `x` between `lo` and `hi` | Branchless conditional move (`cmp`/`movlt`/`movgt` or `vcmp.f32`/`vmrs`) |
| `lerp(a, b, t)` | `(f32, f32, f32) -> f32` | Linear interpolation `a + (b - a) * t` | VFPv2 single-precision `vsub.f32` + `vmul.f32` + `vadd.f32` |
| `flr(x)` | `(f32) -> i32` | Floor float to integer (alias of `floor`) | Hardware VFPv2 `vcvt.s32.f32` with floor adjustment |
| `sin(rad)` | `(f32) -> f32` | Sine of angle in radians | Kernel host call (libm / Lua math bridge) |
| `cos(rad)` | `(f32) -> f32` | Cosine of angle in radians | Kernel host call (libm / Lua math bridge) |
| `atan2(y, x)` | `(f32, f32) -> f32` | Arc tangent of $y / x$ with quadrant | Kernel host call (libm / Lua math bridge) |

#### 9.7.2 Constant Arrays in `.rodata`

Precomputed lookup tables (e.g. trigonometry tables, fixed physics coefficients, tile attributes) can be declared as global constants:

```lua
const SIN_LUT: f32[] = [0.0, 0.7071, 1.0, 0.7071, 0.0]
const TILE_ATTRS: i32[] = [0, 1, 1, 2, 4, 8]

function get_tile_attr(id: i32): i32
  return TILE_ATTRS[id]
end
```

- **Zero-Allocation**: Array elements are packed directly into the executable binary's `.rodata` segment.
- **Zero-Cost PC-Relative Addressing**: The base pointer is computed at runtime via `ldr rd, [pc, #lit]` + `add rd, pc, rd`, requiring no dynamic allocation or table hashing.
- **Lua Desugaring**: In Lua, constant arrays desugar to standard 1-based local tables (`local TILE_ATTRS = {0, 1, 1, 2, 4, 8}`).
- **Immutability**: Assigning to a constant array or its elements is rejected at compile time.

#### 9.7.3 Mutable Local Arrays in RAM

Functions can initialize mutable local arrays directly on the stack or in RAM:

```lua
local arr: i32[] = [10, 20, 30, 40]
local farr: f32[] = [1.5, 2.5, 3.5]

arr[1] = 99
farr[2] += 10.0
```

- **Stack Allocation**: The compiler reserves contiguous words on the stack frame below spill slots, calculates the base address (`fp - offset`), writes the initial elements, and assigns the base pointer to the local variable slot.
- **Lua Compatibility**: Desugars cleanly to Lua table literals (`local arr = {10, 20, 30, 40}`).
- **Indexed Access**: Elements are 1-based and accessed via standard `arr[i]` syntax.

#### 9.7.4 Compile-Time Length Operator (`#arr`)

The `#` operator retrieves the length of known-size collections at compile time without runtime overhead:

```lua
const LUT: i32[] = [10, 20, 30]
local a: i32[] = [1, 2, 3, 4]
local len_lut = #LUT      -- Evaluates to 3 at compile time
local len_a = #a          -- Evaluates to 4 at compile time
local str_len = #"hello"  -- Evaluates to 5 at compile time

for i = 1, #a do
  a[i] = a[i] * 2
end
```

- **Supported Targets**: Constant arrays, mutable local arrays with known declaration size, string literals, and array literals.
- **Zero Runtime Cost**: Emitted as an immediate constant (`PUSH_CONST` / `mov rd, #imm`).
- **Safety**: Applying `#` to non-array scalar types or variables without known compile-time bounds is rejected with a type error.

---

## 10. Utility of `.b`, Performance vs. C (GCC/Clang), and Optimization Roadmap

This section documents how `.b` compares to compiled C on the Pi Zero W (ARM1176JZF-S), why `.b` exists in the `bm` cartridge model, and where future compiler improvements should focus.

### 10.1 Where `.b` Sits Today: Lua VM vs. `.b` vs. Compiled C

Although `.b` emits native 32-bit ARMv6 A32 + VFPv2 machine code (via [`tools/bml/bml.py`](../../tools/bml/bml.py) or [`src/bm/bmlc.c`](../../src/bm/bmlc.c)), its performance sits **far above the Lua VM** and **below C compiled with GCC/Clang `-O2`/`-O3`** on general code:

| Aspect | Lua 5.4 VM | `.b` (`bmlc.c` / `ARMv6Compiler`) | C (`gcc -O2/-O3` for ARM1176) |
|---|---|---|---|
| **Execution model** | Bytecode interpreter + dynamic boxing | Native ARMv6 A32 + VFPv2 machine words | Native ARMv6 A32 + VFPv2 machine words |
| **Register allocation** | Virtual stack registers | Fixed slots $\to$ `r4..r10` + `s4..s31` (`bmlc.c`), spills to `[fp, #-off]` | Graph-coloring / SSA across `r0..r12` and `s0..s31` |
| **VFPv2 (`f32`) handling** | `double` (`f64`) boxed in `TValue` | `bmlc.c` keeps `f32` in `s4..s31` with `vldr`/`vstr`/`vpush`/`vpop`/`vmla.f32`; `bml.py` uses `vmov` via ARM regs | Values kept in `s0..s31` across loops + `vldr`/`vstr` |
| **Loop optimizations** | None | `check_canonical_leaf_sum` + `base - 4` hoisting (`bmlc.c`) | LICM, CSE, unrolling, strength reduction, inlining |
| **Prologue / epilogue** | Lua C frame | `bmlc.c` minimal callee-saved set & zero-frame tiny leaves; `bml.py` pushes `{r4-r10, fp, lr}` | Minimal callee-saved set; leaf functions omit stack frames |
| **Division (`//`, `%`)** | VM opcode | Power-of-2 constant divisors lowered to `asr`/`and` in `bmlc.c`; general divisors call `__bml_idivmod` | Constant divisors lowered to multiply + shift |
| **Array indexing** | Hash/array table lookup | `bmlc.c` uses `ldr/str [base_m4, idx, lsl #2]` and `vldr/vstr` on hoisted `base - 4`; `bml.py` computes `(idx - 1) << 2` | Pointer induction with pre/post-increment (`ldr r, [p], #4`) |
| **Bulk memory** | C `memcpy` via API | `bmlc.c` uses 16-byte `ldmia`/`stmia` + word loops (`__bml_memcpy`/`__bml_memset`); `bml.py` uses byte loops (`ldrb`/`strb`) | Word/block transfers (`ldm`/`stm`) |
| **Math & kernel calls** | Direct C API call | `bmlc.c` dispatches `sin`/`cos`/`atan2` (`api_vm.c`) and `s16_*` (`snes16.c`) directly in C; other `OP_CALL_HOST` calls route through Lua | Direct `bl` to C / `libm` |

### 10.2 Why `.b` Still Matters in `bm`

Cartridges (`.bm`) cannot ship arbitrary precompiled C binaries: the kernel enforces a single portable cartridge format and does not link untrusted ELF object files. Within those constraints, `.b` fills a unique niche:

1. **Zero-toolchain native execution on the console**: [`native_compile`](../../src/bm/api_vm.c) compiles `.b` source text directly into `BMN1` ARMv6 machine code in RAM on the Pi Zero W without GCC, Python, or a cross-compiler.
2. **Verified sandboxing**: [`bmnative_load`](../../src/bm/bmnative.c) (`check_armv6_words`) rejects privileged/coprocessor instructions (allowing only CP10 VFPv2), `SVC`/`SWI`, `BKPT`, and out-of-module branches before synchronizing D-cache/I-cache.
3. **Eliminating Lua table and bridge bottlenecks**: Passing `vm_buf` pointers once per frame (`native_run`) avoids both Lua table hashing and the ~1 µs per-call Lua $\leftrightarrow$ C crossing cost (e.g. 3.7x–6.0x faster on `move_and_separate` in Section 9.7).
4. **Single-source portability**: Stripping `: <type>` yields plain Lua 5.4 so the same game source runs on non-ARM hosts (or when native compilation is bypassed) with identical semantics.

### 10.3 Can `.b` Beat Compiled C?

For the **same algorithm with compile-time-known data**, `.b` cannot outperform a mature optimizing C compiler (GCC/Clang): both target the exact same ARM1176 silicon, and GCC/Clang apply decades of SSA passes, pipeline scheduling, and register allocation. On trivial leaf loops (such as `sum`), `.b` reaches **parity** (identical instructions).

However, `.b` has three narrow architectural paths where it can **match or outperform static Ahead-of-Time (AOT) kernel C**:

1. **Runtime specialization (partial evaluation via `native_compile`)**: Kernel C is compiled once ahead of time and must branch on runtime flags, map dimensions, blend modes, or function pointers inside hot loops. Because `native_compile` generates machine code on the console in RAM, a cartridge can synthesize `.b` source with level constants, formulas, or dimensions baked in—and `bmlc.c` folds constants and eliminates dead branches inside the inner loop.
2. **Fusing cartridge logic without callback overhead**: Generic C subsystems (particle updaters, collision filters, custom sort comparators) must invoke a callback or return to Lua per element. Compiling the cartridge's custom inner loop as one `.b` function removes indirect calls and Lua crossings.
3. **Targeted backend primitives**: Emitting ARMv6 integer SIMD (`uadd8`, `sadd16`, `usad8`) or word/block `ldm`/`stm` memory loops directly from `.b` builtins can beat portable scalar C written without compiler intrinsics or inline assembly.

### 10.4 Optimization Roadmap for an ARMv6-Tuned `.b` Backend

While general-purpose GCC `-O2`/`-O3` remains hard to beat on arbitrary programs, closing the gap on `bm`'s target workload (numeric/physics/3D loops on `vm_buf` arrays, targeting ~80–100% of C speed) comes down to **five concrete backend improvements**:

1. **Keep `f32` values in VFPv2 registers (`s4..s31`) *(done in `bmlc.c` + `bmnative.c`)***:
   - [`check_armv6_words`](../../src/bm/bmnative.c) and `sim_armv6_call` permit CP10 `0xC`/`0xD` (`vldr`/`vstr`/`vldm`/`vstm`) and `s0..s31`; [`compile_armv6_fn`](../../src/bm/bmlc.c) allocates `f32` locals and stack operands directly in `s4..s31` (using `vpush`/`vpop` on `s16..s31` when needed and loading non-`imm12` float literals from `.rodata` via `vldr`).
2. **Inline rotated immediates and ARM barrel-shifter indexing *(done in `bmlc.c`)***:
   - Fold 8-bit rotated constants directly into `add`/`sub`/`cmp`/`cmn`/bitwise immediates, lower `OP_AGET`/`OP_ASET` to `ldr`/`str [base_m4, idx, lsl #2]`, and hoist `base - 4` for unmodified array parameters.
3. **Minimal prologue/epilogue and generic leaf functions *(done in `bmlc.c`)***:
   - Save/restore only the subset of `r4..r10` (plus `fp`, `lr`) touched by the function, and omit `push`/`pop` entirely (`bx lr`) for leaf functions without calls or callee-saved slots.
4. **Low-cost local/peephole passes and word-block memory *(done in `bmlc.c`)***:
   - Done: compile-time scalar constant propagation/folding, dead-branch elimination (unlocking runtime specialization via `native_compile`), word/block `ldmia`/`stmia` transfers for `OP_MEMCPY`/`OP_MEMSET`, `mla` (`i32`) and `vmla.f32`/`vmls.f32` (`f32`) fusion, `cmp`/`fcmp` + `jz` fusion, `OP_LOAD` aliasing, and power-of-2 constant `*`, `//`, `%` shifts/masks.
5. **Small-function inlining and direct C math fast-paths *(done in `bmlc.c` + `api_vm.c`)***:
   - Done: [`optimize_ir`](../../src/bm/bmlc.c) inlines leaf small `.b` helper functions (`code_len <= 48`, no `OP_NEW_ARR`) directly into callers, and [`bmnative_lua_host_dispatch`](../../src/bm/api_vm.c) binds `sin`, `cos`, and `atan2` directly to C `libm` (`sinf`, `cosf`, `atan2f`) without Lua lookups. (Offline PC build-time SSA/graph-coloring/LICM in [`tools/bml/bml.py`](../../tools/bml/bml.py) remains an optional Path B track in §10.5.)

### 10.5 Two Implementation Paths (Combinable)

Both paths emit the exact same `BMN1` binary container loaded by [`bmnative_load`](../../src/bm/bmnative.c), so they require no cartridge format changes:

- **Path A — Upgrade [`src/bm/bmlc.c`](../../src/bm/bmlc.c) (On-Console Compiler)**:
  Items 1–4 require ~1,000–1,500 lines of straightforward C (no full SSA optimizer needed) and keep instant in-RAM compilation on the Pi Zero W via `native_compile(src)` (including runtime constant specialization).
- **Path B — Offline Optimizing Backend in [`tools/bml/bml.py`](../../tools/bml/bml.py) (PC Build-Time AOT)**:
  During cartridge packaging on a PC, Python has no RAM/CPU constraints and can run full SSA passes, graph-coloring register allocation across `r0..r12` and `s0..s31`, loop-invariant code motion, and cross-function inlining, packing precompiled `BMN1` modules directly into `.bm` builds.

---

## 11. Cartridge Integration (`BM_SEC_B = 17`), Disassembly, and Console Editors

As of specification revision `7` ([`docs/spec/specs.toml`](../spec/specs.toml)), `.b` code can live directly inside `.bm` and `.bme` containers alongside or instead of Lua:

1. **Container Section (`BM_SEC_B = 17`)**: [`src/bm/bm.h`](../../src/bm/bm.h), [`src/bm/format.c`](../../src/bm/format.c), and [`scripts/mkbm.py`](../../scripts/mkbm.py) (`--b`, alone or with `--lua`) pack `.b` source into `BM_SEC_B`. Pure `.b` cartridges define `_init`, `_update`, and `_draw` (each taking `()` or `(state: i32[])` backed by a persistent 256-word buffer in [`src/bm/runtime.c`](../../src/bm/runtime.c)). Hybrid cartridges expose compiled `.b` functions to Lua via `_B` and `B.<fn>(...)`.
2. **Compile-Time Error Reporting**: [`bm_run()`](../../src/bm/runtime.c) compiles `cart.b` on load with [`bml_compile_native`](../../src/bm/bmlc.c) and reports syntax/type diagnostics as `main.b:<line>:<message>`, jumping the cursor in `bm Code` and `bm SDK` to the failing line.
3. **Shared ARMv6 + VFPv2 Disassembler (`native_disasm`)**: [`bmnative_disasm_word`](../../src/bm/bmnative.c) provides a freestanding ARMv6 + VFPv2 CP10 decoder (`vldr`, `vstr`, `vpush`, `vpop`, `vmov`, `vadd`/`vsub`/`vmul`/`vdiv`/`vmla`/`vmls`/`vneg`/`vabs`/`vsqrt`/`vcvt`/`vcmp`/`vcmpz`/`vmrs`, `ldr`/`str` indexed with register offsets and shifts, `mla`/`mul`, rotated `imm12` and ALU shifts) shared by [`native_disasm(blob)`](../../src/bm/api_vm.c) in `bm Code` (`Ctrl+Shift+D`) and the serial shell editor [`src/kernel/shell_code.c`](../../src/kernel/shell_code.c).
4. **Console Editors (`bm Code` & `shell_code`)**: Both [`carts/code/main.lua`](../../carts/code/main.lua) and [`src/kernel/shell_code.c`](../../src/kernel/shell_code.c) edit `.b` and `.lua` with syntax highlighting, language-specific completion, syntax checks (`native_compile` / `tool_lua_check`), sandboxed execution (`native_run` with `f32` I/O formatting in `shell_code`, `tool_lua_run`), and disassembly views.





