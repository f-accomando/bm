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
        if not self.code or self.code[-1].op not in ("RET", "RET0"):
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
