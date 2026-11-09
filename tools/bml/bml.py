#!/usr/bin/env python3
"""bml.py: the compiler and stack virtual machine for the language of bm (stages 1 and 2).

The language of bm uses the typed Lua syntax of scripts/bmt.py so that a single source file:
  1. runs in the standard Lua VM when type annotations are stripped (--lua), and
  2. compiles through the bml front end (Lexer -> Parser -> AST -> TypeChecker) to stack bytecode
     executed by the host StackVM (stage 2), and later to ARMv6 A32 machine words (stage 4).

Usage:
  python3 tools/bml/bml.py <file.bml> [--check] [--lua <out.lua>] [--dump] [--run <fn> <arg>...] [--wrap32]
"""
import argparse
from dataclasses import dataclass, field
import re
import struct
import sys
from typing import Dict, List, Optional, Tuple, Union

# ---------------------------------------------------------------------------
# Errors
# ---------------------------------------------------------------------------

class Error(Exception):
    """Compile-time (lexical, syntax, or type) error with a 1-based source line."""


class VMError(Exception):
    """Runtime error raised by the stack virtual machine (budget, depth, bad call)."""


# ---------------------------------------------------------------------------
# Stage 1.1: Lexer
# ---------------------------------------------------------------------------

TOKEN_RE = re.compile(
    r"(?P<ws>\s+)"
    r"|(?P<comment>--[^\n]*)"
    r"|(?P<float>\d+\.\d*|\.\d+)"
    r"|(?P<int>\d+)"
    r"|(?P<name>[A-Za-z_]\w*)"
    r"|(?P<op><=|>=|==|~=|//|[-+*/%&(),:\[\]=<>])"
)

KEYWORDS = {
    "function", "local", "for", "do", "while", "end",
    "if", "then", "elseif", "else", "return", "break",
    "and", "or", "not", "true", "false",
}

TYPES = ("i32", "bool")
BUILTINS = {
    "abs": (["i32"], "i32"),
    "min": (["i32", "i32"], "i32"),
    "max": (["i32", "i32"], "i32"),
}


@dataclass(frozen=True)
class Token:
    kind: str       # 'kw', 'name', 'int', 'float', 'op', 'eof'
    text: str
    line: int
    start: int
    end: int


def tokenize(src: str) -> List[Token]:
    """Splits a BML source string into tokens with 1-based line numbers and byte spans."""
    toks: List[Token] = []
    pos = 0
    line = 1
    last_pos = 0
    n = len(src)
    while pos < n:
        m = TOKEN_RE.match(src, pos)
        if not m:
            line += src.count("\n", last_pos, pos)
            raise Error(f"line {line}: unexpected character {src[pos]!r}")
        line += src.count("\n", last_pos, m.start())
        last_pos = m.start()
        grp = m.lastgroup
        if grp not in ("ws", "comment"):
            text = m.group()
            kind = "kw" if (grp == "name" and text in KEYWORDS) else grp
            toks.append(Token(kind, text, line, m.start(), m.end()))
        pos = m.end()
    line += src.count("\n", last_pos, n)
    toks.append(Token("eof", "", line, n, n))
    return toks


# ---------------------------------------------------------------------------
# Stage 1.2: Abstract Syntax Tree (AST)
# ---------------------------------------------------------------------------

@dataclass
class Expr:
    line: int
    typ: Optional[str] = field(default=None, init=False)


@dataclass
class IntLit(Expr):
    value: int


@dataclass
class BoolLit(Expr):
    value: bool


@dataclass
class VarExpr(Expr):
    name: str


@dataclass
class UnaryOp(Expr):
    op: str         # '-', 'not'
    operand: Expr


@dataclass
class BinOp(Expr):
    op: str         # '+', '-', '*', '//', '%', '&', '<', '<=', '>', '>=', '==', '~=', 'and', 'or'
    left: Expr
    right: Expr


@dataclass
class CallExpr(Expr):
    callee: str
    args: List[Expr]


@dataclass
class Stmt:
    line: int


@dataclass
class LocalStmt(Stmt):
    name: str
    decl_type: Optional[str]
    expr: Expr


@dataclass
class AssignStmt(Stmt):
    name: str
    expr: Expr


@dataclass
class IfBranch:
    line: int
    cond: Expr
    body: List[Stmt]


@dataclass
class IfStmt(Stmt):
    branches: List[IfBranch]
    else_body: Optional[List[Stmt]]


@dataclass
class WhileStmt(Stmt):
    cond: Expr
    body: List[Stmt]


@dataclass
class ForStmt(Stmt):
    var: str
    start: Expr
    stop: Expr
    step: Optional[Expr]
    body: List[Stmt]


@dataclass
class ReturnStmt(Stmt):
    expr: Optional[Expr]


@dataclass
class BreakStmt(Stmt):
    pass


@dataclass
class CallStmt(Stmt):
    call: CallExpr


@dataclass
class Param:
    name: str
    typ: str
    line: int


@dataclass
class FuncDecl:
    name: str
    params: List[Param]
    ret_type: Optional[str]
    body: List[Stmt]
    line: int


@dataclass
class Program:
    funcs: List[FuncDecl]
    cuts: List[Tuple[int, int]]


# ---------------------------------------------------------------------------
# Stage 1.3: Recursive-Descent Parser
# ---------------------------------------------------------------------------

class Parser:
    def __init__(self, src: str):
        self.src = src
        self.toks = tokenize(src)
        self.i = 0
        self.cuts: List[Tuple[int, int]] = []

    def cur(self) -> Token:
        return self.toks[self.i]

    def fail(self, msg: str, tok: Optional[Token] = None) -> None:
        t = tok or self.cur()
        raise Error(f"line {t.line}: {msg}")

    def accept(self, text: str) -> Optional[Token]:
        t = self.cur()
        if t.text == text and t.kind in ("kw", "op"):
            self.i += 1
            return t
        return None

    def expect(self, text: str) -> Token:
        t = self.accept(text)
        if not t:
            found = self.cur().text or "end of file"
            self.fail(f"expected '{text}', found '{found}'")
        return t

    def expect_name(self) -> Token:
        t = self.cur()
        if t.kind != "name":
            found = t.text or "end of file"
            self.fail(f"expected a name, found '{found}'")
        self.i += 1
        return t

    def parse_type_annotation(self) -> str:
        """Parses ': <type>' and records its byte span for Lua stripping."""
        colon = self.expect(":")
        tname = self.expect_name()
        if tname.text not in TYPES:
            self.fail(f"unknown type '{tname.text}' (expected i32 or bool)", tname)
        self.cuts.append((colon.start, tname.end))
        return tname.text

    def parse(self) -> Program:
        funcs: List[FuncDecl] = []
        while self.cur().kind != "eof":
            funcs.append(self.parse_function())
        return Program(funcs=funcs, cuts=self.cuts)

    def parse_function(self) -> FuncDecl:
        kw = self.expect("function")
        fname = self.expect_name()
        self.expect("(")
        params: List[Param] = []
        if self.cur().text != ")":
            while True:
                pname = self.expect_name()
                ptype = self.parse_type_annotation()
                params.append(Param(pname.text, ptype, pname.line))
                if not self.accept(","):
                    break
        self.expect(")")
        ret_type: Optional[str] = None
        if self.cur().text == ":":
            ret_type = self.parse_type_annotation()
        body = self.parse_block({"end"})
        self.expect("end")
        return FuncDecl(fname.text, params, ret_type, body, kw.line)

    def parse_block(self, stops: set) -> List[Stmt]:
        stmts: List[Stmt] = []
        while self.cur().kind != "eof" and self.cur().text not in stops:
            stmts.append(self.parse_stmt())
        return stmts

    def parse_stmt(self) -> Stmt:
        t = self.cur()
        if t.kind == "kw":
            if t.text == "local":
                self.i += 1
                v = self.expect_name()
                decl_type: Optional[str] = None
                if self.cur().text == ":":
                    decl_type = self.parse_type_annotation()
                self.expect("=")
                e = self.parse_expr()
                return LocalStmt(t.line, v.text, decl_type, e)
            if t.text == "if":
                self.i += 1
                cond = self.parse_expr()
                self.expect("then")
                body = self.parse_block({"elseif", "else", "end"})
                branches = [IfBranch(t.line, cond, body)]
                while self.cur().text == "elseif":
                    ekw = self.expect("elseif")
                    econd = self.parse_expr()
                    self.expect("then")
                    ebody = self.parse_block({"elseif", "else", "end"})
                    branches.append(IfBranch(ekw.line, econd, ebody))
                else_body: Optional[List[Stmt]] = None
                if self.accept("else"):
                    else_body = self.parse_block({"end"})
                self.expect("end")
                return IfStmt(t.line, branches, else_body)
            if t.text == "while":
                self.i += 1
                cond = self.parse_expr()
                self.expect("do")
                body = self.parse_block({"end"})
                self.expect("end")
                return WhileStmt(t.line, cond, body)
            if t.text == "for":
                self.i += 1
                v = self.expect_name()
                self.expect("=")
                start = self.parse_expr()
                self.expect(",")
                stop = self.parse_expr()
                step: Optional[Expr] = None
                if self.accept(","):
                    step = self.parse_expr()
                self.expect("do")
                body = self.parse_block({"end"})
                self.expect("end")
                return ForStmt(t.line, v.text, start, stop, step, body)
            if t.text == "return":
                self.i += 1
                if self.cur().text in ("end", "else", "elseif") or self.cur().kind == "eof":
                    return ReturnStmt(t.line, None)
                return ReturnStmt(t.line, self.parse_expr())
            if t.text == "break":
                self.i += 1
                return BreakStmt(t.line)

        if t.kind == "name":
            ident = self.expect_name()
            if self.accept("("):
                args = self.parse_arg_list()
                return CallStmt(ident.line, CallExpr(ident.line, ident.text, args))
            self.expect("=")
            e = self.parse_expr()
            return AssignStmt(ident.line, ident.text, e)

        found = t.text or "end of file"
        self.fail(f"a statement cannot start with '{found}'", t)

    def parse_arg_list(self) -> List[Expr]:
        args: List[Expr] = []
        if self.cur().text != ")":
            while True:
                args.append(self.parse_expr())
                if not self.accept(","):
                    break
        self.expect(")")
        return args

    # -- Expressions (precedence: or < and < cmp < & < + - < * // % < unary < primary)
    def parse_expr(self) -> Expr:
        return self.parse_or()

    def parse_or(self) -> Expr:
        left = self.parse_and()
        while self.accept("or"):
            right = self.parse_and()
            left = BinOp(left.line, "or", left, right)
        return left

    def parse_and(self) -> Expr:
        left = self.parse_cmp()
        while self.accept("and"):
            right = self.parse_cmp()
            left = BinOp(left.line, "and", left, right)
        return left

    def parse_cmp(self) -> Expr:
        left = self.parse_band()
        t = self.cur()
        if t.kind == "op" and t.text in ("<", "<=", ">", ">=", "==", "~="):
            self.i += 1
            right = self.parse_band()
            return BinOp(left.line, t.text, left, right)
        return left

    def parse_band(self) -> Expr:
        left = self.parse_add()
        while self.cur().kind == "op" and self.cur().text == "&":
            self.i += 1
            right = self.parse_add()
            left = BinOp(left.line, "&", left, right)
        return left

    def parse_add(self) -> Expr:
        left = self.parse_mul()
        while self.cur().kind == "op" and self.cur().text in ("+", "-"):
            op = self.cur().text
            self.i += 1
            right = self.parse_mul()
            left = BinOp(left.line, op, left, right)
        return left

    def parse_mul(self) -> Expr:
        left = self.parse_unary()
        while self.cur().kind == "op" and self.cur().text in ("*", "/", "//", "%"):
            op = self.cur().text
            self.i += 1
            right = self.parse_unary()
            left = BinOp(left.line, op, left, right)
        return left

    def parse_unary(self) -> Expr:
        t = self.cur()
        if self.accept("-"):
            operand = self.parse_unary()
            return UnaryOp(t.line, "-", operand)
        if self.accept("not"):
            operand = self.parse_unary()
            return UnaryOp(t.line, "not", operand)
        return self.parse_primary()

    def parse_primary(self) -> Expr:
        t = self.cur()
        if t.kind == "int":
            self.i += 1
            val = int(t.text)
            if val > 0x80000000:
                self.fail(f"integer literal {t.text} overflows i32", t)
            return IntLit(t.line, val)
        if t.kind == "float":
            self.fail(f"floating-point literal '{t.text}' is not in stage 1 (only i32 and bool)", t)
        if self.accept("true"):
            return BoolLit(t.line, True)
        if self.accept("false"):
            return BoolLit(t.line, False)
        if self.accept("("):
            e = self.parse_expr()
            self.expect(")")
            return e
        if t.kind == "name":
            ident = self.expect_name()
            if self.accept("("):
                args = self.parse_arg_list()
                return CallExpr(ident.line, ident.text, args)
            return VarExpr(ident.line, ident.text)
        found = t.text or "end of file"
        self.fail(f"an expression cannot start with '{found}'", t)


