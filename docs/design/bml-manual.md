# BML Language & Compiler Manual (`tools/bml/`)

This document is the technical reference and manual for **BML** (*BareMetal Language*), the compiler and virtual machine in [`tools/bml/bml.py`](../../tools/bml/bml.py).

For project state and rules, see [`docs/progress/typed.md`](../progress/typed.md). For the earlier C-generating prototype (`scripts/bmt.py`) and benchmark history, see [`docs/design/typed-native.md`](typed-native.md).

---

## 1. Purpose and Design Principles

BML is a statically typed subset of Lua 5.4 designed for `bm` cartridges on the Raspberry Pi Zero W (BCM2835, ARM1176JZF-S, ARMv6 A32):

1. **Single-Source Dual Execution**: Every valid `.bml` file is also valid Lua 5.4 once its `: <type>` annotations are stripped (`bml.py --lua`). The standard Lua VM is the ground-truth oracle for every test stage.
2. **Explicit Multi-Pass Architecture**: Unlike `scripts/bmt.py` (which emits C text directly during single-pass parsing), `tools/bml/bml.py` separates the compiler into distinct, inspectable stages:
   - **Stage 1 (Front End)**: `Lexer` $\to$ `Parser` $\to$ `AST` $\to$ `TypeChecker` (+ Lua stripper `to_lua`).
   - **Stage 2 (Host Bytecode & VM)**: `BytecodeCompiler` $\to$ `Module` $\to$ `StackVM` (with `--dump` disassembler).
   - **Stage 3 (Kernel VM — next)**: Target either the existing register VM (`src/bm/bmvm.h`) or a compact stack bytecode section in `.bm`.
   - **Stage 4 (ARMv6 A32 Back End — goal)**: Lower the checked AST / IR directly to 32-bit ARMv6 machine words (AAPCS calling convention).
3. **Line-Accurate Diagnostics**: Every lexical, syntactic, or type error reports the 1-based line number of the offending token or statement (`line <N>: <message>`).

---

## 2. Command-Line Usage

```bash
# 1. Check syntax and types only (Stage 1)
python3 tools/bml/bml.py tests/bml/sum.bml --check

# 2. Strip type annotations to produce plain Lua
python3 tools/bml/bml.py tests/bml/sum.bml --lua build/sum.lua

# 3. Disassemble the compiled Stage 2 stack bytecode
python3 tools/bml/bml.py tests/bml/sum.bml --dump

# 4. Compile and run a function in the Stage 2 StackVM on the host
python3 tools/bml/bml.py tests/bml/sum.bml --run sum 10
# -> 55

# 5. Run with 32-bit signed two's-complement wrapping enabled
python3 tools/bml/bml.py tests/bml/sum.bml --wrap32 --run sum 10

# 6. Run the full verification suite (Stage 1 + Stage 2 + ARMv6 sum check + 200 random programs vs Lua)
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
cmp_expr    = band_expr [ ( "<" | "<=" | ">" | ">=" | "==" | "~=" ) band_expr ] ;
band_expr   = add_expr { "&" add_expr } ;
add_expr    = mul_expr { ( "+" | "-" ) mul_expr } ;
mul_expr    = unary_expr { ( "*" | "//" | "%" ) unary_expr } ;
unary_expr  = ( "-" | "not" ) unary_expr | primary ;
primary     = INT | "true" | "false" | "(" expr ")" | NAME "(" [ arg_list ] ")" | NAME ;
```

### 3.4 Operators and Precedence (Lowest to Highest)