# ---------------------------------------------------------------------------
# Stage 1.4: Static Type Checker
# ---------------------------------------------------------------------------

class TypeChecker:
    """Checks types (i32 and bool), variable scopes, loop break placement, and function signatures."""

    def __init__(self) -> None:
        self.funcs: Dict[str, Tuple[List[str], Optional[str]]] = {}
        self.vars: Dict[str, str] = {}
        self.ret_type: Optional[str] = None
        self.loop_depth: int = 0

    def fail(self, line: int, msg: str) -> None:
        raise Error(f"line {line}: {msg}")

    def check_program(self, prog: Program) -> Program:
        if not prog.funcs:
            self.fail(1, "program has no functions")
        for fn in prog.funcs:
            if fn.name in self.funcs or fn.name in BUILTINS:
                self.fail(fn.line, f"function '{fn.name}' is defined twice")
            self.check_function(fn)
        return prog

    def check_function(self, fn: FuncDecl) -> None:
        self.vars = {}
        param_types: List[str] = []
        for p in fn.params:
            if p.name in self.vars:
                self.fail(p.line, f"the parameter '{p.name}' is declared twice")
            self.vars[p.name] = p.typ
            param_types.append(p.typ)
        # Register signature before checking body so recursive calls are type-checked.
        self.funcs[fn.name] = (param_types, fn.ret_type)
        self.ret_type = fn.ret_type
        self.loop_depth = 0
        self.check_block(fn.body, stmt_line=fn.line)
        if fn.ret_type is not None and not self.block_always_returns(fn.body):
            self.fail(fn.line, f"function '{fn.name}' may end without returning {fn.ret_type}")

    def block_always_returns(self, stmts: List[Stmt]) -> bool:
        for s in stmts:
            if isinstance(s, ReturnStmt):
                return True
            if isinstance(s, IfStmt) and s.else_body is not None:
                if all(self.block_always_returns(b.body) for b in s.branches) and self.block_always_returns(s.else_body):
                    return True
        return False

    def check_block(self, stmts: List[Stmt], stmt_line: int) -> None:
        saved = dict(self.vars)
        for s in stmts:
            self.check_stmt(s)
        self.vars = saved

    def check_stmt(self, s: Stmt) -> None:
        if isinstance(s, LocalStmt):
            et = self.check_expr(s.expr, s.line)
            if s.decl_type is not None and s.decl_type != et:
                self.fail(s.line, f"{et} assigned to '{s.name}' of {s.decl_type}")
            if s.name in self.vars:
                self.fail(s.line, f"'{s.name}' is declared twice")
            self.vars[s.name] = et
            return

        if isinstance(s, AssignStmt):
            if s.name not in self.vars:
                self.fail(s.line, f"'{s.name}' is not declared")
            vt = self.vars[s.name]
            et = self.check_expr(s.expr, s.line)
            if vt != et:
                self.fail(s.line, f"{et} assigned to '{s.name}' of {vt}")
            return

        if isinstance(s, IfStmt):
            for br in s.branches:
                ct = self.check_expr(br.cond, br.line)
                if ct != "bool":
                    self.fail(br.line, f"the test of an if/elseif is bool, not {ct}")
                self.check_block(br.body, br.line)
            if s.else_body is not None:
                self.check_block(s.else_body, s.line)
            return

        if isinstance(s, WhileStmt):
            ct = self.check_expr(s.cond, s.line)
            if ct != "bool":
                self.fail(s.line, f"the test of a while is bool, not {ct}")
            self.loop_depth += 1
            self.check_block(s.body, s.line)
            self.loop_depth -= 1
            return

        if isinstance(s, ForStmt):
            at = self.check_expr(s.start, s.line)
            bt = self.check_expr(s.stop, s.line)
            if at != "i32" or bt != "i32":
                self.fail(s.line, "the bounds of a for loop are i32")
            if s.step is not None:
                st = self.check_expr(s.step, s.line)
                if st != "i32":
                    self.fail(s.line, "the step of a for loop is i32")
            if s.var in self.vars:
                self.fail(s.line, f"'{s.var}' is declared twice")
            saved = dict(self.vars)
            self.vars[s.var] = "i32"
            self.loop_depth += 1
            for body_stmt in s.body:
                self.check_stmt(body_stmt)
            self.loop_depth -= 1
            self.vars = saved
            return

        if isinstance(s, ReturnStmt):
            if s.expr is None:
                if self.ret_type is not None:
                    self.fail(s.line, f"return needs a value of type {self.ret_type}")
                return
            et = self.check_expr(s.expr, s.line)
            if self.ret_type is None:
                self.fail(s.line, "this function has no return type")
            if et != self.ret_type:
                self.fail(s.line, f"return of {et} in a function of {self.ret_type}")
            return

        if isinstance(s, BreakStmt):
            if self.loop_depth == 0:
                self.fail(s.line, "'break' is outside any loop")
            return

        if isinstance(s, CallStmt):
            self.check_call(s.call, s.line, need_value=False)
            return

        self.fail(s.line, f"unknown statement {type(s).__name__}")

    def check_expr(self, e: Expr, stmt_line: int) -> str:
        if isinstance(e, IntLit):
            if e.value == 0x80000000:
                self.fail(stmt_line, "integer literal 2147483648 overflows i32 unless negated")
            e.typ = "i32"
            return "i32"

        if isinstance(e, BoolLit):
            e.typ = "bool"
            return "bool"

        if isinstance(e, VarExpr):
            if e.name not in self.vars:
                self.fail(stmt_line, f"'{e.name}' is not declared")
            e.typ = self.vars[e.name]
            return e.typ

        if isinstance(e, UnaryOp):
            if e.op == "-" and isinstance(e.operand, IntLit) and e.operand.value == 0x80000000:
                e.operand.typ = "i32"
                e.typ = "i32"
                return "i32"
            ot = self.check_expr(e.operand, stmt_line)
            if e.op == "-":
                if ot != "i32":
                    self.fail(stmt_line, f"unary '-' takes i32, not {ot}")
                e.typ = "i32"
                return "i32"
            if e.op == "not":
                if ot != "bool":
                    self.fail(stmt_line, f"'not' takes bool, not {ot}")
                e.typ = "bool"
                return "bool"

        if isinstance(e, BinOp):
            lt = self.check_expr(e.left, stmt_line)
            rt = self.check_expr(e.right, stmt_line)
            if e.op in ("and", "or"):
                if lt != "bool" or rt != "bool":
                    self.fail(stmt_line, f"'{e.op}' takes bool, not {lt} and {rt}")
                e.typ = "bool"
                return "bool"
            if e.op in ("+", "-", "*", "//", "%", "&"):
                if lt != "i32" or rt != "i32":
                    self.fail(stmt_line, f"'{e.op}' takes i32, not {lt} and {rt}")
                e.typ = "i32"
                return "i32"
            if e.op == "/":
                self.fail(stmt_line, "'/' gives a float in Lua; use '//' for integer division in stage 1")
            if e.op in ("<", "<=", ">", ">="):
                if lt != "i32" or rt != "i32":
                    self.fail(stmt_line, f"cannot compare {lt} with {rt}")
                e.typ = "bool"
                return "bool"
            if e.op in ("==", "~="):
                if lt != rt:
                    self.fail(stmt_line, f"cannot compare {lt} with {rt}")
                e.typ = "bool"
                return "bool"

        if isinstance(e, CallExpr):
            ret = self.check_call(e, stmt_line, need_value=True)
            assert ret is not None
            e.typ = ret
            return ret

        self.fail(stmt_line, f"invalid expression {type(e).__name__}")
        return ""

    def check_call(self, call: CallExpr, stmt_line: int, need_value: bool) -> Optional[str]:
        if call.callee in BUILTINS:
            ptypes, ret = BUILTINS[call.callee]
        elif call.callee in self.funcs:
            ptypes, ret = self.funcs[call.callee]
        else:
            self.fail(stmt_line, f"'{call.callee}' is not a defined function")
            return None

        if need_value and ret is None:
            self.fail(stmt_line, f"'{call.callee}' gives no value")
        if len(call.args) != len(ptypes):
            self.fail(stmt_line, f"'{call.callee}' takes {len(ptypes)} arguments, not {len(call.args)}")
        for arg, expected in zip(call.args, ptypes):
            actual = self.check_expr(arg, stmt_line)
            if actual != expected:
                self.fail(stmt_line, f"'{call.callee}' gets {actual} where {expected} is expected")
        call.typ = ret
        return ret


def parse_and_check(src: str) -> Program:
    """Runs Stage 1 (Lexer + Parser + TypeChecker) and returns the checked Program AST."""
    prog = Parser(src).parse()
    return TypeChecker().check_program(prog)


def to_lua(src: str) -> str:
    """Checks the BML source and strips type annotations to yield plain Lua."""
    prog = parse_and_check(src)
    lua = src
    for start, end in sorted(prog.cuts, reverse=True):
        lua = lua[:start] + lua[end:]
    return lua


# ---------------------------------------------------------------------------
# Stage 2.1: Stack Bytecode ISA and Compiler
# ---------------------------------------------------------------------------

OPCODES = (
    "PUSH_CONST",   # arg = constant index k; pushes K[k] (i32)
    "PUSH_TRUE",    # pushes True
    "PUSH_FALSE",   # pushes False
    "POP",          # discards top of stack
    "LOAD",         # arg = local slot index; pushes locals[slot]
    "STORE",        # arg = local slot index; pops TOS into locals[slot]
    "ADD",          # pops b, a; pushes a + b
    "SUB",          # pops b, a; pushes a - b
    "MUL",          # pops b, a; pushes a * b
    "IDIV",         # pops b, a; pushes floor(a / b) (0 if b == 0)
    "IMOD",         # pops b, a; pushes a - floor(a / b) * b (0 if b == 0)
    "BAND",         # pops b, a; pushes a & b (32-bit signed)
    "NEG",          # pops a; pushes -a
    "NOT",          # pops a; pushes not a
    "EQ",           # pops b, a; pushes a == b
    "NE",           # pops b, a; pushes a != b
    "LT",           # pops b, a; pushes a < b
    "LE",           # pops b, a; pushes a <= b
    "GT",           # pops b, a; pushes a > b
    "GE",           # pops b, a; pushes a >= b
    "ABS",          # pops a; pushes abs(a)
    "MIN",          # pops b, a; pushes min(a, b)
    "MAX",          # pops b, a; pushes max(a, b)
    "JMP",          # arg = target pc; pc = target
    "JZ",           # arg = target pc; pops cond; if not cond: pc = target
    "JF_KEEP",      # arg = target pc; if not TOS: pc = target (keeps False), else pops True
    "JT_KEEP",      # arg = target pc; if TOS: pc = target (keeps True), else pops False
    "CALL",         # arg = (fn_index << 8) | nargs; pops nargs, pushes return value (if any)
    "RET",          # pops return value and returns from frame
    "RET0",         # returns None from void function
)


@dataclass
class Insn:
    op: str
    arg: int = 0
    comment: str = ""


@dataclass
class CompiledFunc:
    name: str
    nparams: int
    nlocals: int
    param_types: List[str]
    ret_type: Optional[str]
    code: List[Insn]
    local_names: List[str]


@dataclass
class Module:
    funcs: List[CompiledFunc]
    consts: List[int]
    func_index: Dict[str, int]


class BytecodeCompiler:
    """Compiles a checked Program AST into a StackVM Module."""

    def __init__(self, prog: Program):
        self.prog = prog
        self.consts: List[int] = []
        self.const_map: Dict[int, int] = {}
        self.func_index: Dict[str, int] = {fn.name: idx for idx, fn in enumerate(prog.funcs)}
        self.func_ret: Dict[str, Optional[str]] = {fn.name: fn.ret_type for fn in prog.funcs}
        self.code: List[Insn] = []
        self.slots: Dict[str, int] = {}
        self.local_names: List[str] = []
        self.break_lists: List[List[int]] = []

    def add_const(self, val: int) -> int:
        if val not in self.const_map:
            self.const_map[val] = len(self.consts)
            self.consts.append(val)
        return self.const_map[val]

    def alloc_slot(self, name: str) -> int:
        slot = len(self.local_names)
        self.local_names.append(name)
        self.slots[name] = slot
        return slot

    def emit(self, op: str, arg: int = 0, comment: str = "") -> int:
        self.code.append(Insn(op, arg, comment))
        return len(self.code) - 1

    def here(self) -> int:
        return len(self.code)

    def patch(self, idx: int, target: int) -> None:
        self.code[idx].arg = target

    def compile(self) -> Module:
        compiled: List[CompiledFunc] = []
        for fn in self.prog.funcs:
            compiled.append(self.compile_func(fn))
        return Module(funcs=compiled, consts=self.consts, func_index=self.func_index)

    def compile_func(self, fn: FuncDecl) -> CompiledFunc:
        self.code = []
        self.slots = {}
        self.local_names = []
        self.break_lists = []
        for p in fn.params:
            self.alloc_slot(p.name)
        self.compile_block(fn.body)
        end_target = len(self.code)
        jumps_to_end = any(
            ins.op in ("JMP", "JZ", "JF_KEEP", "JT_KEEP") and ins.arg >= end_target
            for ins in self.code
        )
        if not self.code or self.code[-1].op not in ("RET", "RET0") or jumps_to_end:
            if fn.ret_type is None:
                self.emit("RET0")
            else:
                k = self.add_const(0)
                self.emit("PUSH_CONST", k)
                self.emit("RET")
        return CompiledFunc(
            name=fn.name,
            nparams=len(fn.params),
            nlocals=len(self.local_names),
            param_types=[p.typ for p in fn.params],
            ret_type=fn.ret_type,
            code=self.code,
            local_names=self.local_names,
        )

    def compile_block(self, stmts: List[Stmt]) -> None:
        saved = dict(self.slots)
        for s in stmts:
            self.compile_stmt(s)
        self.slots = saved

    def compile_stmt(self, s: Stmt) -> None:
        if isinstance(s, LocalStmt):
            self.compile_expr(s.expr)
            slot = self.alloc_slot(s.name)
            self.emit("STORE", slot, s.name)
            return

        if isinstance(s, AssignStmt):
            self.compile_expr(s.expr)
            slot = self.slots[s.name]
            self.emit("STORE", slot, s.name)
            return

        if isinstance(s, IfStmt):
            end_jumps: List[int] = []
            for br in s.branches:
                self.compile_expr(br.cond)
                jz = self.emit("JZ", 0)
                self.compile_block(br.body)
                end_jumps.append(self.emit("JMP", 0))
                self.patch(jz, self.here())
            if s.else_body is not None:
                self.compile_block(s.else_body)
            end_pc = self.here()
            for j in end_jumps:
                self.patch(j, end_pc)
            return

        if isinstance(s, WhileStmt):
            loop_start = self.here()
            self.compile_expr(s.cond)
            jz_exit = self.emit("JZ", 0)
            self.break_lists.append([])
            self.compile_block(s.body)
            self.emit("JMP", loop_start)
            loop_end = self.here()
            self.patch(jz_exit, loop_end)
            for brk in self.break_lists.pop():
                self.patch(brk, loop_end)
            return

        if isinstance(s, ForStmt):
            saved = dict(self.slots)
            self.compile_expr(s.start)
            slot_i = self.alloc_slot(s.var)
            self.emit("STORE", slot_i, s.var)

            self.compile_expr(s.stop)
            slot_stop = self.alloc_slot(f"{s.var}$stop")
            self.emit("STORE", slot_stop, f"{s.var}$stop")

            slot_step: Optional[int] = None
            if s.step is not None:
                self.compile_expr(s.step)
                slot_step = self.alloc_slot(f"{s.var}$step")
                self.emit("STORE", slot_step, f"{s.var}$step")

            loop_test = self.here()
            if slot_step is None:
                self.emit("LOAD", slot_i, s.var)
                self.emit("LOAD", slot_stop, f"{s.var}$stop")
                self.emit("LE")
                jz_exit = self.emit("JZ", 0)
            else:
                # Lua numeric for: if step >= 0 test i <= stop, else test i >= stop
                k0 = self.add_const(0)
                self.emit("LOAD", slot_step, f"{s.var}$step")
                self.emit("PUSH_CONST", k0, "0")
                self.emit("GE")
                jz_neg = self.emit("JZ", 0)
                self.emit("LOAD", slot_i, s.var)
                self.emit("LOAD", slot_stop, f"{s.var}$stop")
                self.emit("LE")
                jmp_tested = self.emit("JMP", 0)
                self.patch(jz_neg, self.here())
                self.emit("LOAD", slot_i, s.var)
                self.emit("LOAD", slot_stop, f"{s.var}$stop")
                self.emit("GE")
                self.patch(jmp_tested, self.here())
                jz_exit = self.emit("JZ", 0)

            self.break_lists.append([])
            for body_stmt in s.body:
                self.compile_stmt(body_stmt)

            self.emit("LOAD", slot_i, s.var)
            if slot_step is None:
                k1 = self.add_const(1)
                self.emit("PUSH_CONST", k1, "1")
            else:
                self.emit("LOAD", slot_step, f"{s.var}$step")
            self.emit("ADD")
            self.emit("STORE", slot_i, s.var)
            self.emit("JMP", loop_test)

            loop_end = self.here()
            self.patch(jz_exit, loop_end)
            for brk in self.break_lists.pop():
                self.patch(brk, loop_end)
            self.slots = saved
            return

        if isinstance(s, ReturnStmt):
            if s.expr is None:
                self.emit("RET0")
            else:
                self.compile_expr(s.expr)
                self.emit("RET")
            return

        if isinstance(s, BreakStmt):
            self.break_lists[-1].append(self.emit("JMP", 0))
            return

        if isinstance(s, CallStmt):
            self.compile_call(s.call)
            if s.call.typ is not None:
                self.emit("POP")
            return

    def compile_expr(self, e: Expr) -> None:
        if isinstance(e, IntLit):
            k = self.add_const(e.value)
            self.emit("PUSH_CONST", k, str(e.value))
            return

        if isinstance(e, BoolLit):
            self.emit("PUSH_TRUE" if e.value else "PUSH_FALSE")
            return

        if isinstance(e, VarExpr):
            slot = self.slots[e.name]
            self.emit("LOAD", slot, e.name)
            return

        if isinstance(e, UnaryOp):
            if e.op == "-" and isinstance(e.operand, IntLit) and e.operand.value == 0x80000000:
                k = self.add_const(-2147483648)
                self.emit("PUSH_CONST", k, "-2147483648")
                return
            self.compile_expr(e.operand)
            self.emit("NEG" if e.op == "-" else "NOT")
            return

        if isinstance(e, BinOp):
            if e.op == "and":
                self.compile_expr(e.left)
                jf = self.emit("JF_KEEP", 0)
                self.compile_expr(e.right)
                self.patch(jf, self.here())
                return
            if e.op == "or":
                self.compile_expr(e.left)
                jt = self.emit("JT_KEEP", 0)
                self.compile_expr(e.right)
                self.patch(jt, self.here())
                return
            self.compile_expr(e.left)
            self.compile_expr(e.right)
            op_map = {
                "+": "ADD", "-": "SUB", "*": "MUL", "//": "IDIV", "%": "IMOD", "&": "BAND",
                "==": "EQ", "~=": "NE", "<": "LT", "<=": "LE", ">": "GT", ">=": "GE",
            }
            self.emit(op_map[e.op])
            return

        if isinstance(e, CallExpr):
            self.compile_call(e)
            return

    def compile_call(self, call: CallExpr) -> None:
        for arg in call.args:
            self.compile_expr(arg)
        if call.callee in ("abs", "min", "max"):
            self.emit(call.callee.upper())
            return
        fn_idx = self.func_index[call.callee]
        packed = (fn_idx << 8) | (len(call.args) & 0xFF)
        self.emit("CALL", packed, f"{call.callee}/{len(call.args)}")