| Level | Operators | Operand Types | Result Type | Semantics |
|---|---|---|---|---|
| 1 | `or` | `bool, bool` | `bool` | Short-circuit logical OR (right operand not evaluated if left is `true`) |
| 2 | `and` | `bool, bool` | `bool` | Short-circuit logical AND (right operand not evaluated if left is `false`) |
| 3 | `<`, `<=`, `>`, `>=` | `i32, i32` | `bool` | Signed integer order comparison |
| 3 | `==`, `~=` | `T, T` (`i32` or `bool`) | `bool` | Equality / inequality of identical types |
| 4 | `&` | `i32, i32` | `i32` | Bitwise AND (between `+`/`-` and comparisons, matching Lua 5.4) |
| 5 | `+`, `-` | `i32, i32` | `i32` | Integer addition and subtraction |
| 6 | `*`, `//`, `%` | `i32, i32` | `i32` | Multiplication, floor division toward $-\infty$, floor modulo ($a - (a // b) \times b$) |
| 7 | `-` (unary), `not` | `i32` / `bool` | `i32` / `bool` | Arithmetic negation / logical negation |

### 3.5 Built-in Functions

Three intrinsic functions are built into the type checker and lowered directly to single VM opcodes (`ABS`, `MIN`, `MAX`):
- `abs(x: i32): i32`
- `min(a: i32, b: i32): i32`
- `max(a: i32, b: i32): i32`

When running the stripped Lua output (`--lua`), bind them in the Lua prelude via `local abs, min, max = math.abs, math.min, math.max`.

---

## 4. Stage 1 Internals: Lexer, Parser, AST, and Type Checker

### 4.1 Lexer (`tokenize`)

`tokenize(src)` scans the source with `TOKEN_RE`, tracks 1-based line numbers across newlines, skips whitespace and `--` comments, classifies identifiers against `KEYWORDS`, and appends an explicit `eof` token. Every `Token` records `(kind, text, line, start, end)`.

### 4.2 Parser and Lua Stripping (`Parser`, `to_lua`)

`Parser` is a recursive-descent parser that produces a `Program` tree while recording byte spans `(start, end)` in `self.cuts` whenever `parse_type_annotation()` consumes `: i32` or `: bool` (on function parameters, function return types, or optional `local x: i32 = ...` declarations).

`to_lua(src)` runs `parse_and_check(src)` first—guaranteeing that ill-typed sources are never emitted—and then slices out `prog.cuts` in reverse order. Because only `: <type>` spans are removed, line numbers in the stripped Lua file match the `.bml` source 1-to-1.

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
| `BAND` | — | $a, b \to (a \mathbin{\&} b)$ | Bitwise AND |
| `NEG` | — | $a \to (-a)$ | Integer negation |
| `NOT` | — | $a \to (\neg a)$ | Boolean negation |
| `EQ`, `NE` | — | $a, b \to (a \circ b)$ | Equality / inequality (`bool`) |
| `LT`, `LE`, `GT`, `GE` | — | $a, b \to (a \circ b)$ | Signed integer comparison (`bool`) |
| `ABS` | — | $a \to |a|$ | Built-in `abs(a)` |
| `MIN`, `MAX` | — | $a, b \to \min/\max(a,b)$ | Built-in `min(a, b)` / `max(a, b)` |
| `JMP` | `target` | — | Unconditional jump (`pc = target`) |
| `JZ` | `target` | $\text{cond} \to$ | Pops `cond`; if `False`, sets `pc = target` |
| `JF_KEEP` | `target` | $b \to b \text{ (jump)} \mid \text{ (fall)}$ | Short-circuit `and`: if TOS is `False`, jumps keeping `False`; else pops `True` |
| `JT_KEEP` | `target` | $b \to b \text{ (jump)} \mid \text{ (fall)}$ | Short-circuit `or`: if TOS is `True`, jumps keeping `True`; else pops `False` |
| `CALL` | `(fn<<8)\|argc` | $a_1 \dots a_n \to [\text{ret}]$ | Pops `argc` arguments, invokes function `fn`, pushes return value if non-void |
| `RET` | — | $v \to$ | Returns `v` to caller |
| `RET0` | — | — | Returns from a void function |

### 5.3 Walkthrough: Compiling and Running `sum(10)`

Source (`tests/bml/sum.bml`):
```lua
function sum(n: i32): i32
  local s = 0
  for i = 1, n do s = s + i end
  return s
end
```

Disassembly (`python3 tools/bml/bml.py tests/bml/sum.bml --dump`):
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

## 6. Stage 4 Preview: Verified ARMv6 Words of `sum`

In `docs/progress/typed.md`, nine 32-bit ARMv6 (A32) machine words are given for `sum(n: i32): i32` under the AAPCS calling convention (`n` in `r0`, return value in `r0`, scratch registers `r1` for `s` and `r2` for `i`).

`tools/bml/bml.py` (`SUM_ARMV6_WORDS` and `run_armv6_words`) and `tests/bml/run.py` verify these 9 words both with `arm-none-eabi-objdump` and by executing them across `n = -5 .. 100`:

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

---

## 7. Open Architectural Questions (for Stages 3 and 6)

1. **`i32` Overflow Semantics (Stage 6)**:
   - Native ARMv6 A32 instructions (`ADD`, `SUB`, `MUL`) wrap modulo $2^{32}$ in two's complement at zero cost, whereas plain Lua 5.4 uses 64-bit integers.
   - `StackVM` already supports both modes (`wrap32=False` by default for direct comparison with unannotated Lua, and `wrap32=True` via `--wrap32` for 32-bit two's-complement wrapping).
   - When Stage 6 fixes 32-bit wrapping as the language rule, `to_lua` can either assume programs stay within `[-2^31, 2^31 - 1]` or wrap arithmetic expressions when strict overflow equivalence is requested.
2. **Kernel Virtual Machine Choice (Stage 3)**:
   - `src/bm/bmvm.h` (`bmvm.c`) is already in the kernel as a verified register machine with step budget and bounds checks.
   - Stage 3 can either emit `bmvm` register bytecode from the `bml.py` AST or embed a compact stack VM in C mirroring `StackVM`.