def compile_source(src: str) -> Module:
    """Compiles BML source code through Stage 1 and Stage 2 into a StackVM Module."""
    prog = parse_and_check(src)
    return BytecodeCompiler(prog).compile()


def disassemble(mod: Module) -> str:
    """Formats a compiled StackVM Module as human-readable bytecode."""
    lines: List[str] = []
    if mod.consts:
        consts_str = ", ".join(f"K[{i}]={v}" for i, v in enumerate(mod.consts))
        lines.append(f"; constants: {consts_str}")
    for idx, fn in enumerate(mod.funcs):
        ret_s = fn.ret_type or "void"
        params_s = ", ".join(f"{fn.local_names[i]}: {t}" for i, t in enumerate(fn.param_types))
        lines.append(f"\nfn #{idx} {fn.name}({params_s}) -> {ret_s}  [locals={fn.nlocals}]")
        for pc, ins in enumerate(fn.code):
            note = f"  ; {ins.comment}" if ins.comment else ""
            if ins.op in ("PUSH_CONST", "LOAD", "STORE", "JMP", "JZ", "JF_KEEP", "JT_KEEP"):
                lines.append(f"  {pc:04d}  {ins.op:<12} {ins.arg}{note}")
            elif ins.op == "CALL":
                lines.append(f"  {pc:04d}  {ins.op:<12} fn={ins.arg >> 8}, argc={ins.arg & 0xFF}{note}")
            else:
                lines.append(f"  {pc:04d}  {ins.op}{note}")
    return "\n".join(lines) + "\n"


# ---------------------------------------------------------------------------
# Stage 2.2: Stack Virtual Machine (Host)
# ---------------------------------------------------------------------------

def wrap_i32(x: int) -> int:
    """Wraps a Python integer to signed 32-bit two's complement [-2^31, 2^31 - 1]."""
    return ((x + 0x80000000) & 0xFFFFFFFF) - 0x80000000


def lua_idiv(a: int, b: int) -> int:
    """Floor division towards -infinity (matches Lua 5.4 '//' and bmt_idiv; 0 on b == 0)."""
    if b == 0:
        return 0
    return a // b


def lua_imod(a: int, b: int) -> int:
    """Floor modulo towards -infinity (matches Lua 5.4 '%' and bmt_imod; 0 on b == 0)."""
    if b == 0:
        return 0
    return a % b


class StackVM:
    """Executes a compiled BML Module on a stack machine."""

    def __init__(self, mod: Module, wrap32: bool = False, max_depth: int = 64):
        self.mod = mod
        self.wrap32 = wrap32
        self.max_depth = max_depth

    def _norm(self, x: int) -> int:
        return wrap_i32(x) if self.wrap32 else x

    def call(self, fn_name: str, args: List[Union[int, bool]], budget: int = 1_000_000) -> Optional[Union[int, bool]]:
        if fn_name not in self.mod.func_index:
            raise VMError(f"unknown function '{fn_name}'")
        fn_idx = self.mod.func_index[fn_name]
        fn = self.mod.funcs[fn_idx]
        if len(args) != fn.nparams:
            raise VMError(f"'{fn_name}' expects {fn.nparams} arguments, got {len(args)}")
        norm_args: List[Union[int, bool]] = []
        for a, pt in zip(args, fn.param_types):
            if pt == "bool":
                norm_args.append(bool(a))
            else:
                norm_args.append(self._norm(int(a)))
        val, _ = self._exec(fn, norm_args, depth=1, budget=budget)
        return val

    def _exec(
        self,
        fn: CompiledFunc,
        args: List[Union[int, bool]],
        depth: int,
        budget: int,
    ) -> Tuple[Optional[Union[int, bool]], int]:
        if depth > self.max_depth:
            raise VMError("call stack depth exceeded")
        locals_arr: List[Union[int, bool]] = [0] * fn.nlocals
        for i, v in enumerate(args):
            locals_arr[i] = v
        stack: List[Union[int, bool]] = []
        code = fn.code
        consts = self.mod.consts
        pc = 0
        n = len(code)

        while pc < n:
            if budget <= 0:
                raise VMError("step budget exhausted")
            budget -= 1
            ins = code[pc]
            op = ins.op
            pc += 1

            if op == "PUSH_CONST":
                stack.append(self._norm(consts[ins.arg]))
            elif op == "PUSH_TRUE":
                stack.append(True)
            elif op == "PUSH_FALSE":
                stack.append(False)
            elif op == "POP":
                stack.pop()
            elif op == "LOAD":
                stack.append(locals_arr[ins.arg])
            elif op == "STORE":
                locals_arr[ins.arg] = stack.pop()
            elif op == "ADD":
                b = int(stack.pop())
                a = int(stack.pop())
                stack.append(self._norm(a + b))
            elif op == "SUB":
                b = int(stack.pop())
                a = int(stack.pop())
                stack.append(self._norm(a - b))
            elif op == "MUL":
                b = int(stack.pop())
                a = int(stack.pop())
                stack.append(self._norm(a * b))
            elif op == "IDIV":
                b = int(stack.pop())
                a = int(stack.pop())
                stack.append(self._norm(lua_idiv(a, b)))
            elif op == "IMOD":
                b = int(stack.pop())
                a = int(stack.pop())
                stack.append(self._norm(lua_imod(a, b)))
            elif op == "BAND":
                b = int(stack.pop())
                a = int(stack.pop())
                stack.append(self._norm(a & b))
            elif op == "NEG":
                a = int(stack.pop())
                stack.append(self._norm(-a))
            elif op == "NOT":
                a = bool(stack.pop())
                stack.append(not a)
            elif op == "EQ":
                b = stack.pop()
                a = stack.pop()
                stack.append(a == b)
            elif op == "NE":
                b = stack.pop()
                a = stack.pop()
                stack.append(a != b)
            elif op == "LT":
                b = int(stack.pop())
                a = int(stack.pop())
                stack.append(a < b)
            elif op == "LE":
                b = int(stack.pop())
                a = int(stack.pop())
                stack.append(a <= b)
            elif op == "GT":
                b = int(stack.pop())
                a = int(stack.pop())
                stack.append(a > b)
            elif op == "GE":
                b = int(stack.pop())
                a = int(stack.pop())
                stack.append(a >= b)
            elif op == "ABS":
                a = int(stack.pop())
                stack.append(self._norm(abs(a)))
            elif op == "MIN":
                b = int(stack.pop())
                a = int(stack.pop())
                stack.append(a if a < b else b)
            elif op == "MAX":
                b = int(stack.pop())
                a = int(stack.pop())
                stack.append(a if a > b else b)
            elif op == "JMP":
                pc = ins.arg
            elif op == "JZ":
                if not stack.pop():
                    pc = ins.arg
            elif op == "JF_KEEP":
                if not stack[-1]:
                    pc = ins.arg
                else:
                    stack.pop()
            elif op == "JT_KEEP":
                if stack[-1]:
                    pc = ins.arg
                else:
                    stack.pop()
            elif op == "CALL":
                callee_idx = ins.arg >> 8
                argc = ins.arg & 0xFF
                callee = self.mod.funcs[callee_idx]
                call_args = [stack.pop() for _ in range(argc)]
                call_args.reverse()
                ret_val, budget = self._exec(callee, call_args, depth + 1, budget)
                if callee.ret_type is not None:
                    assert ret_val is not None
                    stack.append(ret_val)
            elif op == "RET":
                return stack.pop(), budget
            elif op == "RET0":
                return None, budget
            else:
                raise VMError(f"unknown opcode '{op}'")

        return None, budget


# ---------------------------------------------------------------------------
# Stage 4 Preview Helper: Verify and Simulate the Hand-Encoded ARMv6 Words of sum
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# Stage 3: Kernel VM Back End (bmvm register machine, src/bm/bmvm.h)
# ---------------------------------------------------------------------------

BMVM_MAGIC = 0x31564D42  # "BMV1"

BMVM_OPCODES = (
    "LK", "MOV",
    "ADDI", "SUBI", "MULI", "IDIV", "IMOD", "NEGI", "ANDI",
    "ADDF", "SUBF", "MULF", "DIVF", "FFDIV", "FFMOD", "NEGF",
    "I2F",
    "LTI", "LEI", "EQI", "NEI", "LTF", "LEF", "EQF", "NEF",
    "NOT",
    "FLOOR", "SQRT", "ABSI", "ABSF",
    "MINI", "MAXI", "MINF", "MAXF",
    "JMP", "JF", "JT",
    "JFLTI", "JFLEI", "JFEQI", "JFNEI", "JFLTF", "JFLEF", "JFEQF", "JFNEF",
    "FORI",
    "GETI", "GETF", "SETI", "SETF",
    "CALL", "RET", "RET0",
)
BMVM_OP = {name: idx for idx, name in enumerate(BMVM_OPCODES)}
BMVM_JUMPS = {"JMP", "JF", "JT", "FORI", "JFLTI", "JFLEI", "JFEQI", "JFNEI", "JFLTF", "JFLEF", "JFEQF", "JFNEF"}


def compute_stack_depths(code: List[Insn]) -> Tuple[List[int], int]:
    """Computes the static evaluation stack depth before each instruction in a Stage 2 CompiledFunc."""
    n = len(code)
    depths: List[Optional[int]] = [None] * n
    depths[0] = 0
    work = [0]
    max_sp = 0

    while work:
        pc = work.pop()
        sp = depths[pc]
        assert sp is not None
        if sp > max_sp:
            max_sp = sp
        ins = code[pc]
        op = ins.op

        def reach(target: int, next_sp: int) -> None:
            if target < 0 or target >= n:
                return
            if depths[target] is None:
                depths[target] = next_sp
                work.append(target)
            elif depths[target] != next_sp:
                raise Error(f"internal: stack depth mismatch at pc={target}: {depths[target]} vs {next_sp}")

        if op in ("PUSH_CONST", "PUSH_TRUE", "PUSH_FALSE", "LOAD"):
            if sp + 1 > max_sp:
                max_sp = sp + 1
            reach(pc + 1, sp + 1)
        elif op in ("STORE", "POP"):
            reach(pc + 1, sp - 1)
        elif op in ("NEG", "NOT", "ABS"):
            reach(pc + 1, sp)
        elif op in ("ADD", "SUB", "MUL", "IDIV", "IMOD", "BAND", "EQ", "NE", "LT", "LE", "GT", "GE", "MIN", "MAX"):
            reach(pc + 1, sp - 1)
        elif op == "JMP":
            reach(ins.arg, sp)
        elif op == "JZ":
            reach(ins.arg, sp - 1)
            reach(pc + 1, sp - 1)
        elif op in ("JF_KEEP", "JT_KEEP"):
            reach(ins.arg, sp)
            reach(pc + 1, sp - 1)
        elif op == "CALL":
            argc = ins.arg & 0xFF
            # Note: all BML Stage 1/2 functions return a value (or CALLStmt pops it if non-void)
            reach(pc + 1, sp - argc + 1)
        elif op in ("RET", "RET0"):
            pass
        else:
            raise Error(f"internal: unknown stack op {op}")

    resolved = [d if d is not None else 0 for d in depths]
    return resolved, max_sp


class BMVMCompiler:
    """Lowers a Stage 2 Module into a binary BMV1 module for the kernel's bmvm (src/bm/bmvm.h)."""

    def __init__(self, mod: Module):
        self.mod = mod
        self.consts_bytes: List[bytes] = []
        self.fns_meta: List[Tuple[str, int, int, int, int, bytes, List[Tuple[int, int, int, int, int]], int]] = []

    def compile_blob(self) -> bytes:
        for fn in self.mod.funcs:
            self._compile_fn(fn)

        total_code = sum(len(f[6]) for f in self.fns_meta)
        hdr = struct.pack("<IHHI", BMVM_MAGIC, len(self.fns_meta), len(self.consts_bytes), total_code)
        ftab = bytearray()
        code_bytes = bytearray()
        base_pc = 0

        for name, nparams, nregs, ret_code, nk, pt_bytes, insns, kfirst in self.fns_meta:
            ftab += struct.pack(
                "<24sBBBB16sIII",
                name.encode("utf-8")[:23],
                nparams,
                nregs,
                ret_code,
                nk,
                pt_bytes,
                base_pc,
                len(insns),
                kfirst,
            )
            for op, a, b, c, k in insns:
                if BMVM_OPCODES[op] in BMVM_JUMPS:
                    k += base_pc
                code_bytes += struct.pack("<BBBBi", op, a, b, c, k)
            base_pc += len(insns)

        return bytes(hdr) + bytes(ftab) + b"".join(self.consts_bytes) + bytes(code_bytes)

    def _compile_fn(self, fn: CompiledFunc) -> None:
        depths, max_sp = compute_stack_depths(fn.code)

        # Collect unique integer constants used by this function
        kfirst = len(self.consts_bytes)
        fn_kmap: Dict[int, int] = {}
        fn_klist: List[int] = []

        def get_k_idx(val: int) -> int:
            wval = wrap_i32(val)
            if wval not in fn_kmap:
                fn_kmap[wval] = len(fn_klist)
                fn_klist.append(wval)
                self.consts_bytes.append(struct.pack("<iI", wval, 0))
            return fn_kmap[wval]

        # Pre-scan constants so nconst is fixed before assigning register numbers
        for ins in fn.code:
            if ins.op == "PUSH_CONST":
                get_k_idx(self.mod.consts[ins.arg])
            elif ins.op == "PUSH_TRUE":
                get_k_idx(1)
            elif ins.op == "PUSH_FALSE":
                get_k_idx(0)

        nparams = fn.nparams
        nconst = len(fn_klist)
        nextra_locals = fn.nlocals - nparams

        def reg_local(slot: int) -> int:
            if slot < nparams:
                return slot
            return nparams + nconst + (slot - nparams)

        def reg_const(val: int) -> int:
            return nparams + get_k_idx(val)

        def reg_stack(sp_idx: int) -> int:
            return nparams + nconst + nextra_locals + sp_idx

        nregs = max(1, nparams + nconst + nextra_locals + max(1, max_sp + 1))
        if nregs > 250:
            raise Error(f"function '{fn.name}' needs too many bmvm registers ({nregs})")

        insns: List[Tuple[int, int, int, int, int]] = []
        for pc, ins in enumerate(fn.code):
            sp = depths[pc]
            op = ins.op
            if op == "PUSH_CONST":
                cval = self.mod.consts[ins.arg]
                insns.append((BMVM_OP["MOV"], reg_stack(sp), reg_const(cval), 0, 0))
            elif op == "PUSH_TRUE":
                insns.append((BMVM_OP["MOV"], reg_stack(sp), reg_const(1), 0, 0))
            elif op == "PUSH_FALSE":
                insns.append((BMVM_OP["MOV"], reg_stack(sp), reg_const(0), 0, 0))
            elif op == "POP":
                insns.append((BMVM_OP["MOV"], 0, 0, 0, 0))
            elif op == "LOAD":
                insns.append((BMVM_OP["MOV"], reg_stack(sp), reg_local(ins.arg), 0, 0))
            elif op == "STORE":
                insns.append((BMVM_OP["MOV"], reg_local(ins.arg), reg_stack(sp - 1), 0, 0))
            elif op in ("ADD", "SUB", "MUL", "IDIV", "IMOD", "BAND", "EQ", "NE", "LT", "LE", "MIN", "MAX"):
                bm_op = {
                    "ADD": "ADDI", "SUB": "SUBI", "MUL": "MULI", "IDIV": "IDIV", "IMOD": "IMOD",
                    "BAND": "ANDI", "EQ": "EQI", "NE": "NEI", "LT": "LTI", "LE": "LEI",
                    "MIN": "MINI", "MAX": "MAXI",
                }[op]
                ra = reg_stack(sp - 2)
                rb = reg_stack(sp - 2)
                rc = reg_stack(sp - 1)
                insns.append((BMVM_OP[bm_op], ra, rb, rc, 0))
            elif op == "GT":
                ra = reg_stack(sp - 2)
                insns.append((BMVM_OP["LTI"], ra, reg_stack(sp - 1), reg_stack(sp - 2), 0))
            elif op == "GE":
                ra = reg_stack(sp - 2)
                insns.append((BMVM_OP["LEI"], ra, reg_stack(sp - 1), reg_stack(sp - 2), 0))
            elif op in ("NEG", "NOT", "ABS"):
                bm_op = {"NEG": "NEGI", "NOT": "NOT", "ABS": "ABSI"}[op]
                r = reg_stack(sp - 1)
                insns.append((BMVM_OP[bm_op], r, r, 0, 0))
            elif op == "JMP":
                insns.append((BMVM_OP["JMP"], 0, 0, 0, ins.arg))
            elif op == "JZ":
                insns.append((BMVM_OP["JF"], reg_stack(sp - 1), 0, 0, ins.arg))
            elif op == "JF_KEEP":
                insns.append((BMVM_OP["JF"], reg_stack(sp - 1), 0, 0, ins.arg))
            elif op == "JT_KEEP":
                insns.append((BMVM_OP["JT"], reg_stack(sp - 1), 0, 0, ins.arg))
            elif op == "CALL":
                fn_idx = ins.arg >> 8
                argc = ins.arg & 0xFF
                base_reg = reg_stack(sp - argc)
                insns.append((BMVM_OP["CALL"], base_reg, argc, 0, fn_idx))
            elif op == "RET":
                insns.append((BMVM_OP["RET"], reg_stack(sp - 1), 0, 0, 0))
            elif op == "RET0":
                insns.append((BMVM_OP["RET0"], 0, 0, 0, 0))

        pt_bytes = bytes(0 for _ in range(nparams)) + bytes(16 - nparams)
        ret_code = 4 if fn.ret_type is None else 0
        self.fns_meta.append((fn.name, nparams, nregs, ret_code, nconst, pt_bytes, insns, kfirst))


def compile_bmvm(src: str) -> bytes:
    """Compiles BML source code into a binary BMV1 blob for src/bm/bmvm.c."""
    mod = compile_source(src)
    return BMVMCompiler(mod).compile_blob()


# ---------------------------------------------------------------------------
# Stage 4: ARMv6 (A32, AAPCS) Native Machine-Code Back End
# ---------------------------------------------------------------------------

SUM_ARMV6_WORDS = (
    0xE3A01000,  # 0x00: mov r1, #0          (s = 0)
    0xE3A02001,  # 0x04: mov r2, #1          (i = 1)
    0xE1520000,  # 0x08: cmp r2, r0          (cmp i, n)
    0xCA000002,  # 0x0c: bgt 0x1c            (if i > n goto return)
    0xE0811002,  # 0x10: add r1, r1, r2      (s = s + i)
    0xE2822001,  # 0x14: add r2, r2, #1      (i = i + 1)
    0xEAFFFFFA,  # 0x18: b   0x08            (goto loop test)
    0xE1A00001,  # 0x1c: mov r0, r1          (return s in r0)
    0xE12FFF1E,  # 0x20: bx  lr              (return)
)

COND_EQ = 0x0
COND_NE = 0x1
COND_CS = 0x2
COND_CC = 0x3
COND_MI = 0x4
COND_PL = 0x5
COND_GE = 0xA
COND_LT = 0xB
COND_GT = 0xC
COND_LE = 0xD
COND_AL = 0xE

DP_AND = 0x0
DP_EOR = 0x1
DP_SUB = 0x2
DP_RSB = 0x3
DP_ADD = 0x4
DP_CMP = 0xA
DP_ORR = 0xC
DP_MOV = 0xD
DP_MVN = 0xF


def encode_imm12(val: int) -> Optional[int]:
    """Encodes a 32-bit unsigned value as an ARMv6 rotated 8-bit immediate (rot4 << 8 | imm8), or None."""
    u = val & 0xFFFFFFFF
    for rot in range(16):
        shift = rot * 2
        rotated = ((u << shift) | (u >> (32 - shift))) & 0xFFFFFFFF if shift else u
        if rotated <= 0xFF:
            return (rot << 8) | rotated
    return None


class ARMv6Assembler:
    """Assembler for 32-bit ARMv6 (A32) machine instructions with symbolic label resolution."""

    def __init__(self) -> None:
        self.words: List[int] = []
        self.comments: Dict[int, str] = {}
        self.labels: Dict[str, int] = {}
        self.patches: List[Tuple[int, str, int, bool]] = []  # (word_idx, label, cond, is_bl)

    def here(self) -> int:
        return len(self.words)

    def bind(self, label: str) -> None:
        self.labels[label] = len(self.words)

    def emit_word(self, w: int, comment: str = "") -> int:
        idx = len(self.words)
        if comment:
            self.comments[idx] = comment
        self.words.append(w & 0xFFFFFFFF)
        return idx

    def dp_reg(
        self,
        op: int,
        rd: int,
        rn: int,
        rm: int,
        cond: int = COND_AL,
        s: int = 0,
        shift_type: int = 0,
        shift_imm: int = 0,
        comment: str = "",
    ) -> int:
        op2 = ((shift_imm & 31) << 7) | ((shift_type & 3) << 5) | (rm & 0xF)
        w = (cond << 28) | (op << 21) | (s << 20) | ((rn & 0xF) << 16) | ((rd & 0xF) << 12) | op2
        return self.emit_word(w, comment)

    def dp_imm(
        self,
        op: int,
        rd: int,
        rn: int,
        imm12: int,
        cond: int = COND_AL,
        s: int = 0,
        comment: str = "",
    ) -> int:
        w = (cond << 28) | (1 << 25) | (op << 21) | (s << 20) | ((rn & 0xF) << 16) | ((rd & 0xF) << 12) | (imm12 & 0xFFF)
        return self.emit_word(w, comment)

    def mov_reg(self, rd: int, rm: int, cond: int = COND_AL, comment: str = "") -> int:
        return self.dp_reg(DP_MOV, rd, 0, rm, cond=cond, comment=comment)

    def cmp_reg(self, rn: int, rm: int, cond: int = COND_AL, comment: str = "") -> int:
        return self.dp_reg(DP_CMP, 0, rn, rm, cond=cond, s=1, comment=comment)

    def cmp_imm(self, rn: int, imm8: int, cond: int = COND_AL, comment: str = "") -> int:
        return self.dp_imm(DP_CMP, 0, rn, imm8 & 0xFF, cond=cond, s=1, comment=comment)

    def add_reg(self, rd: int, rn: int, rm: int, cond: int = COND_AL, comment: str = "") -> int:
        return self.dp_reg(DP_ADD, rd, rn, rm, cond=cond, comment=comment)

    def sub_reg(self, rd: int, rn: int, rm: int, cond: int = COND_AL, comment: str = "") -> int:
        return self.dp_reg(DP_SUB, rd, rn, rm, cond=cond, comment=comment)

    def and_reg(self, rd: int, rn: int, rm: int, cond: int = COND_AL, comment: str = "") -> int:
        return self.dp_reg(DP_AND, rd, rn, rm, cond=cond, comment=comment)

    def eor_reg(self, rd: int, rn: int, rm: int, cond: int = COND_AL, comment: str = "") -> int:
        return self.dp_reg(DP_EOR, rd, rn, rm, cond=cond, comment=comment)

    def rsb_imm(self, rd: int, rn: int, imm8: int = 0, cond: int = COND_AL, comment: str = "") -> int:
        return self.dp_imm(DP_RSB, rd, rn, imm8 & 0xFF, cond=cond, comment=comment)

    def mvn_reg(self, rd: int, rm: int, cond: int = COND_AL, comment: str = "") -> int:
        return self.dp_reg(DP_MVN, rd, 0, rm, cond=cond, comment=comment)

    def mul(self, rd: int, rm: int, rs: int, cond: int = COND_AL, comment: str = "") -> int:
        # On pre-ARMv6 cores rd != rm is required; if rd == rm we swap rm and rs or copy via r12
        if rd == rm and rd != rs:
            rm, rs = rs, rm
        w = (cond << 28) | ((rd & 0xF) << 16) | ((rs & 0xF) << 8) | 0x90 | (rm & 0xF)
        return self.emit_word(w, comment)

    def mov32(self, rd: int, val: int, comment: str = "") -> None:
        """Loads any signed/unsigned 32-bit integer into rd using 1..4 A32 instructions."""
        u = val & 0xFFFFFFFF
        enc = encode_imm12(u)
        if enc is not None:
            self.dp_imm(DP_MOV, rd, 0, enc, comment=comment or f"mov r{rd}, #{val}")
            return
        inv = (~u) & 0xFFFFFFFF
        enc_inv = encode_imm12(inv)
        if enc_inv is not None:
            self.dp_imm(DP_MVN, rd, 0, enc_inv, comment=comment or f"mvn r{rd}, #{inv}")
            return
        # Decompose into up to four 8-bit chunks
        first = True
        for byte_idx in range(4):
            b = (u >> (byte_idx * 8)) & 0xFF
            if b == 0 and not (byte_idx == 3 and first):
                continue
            rot = (16 - byte_idx * 4) & 0xF
            imm12 = (rot << 8) | b
            if first:
                self.dp_imm(DP_MOV, rd, 0, imm12, comment=comment if first else "")
                first = False
            else:
                self.dp_imm(DP_ORR, rd, rd, imm12)

    def ldr(self, rd: int, rn: int, offset: int = 0, cond: int = COND_AL, comment: str = "") -> int:
        u_bit = 1 if offset >= 0 else 0
        off = abs(offset)
        assert off <= 0xFFF, f"LDR offset out of 12-bit range: {offset}"
        w = (cond << 28) | (0b01010001 << 20) | (u_bit << 23) | ((rn & 0xF) << 16) | ((rd & 0xF) << 12) | off
        return self.emit_word(w, comment)

    def str_w(self, rd: int, rn: int, offset: int = 0, cond: int = COND_AL, comment: str = "") -> int:
        u_bit = 1 if offset >= 0 else 0
        off = abs(offset)
        assert off <= 0xFFF, f"STR offset out of 12-bit range: {offset}"
        w = (cond << 28) | (0b01010000 << 20) | (u_bit << 23) | ((rn & 0xF) << 16) | ((rd & 0xF) << 12) | off
        return self.emit_word(w, comment)

    def push(self, regmask: int, cond: int = COND_AL, comment: str = "") -> int:
        w = (cond << 28) | 0x092D0000 | (regmask & 0xFFFF)
        return self.emit_word(w, comment)

    def pop(self, regmask: int, cond: int = COND_AL, comment: str = "") -> int:
        w = (cond << 28) | 0x08BD0000 | (regmask & 0xFFFF)
        return self.emit_word(w, comment)

    def bx(self, rm: int = 14, cond: int = COND_AL, comment: str = "") -> int:
        w = (cond << 28) | 0x012FFF10 | (rm & 0xF)
        return self.emit_word(w, comment)

    def b(self, label: str, cond: int = COND_AL, comment: str = "") -> int:
        idx = self.emit_word(0, comment or f"b {label}")
        self.patches.append((idx, label, cond, False))
        return idx

    def bl(self, label: str, cond: int = COND_AL, comment: str = "") -> int:
        idx = self.emit_word(0, comment or f"bl {label}")
        self.patches.append((idx, label, cond, True))
        return idx

    def finalize(self) -> List[int]:
        for idx, label, cond, is_bl in self.patches:
            if label not in self.labels:
                raise Error(f"internal: undefined ARMv6 label '{label}'")
            target = self.labels[label]
            disp = (target - (idx + 2)) & 0xFFFFFF
            w = (cond << 28) | (0b101 << 25) | ((1 if is_bl else 0) << 24) | disp
            self.words[idx] = w & 0xFFFFFFFF
        return self.words


def is_canonical_sum(fn: FuncDecl) -> bool:
    """Checks if fn is the exact canonical sum(n: i32): i32 function for the 9-word leaf encoding."""
    if len(fn.params) != 1 or fn.params[0].typ != "i32" or fn.ret_type != "i32":
        return False
    if len(fn.body) != 3:
        return False
    s0, s1, s2 = fn.body
    if not (isinstance(s0, LocalStmt) and isinstance(s0.expr, IntLit) and s0.expr.value == 0):
        return False
    acc = s0.name
    if not (isinstance(s1, ForStmt) and s1.step is None and len(s1.body) == 1):
        return False
    if not (isinstance(s1.start, IntLit) and s1.start.value == 1):
        return False
    if not (isinstance(s1.stop, VarExpr) and s1.stop.name == fn.params[0].name):
        return False
    b0 = s1.body[0]
    if not (isinstance(b0, AssignStmt) and b0.name == acc and isinstance(b0.expr, BinOp) and b0.expr.op == "+"):
        return False
    if not (isinstance(b0.expr.left, VarExpr) and b0.expr.left.name == acc):
        return False
    if not (isinstance(b0.expr.right, VarExpr) and b0.expr.right.name == s1.var):
        return False
    if not (isinstance(s2, ReturnStmt) and isinstance(s2.expr, VarExpr) and s2.expr.name == acc):
        return False
    return True


@dataclass
class ARMv6Module:
    words: List[int]
    func_offsets: Dict[str, int]  # byte offset of each function from the start of words
    comments: Dict[int, str]


class ARMv6Compiler:
    """Compiles a checked BML Program into 32-bit ARMv6 (A32, AAPCS) machine words."""

    def __init__(self, prog: Program, mod: Module, leaf_opt: bool = True):
        self.prog = prog
        self.mod = mod
        self.leaf_opt = leaf_opt
        self.asm = ARMv6Assembler()
        self.func_offsets: Dict[str, int] = {}
        self.needs_idivmod = False

    def compile(self) -> ARMv6Module:
        for fn_ast, fn_ir in zip(self.prog.funcs, self.mod.funcs):
            self.func_offsets[fn_ir.name] = self.asm.here() * 4
            self.asm.bind(f"fn_{fn_ir.name}")
            if self.leaf_opt and is_canonical_sum(fn_ast) and len(self.prog.funcs) == 1:
                for w in SUM_ARMV6_WORDS:
                    self.asm.emit_word(w)
            else:
                self._compile_general_fn(fn_ir)

        if self.needs_idivmod:
            self._emit_idivmod_helper()

        self.asm.finalize()
        return ARMv6Module(words=self.asm.words, func_offsets=self.func_offsets, comments=self.asm.comments)

    def _emit_idivmod_helper(self) -> None:
        """Emits __bml_idivmod: inputs r0 = a, r1 = b; outputs r0 = floor(a / b), r1 = a % b (0 if b == 0)."""
        a = self.asm
        a.bind("__bml_idivmod")
        # Save r4, r5, lr (plus r3 for 8-byte stack alignment)
        a.push((1 << 4) | (1 << 5) | (1 << 14), comment="__bml_idivmod: push {r4, r5, lr}")
        a.cmp_imm(1, 0, comment="cmp r1, #0")
        a.b("__idiv_zero", cond=COND_EQ)

        a.mov_reg(4, 0, comment="r4 = a")
        a.mov_reg(5, 1, comment="r5 = b")
        a.eor_reg(1, 4, 5, comment="r1 = a ^ b (sign diff in bit 31)")

        # r2 = |a| (unsigned), r3 = |b| (unsigned)
        a.mov_reg(2, 4)
        a.cmp_imm(2, 0)
        a.rsb_imm(2, 2, 0, cond=COND_LT, comment="r2 = |a|")

        a.mov_reg(3, 5)
        a.cmp_imm(3, 0)
        a.rsb_imm(3, 3, 0, cond=COND_LT, comment="r3 = |b|")

        a.dp_imm(DP_MOV, 0, 0, 0, comment="r0 = 0 (quotient)")
        a.dp_imm(DP_MOV, 12, 0, 1, comment="r12 = 1 (bit mask)")

        a.bind("__idiv_shift")
        a.cmp_reg(3, 2)
        a.b("__idiv_loop", cond=COND_CS)
        a.cmp_imm(3, 0)
        a.b("__idiv_loop", cond=COND_MI)
        a.dp_reg(DP_MOV, 3, 0, 3, shift_type=0, shift_imm=1, comment="lsl r3, r3, #1")
        a.dp_reg(DP_MOV, 12, 0, 12, shift_type=0, shift_imm=1, comment="lsl r12, r12, #1")
        a.b("__idiv_shift")

        a.bind("__idiv_loop")
        a.cmp_reg(2, 3)
        a.sub_reg(2, 2, 3, cond=COND_CS, comment="subcs r2, r2, r3")
        a.add_reg(0, 0, 12, cond=COND_CS, comment="addcs r0, r0, r12")
        a.dp_reg(DP_MOV, 3, 0, 3, shift_type=1, shift_imm=1, comment="lsr r3, r3, #1")
        a.dp_reg(DP_MOV, 12, 0, 12, s=1, shift_type=1, shift_imm=1, comment="lsrs r12, r12, #1")
        a.b("__idiv_loop", cond=COND_NE)

        # Apply floor rounding toward -infinity when (a ^ b) < 0
        a.cmp_imm(1, 0)
        a.b("__idiv_same_sign", cond=COND_PL)
        a.cmp_imm(2, 0)
        a.rsb_imm(0, 0, 0, cond=COND_EQ, comment="exact negative quotient: q = -q")
        a.mvn_reg(0, 0, cond=COND_NE, comment="inexact negative quotient: q = -q - 1 = ~q")

        a.bind("__idiv_same_sign")
        a.mul(12, 0, 5, comment="r12 = q * b")
        a.sub_reg(1, 4, 12, comment="r1 = a - q * b (modulo)")
        a.pop((1 << 4) | (1 << 5) | (1 << 15), comment="pop {r4, r5, pc}")

        a.bind("__idiv_zero")
        a.dp_imm(DP_MOV, 0, 0, 0)
        a.dp_imm(DP_MOV, 1, 0, 0)
        a.pop((1 << 4) | (1 << 5) | (1 << 15))

    def _compile_general_fn(self, fn: CompiledFunc) -> None:
        """Compiles a general function using r4..r10 for the first 7 slots and [fp, #-off] for spills."""
        a = self.asm
        depths, max_sp = compute_stack_depths(fn.code)
        total_slots = fn.nlocals + max_sp + 1
        spill_slots = max(0, total_slots - 7)
        # Keep stack 8-byte aligned: push {r4-r10, fp, lr} pushes 9 registers (36 bytes),
        # so make frame_bytes % 8 == 4 so (36 + frame_bytes) is a multiple of 8.
        frame_words = spill_slots
        if (9 + frame_words) % 2 != 0:
            frame_words += 1
        frame_bytes = frame_words * 4

        # Prologue: push {r4-r10, fp, lr}; mov fp, sp; sub sp, sp, #frame_bytes
        callee_mask = (0x7F << 4) | (1 << 11) | (1 << 14)  # r4..r10, fp(r11), lr(r14)
        a.push(callee_mask, comment=f"{fn.name}: push {{r4-r10, fp, lr}}")
        a.mov_reg(11, 13, comment="mov fp, sp")
        if frame_bytes > 0:
            a.mov32(12, frame_bytes)
            a.sub_reg(13, 13, 12, comment=f"sub sp, sp, #{frame_bytes}")

        def get_slot(slot: int, dst_reg: int) -> int:
            if slot < 7:
                return 4 + slot
            off = -4 * (slot - 6)
            a.ldr(dst_reg, 11, off)
            return dst_reg

        def put_slot(slot: int, src_reg: int) -> None:
            if slot < 7:
                target_reg = 4 + slot
                if src_reg != target_reg:
                    a.mov_reg(target_reg, src_reg)
            else:
                off = -4 * (slot - 6)
                a.str_w(src_reg, 11, off)

        def s_slot(sp_idx: int) -> int:
            return fn.nlocals + sp_idx

        # Move incoming parameters (r0..r3, and caller stack [fp, #36 + 4*(i-4)] for i >= 4) into local slots 0..nparams-1
        for i in range(fn.nparams):
            if i < 4:
                put_slot(i, i)
            else:
                caller_off = 36 + 4 * (i - 4)
                a.ldr(0, 11, caller_off)
                put_slot(i, 0)

        epilogue_label = f"__epilogue_{fn.name}"

        for pc, ins in enumerate(fn.code):
            a.bind(f"L_{fn.name}_{pc}")
            sp = depths[pc]
            op = ins.op

            if op == "PUSH_CONST":
                val = self.mod.consts[ins.arg]
                dst = 4 + s_slot(sp) if s_slot(sp) < 7 else 0
                a.mov32(dst, val)
                put_slot(s_slot(sp), dst)
            elif op == "PUSH_TRUE":
                dst = 4 + s_slot(sp) if s_slot(sp) < 7 else 0
                a.dp_imm(DP_MOV, dst, 0, 1)
                put_slot(s_slot(sp), dst)
            elif op == "PUSH_FALSE":
                dst = 4 + s_slot(sp) if s_slot(sp) < 7 else 0
                a.dp_imm(DP_MOV, dst, 0, 0)
                put_slot(s_slot(sp), dst)
            elif op == "POP":
                pass
            elif op == "LOAD":
                r = get_slot(ins.arg, 0)
                put_slot(s_slot(sp), r)
            elif op == "STORE":
                r = get_slot(s_slot(sp - 1), 0)
                put_slot(ins.arg, r)
            elif op in ("ADD", "SUB", "BAND"):
                ra = get_slot(s_slot(sp - 2), 0)
                rb = get_slot(s_slot(sp - 1), 1)
                dst = 4 + s_slot(sp - 2) if s_slot(sp - 2) < 7 else 0
                if op == "ADD":
                    a.add_reg(dst, ra, rb)
                elif op == "SUB":
                    a.sub_reg(dst, ra, rb)
                else:
                    a.and_reg(dst, ra, rb)
                put_slot(s_slot(sp - 2), dst)
            elif op == "MUL":
                ra = get_slot(s_slot(sp - 2), 0)
                rb = get_slot(s_slot(sp - 1), 1)
                a.mov_reg(12, ra)
                a.mul(0, 12, rb)
                put_slot(s_slot(sp - 2), 0)
            elif op in ("IDIV", "IMOD"):
                self.needs_idivmod = True
                ra = get_slot(s_slot(sp - 2), 0)
                if ra != 0:
                    a.mov_reg(0, ra)
                rb = get_slot(s_slot(sp - 1), 1)
                if rb != 1:
                    a.mov_reg(1, rb)
                a.bl("__bml_idivmod")
                put_slot(s_slot(sp - 2), 0 if op == "IDIV" else 1)
            elif op == "NEG":
                r = get_slot(s_slot(sp - 1), 0)
                dst = 4 + s_slot(sp - 1) if s_slot(sp - 1) < 7 else 0
                a.rsb_imm(dst, r, 0)
                put_slot(s_slot(sp - 1), dst)
            elif op == "NOT":
                r = get_slot(s_slot(sp - 1), 0)
                dst = 4 + s_slot(sp - 1) if s_slot(sp - 1) < 7 else 0
                a.cmp_imm(r, 0)
                a.dp_imm(DP_MOV, dst, 0, 1, cond=COND_EQ)
                a.dp_imm(DP_MOV, dst, 0, 0, cond=COND_NE)
                put_slot(s_slot(sp - 1), dst)
            elif op == "ABS":
                r = get_slot(s_slot(sp - 1), 0)
                dst = 4 + s_slot(sp - 1) if s_slot(sp - 1) < 7 else 0
                if dst != r:
                    a.mov_reg(dst, r)
                a.cmp_imm(dst, 0)
                a.rsb_imm(dst, dst, 0, cond=COND_LT)
                put_slot(s_slot(sp - 1), dst)
            elif op in ("MIN", "MAX"):
                ra = get_slot(s_slot(sp - 2), 0)
                rb = get_slot(s_slot(sp - 1), 1)
                a.cmp_reg(ra, rb)
                dst = 4 + s_slot(sp - 2) if s_slot(sp - 2) < 7 else 0
                if dst != ra:
                    a.mov_reg(dst, ra)
                cond_pick_b = COND_GT if op == "MIN" else COND_LT
                a.mov_reg(dst, rb, cond=cond_pick_b)
                put_slot(s_slot(sp - 2), dst)
            elif op in ("EQ", "NE", "LT", "LE", "GT", "GE"):
                ra = get_slot(s_slot(sp - 2), 0)
                rb = get_slot(s_slot(sp - 1), 1)
                a.cmp_reg(ra, rb)
                cond_true = {
                    "EQ": COND_EQ, "NE": COND_NE, "LT": COND_LT,
                    "LE": COND_LE, "GT": COND_GT, "GE": COND_GE,
                }[op]
                dst = 4 + s_slot(sp - 2) if s_slot(sp - 2) < 7 else 0
                a.dp_imm(DP_MOV, dst, 0, 0)
                a.dp_imm(DP_MOV, dst, 0, 1, cond=cond_true)
                put_slot(s_slot(sp - 2), dst)
            elif op == "JMP":
                a.b(f"L_{fn.name}_{ins.arg}")
            elif op in ("JZ", "JF_KEEP"):
                r = get_slot(s_slot(sp - 1), 0)
                a.cmp_imm(r, 0)
                a.b(f"L_{fn.name}_{ins.arg}", cond=COND_EQ)
            elif op == "JT_KEEP":
                r = get_slot(s_slot(sp - 1), 0)
                a.cmp_imm(r, 0)
                a.b(f"L_{fn.name}_{ins.arg}", cond=COND_NE)
            elif op == "CALL":
                callee_idx = ins.arg >> 8
                argc = ins.arg & 0xFF
                callee = self.mod.funcs[callee_idx]
                base = sp - argc
                stack_args = max(0, argc - 4)
                pad_words = 1 if (stack_args % 2 == 1) else 0
                if pad_words:
                    a.dp_imm(DP_SUB, 13, 13, 4)
                for j in range(argc - 1, 3, -1):
                    rj = get_slot(s_slot(base + j), 0)
                    a.push(1 << rj)
                for j in range(min(4, argc)):
                    rj = get_slot(s_slot(base + j), j)
                    if rj != j:
                        a.mov_reg(j, rj)
                a.bl(f"fn_{callee.name}")
                cleanup_bytes = (stack_args + pad_words) * 4
                if cleanup_bytes > 0:
                    a.dp_imm(DP_ADD, 13, 13, cleanup_bytes)
                if callee.ret_type is not None:
                    put_slot(s_slot(base), 0)
            elif op == "RET":
                r = get_slot(s_slot(sp - 1), 0)
                if r != 0:
                    a.mov_reg(0, r)
                a.b(epilogue_label)
            elif op == "RET0":
                a.b(epilogue_label)

        a.bind(epilogue_label)
        a.mov_reg(13, 11, comment="mov sp, fp")
        pop_mask = (0x7F << 4) | (1 << 11) | (1 << 15)  # r4..r10, fp(r11), pc(r15)
        a.pop(pop_mask, comment=f"{fn.name}: pop {{r4-r10, fp, pc}}")


def compile_armv6(src: str, leaf_opt: bool = True) -> ARMv6Module:
    """Compiles BML source code through Stages 1, 2, and 4 into an ARMv6Module."""
    prog = parse_and_check(src)
    mod = BytecodeCompiler(prog).compile()
    return ARMv6Compiler(prog, mod, leaf_opt=leaf_opt).compile()


def disassemble_armv6(arm_mod: ARMv6Module) -> str:
    """Formats an ARMv6Module as hex words with function boundaries and comments."""
    inv_fn = {off // 4: name for name, off in arm_mod.func_offsets.items()}
    lines: List[str] = []
    for idx, w in enumerate(arm_mod.words):
        if idx in inv_fn:
            lines.append(f"\n; fn {inv_fn[idx]} @ 0x{idx * 4:04x}")
        note = f"  ; {arm_mod.comments[idx]}" if idx in arm_mod.comments else ""
        lines.append(f"  0x{idx * 4:04x}: {w:08X}{note}")
    return "\n".join(lines) + "\n"


def run_armv6_words(words: Tuple[int, ...], r0: int, budget: int = 100_000) -> int:
    """Minimal A32 instruction interpreter for the subset used by sum(n) (MOV, CMP, ADD, B/BGT, BX LR)."""
    regs = [0] * 16
    regs[0] = wrap_i32(r0)
    LR = 14
    PC = 15
    RET_SENTINEL = 0xFFFFFFFC
    regs[LR] = RET_SENTINEL
    regs[PC] = 0
    flag_z = False
    flag_n = False
    flag_v = False

    while regs[PC] != RET_SENTINEL:
        if budget <= 0:
            raise VMError("ARMv6 simulation step budget exhausted")
        budget -= 1
        idx = regs[PC] // 4
        if idx < 0 or idx >= len(words):
            raise VMError(f"ARMv6 PC out of range: 0x{regs[PC]:08x}")
        w = words[idx]
        curr_pc = regs[PC]
        regs[PC] = curr_pc + 4

        cond = (w >> 28) & 0xF
        if cond == 0xE:     # AL (always)
            cond_ok = True
        elif cond == 0xC:   # GT (signed >: Z == 0 and N == V)
            cond_ok = (not flag_z) and (flag_n == flag_v)
        else:
            raise VMError(f"unsupported A32 condition 0x{cond:x}")
        if not cond_ok:
            continue

        if w == 0xE12FFF1E:  # bx lr
            regs[PC] = regs[LR]
            continue

        top3 = (w >> 25) & 0x7
        if top3 == 0b101:   # Branch: B / BL
            imm24 = w & 0xFFFFFF
            if imm24 & 0x800000:
                imm24 -= 0x1000000
            # In A32, PC during execution is curr_pc + 8
            regs[PC] = (curr_pc + 8) + (imm24 << 2)
            continue

        if (w >> 26) & 0b11 == 0b00:  # Data processing
            is_imm = ((w >> 25) & 1) == 1
            opcode = (w >> 21) & 0xF
            rn = (w >> 16) & 0xF
            rd = (w >> 12) & 0xF
            if is_imm:
                imm8 = w & 0xFF
                rot = ((w >> 8) & 0xF) * 2
                op2 = ((imm8 >> rot) | (imm8 << (32 - rot))) & 0xFFFFFFFF if rot else imm8
                op2 = wrap_i32(op2)
            else:
                rm = w & 0xF
                op2 = regs[rm]

            if opcode == 0xD:   # MOV Rd, Op2
                regs[rd] = wrap_i32(op2)
            elif opcode == 0x4:  # ADD Rd, Rn, Op2
                regs[rd] = wrap_i32(regs[rn] + op2)
            elif opcode == 0xA:  # CMP Rn, Op2 (sets flags on Rn - Op2)
                a = regs[rn]
                b = op2
                diff = wrap_i32(a - b)
                flag_z = (diff == 0)
                flag_n = (diff < 0)
                flag_v = ((a < 0) != (b < 0)) and ((diff < 0) != (a < 0))
            else:
                raise VMError(f"unsupported A32 data-processing opcode 0x{opcode:x}")
            continue

        raise VMError(f"unsupported A32 word 0x{w:08x}")

    return regs[0]


# ---------------------------------------------------------------------------
# Command-line Interface
# ---------------------------------------------------------------------------

def parse_cli_arg(raw: str) -> Union[int, bool]:
    if raw == "true":
        return True
    if raw == "false":
        return False
    return int(raw)


def main(argv: Optional[List[str]] = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("source", help="path to the .bml source file")
    ap.add_argument("--check", action="store_true", help="only run stage 1 (lexer, parser, AST, type check)")
    ap.add_argument("--lua", help="write the stripped plain Lua source to this file")
    ap.add_argument("--dump", action="store_true", help="print the disassembled stage 2 stack bytecode")
    ap.add_argument("--bmvm", help="write the stage 3 kernel bmvm binary blob (.bmv) to this file")
    ap.add_argument("--armv6", help="write the stage 4 ARMv6 A32 machine code (.bin) to this file")
    ap.add_argument("--dump-armv6", action="store_true", help="print the stage 4 ARMv6 A32 words")
    ap.add_argument("--wrap32", action="store_true", help="wrap i32 arithmetic at 32 bits in the stack VM")
    ap.add_argument("--run", nargs="+", metavar=("FN", "ARG"), help="run function FN with arguments in the stack VM")
    args = ap.parse_args(argv)

    try:
        with open(args.source, encoding="utf-8") as f:
            src = f.read()
        lua = to_lua(src)
        mod = compile_source(src)
    except Error as e:
        print(f"{args.source}: {e}", file=sys.stderr)
        return 1

    if args.lua:
        with open(args.lua, "w", encoding="utf-8") as f:
            f.write(lua)
    if args.dump:
        sys.stdout.write(disassemble(mod))
    if args.bmvm:
        blob = BMVMCompiler(mod).compile_blob()
        with open(args.bmvm, "wb") as f:
            f.write(blob)
    if args.armv6 or args.dump_armv6:
        arm_mod = compile_armv6(src)
        if args.armv6:
            with open(args.armv6, "wb") as f:
                f.write(struct.pack(f"<{len(arm_mod.words)}I", *arm_mod.words))
        if args.dump_armv6:
            sys.stdout.write(disassemble_armv6(arm_mod))
    if args.run:
        fn_name = args.run[0]
        fn_args = [parse_cli_arg(x) for x in args.run[1:]]
        vm = StackVM(mod, wrap32=args.wrap32)
        res = vm.call(fn_name, fn_args)
        if res is not None:
            if isinstance(res, bool):
                print("true" if res else "false")
            else:
                print(res)
    return 0


if __name__ == "__main__":
    sys.exit(main())
