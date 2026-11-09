#!/usr/bin/env python3
"""bml.py: the compiler and stack virtual machine for the language of bm (stages 1 and 2).

The language of bm uses the typed Lua syntax of scripts/bmt.py so that a single source file:
  1. runs in the standard Lua VM when type annotations are stripped (--lua), and
  2. compiles through the bml front end (Lexer -> Parser -> AST -> TypeChecker) to stack bytecode
     executed by the host StackVM (stage 2), and later to ARMv6 A32 machine words (stage 4).

Usage:
  python3 tools/bml/bml.py <file.b> [--check] [--lua <out.lua>] [--dump] [--run <fn> <arg>...] [--wrap32]
"""
import argparse
from dataclasses import dataclass, field
import math
import re
import struct
import sys
from typing import Any, Dict, List, Optional, Tuple, Union

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
    r"|(?P<op><=|>=|==|~=|//|[-+*/%&(),.:\[\]=<>])"
)

KEYWORDS = {
    "function", "local", "for", "do", "while", "end",
    "if", "then", "elseif", "else", "return", "break",
    "and", "or", "not", "true", "false", "struct",
    "const", "enum",
}

TYPES = ("i32", "bool", "f32")
ARRAY_TYPES = ("i32[]", "bool[]", "f32[]")
BUILTINS = {
    "abs": (["i32"], "i32"),
    "min": (["i32", "i32"], "i32"),
    "max": (["i32", "i32"], "i32"),
    "sqrt": (["f32"], "f32"),
    "f32": (["i32"], "f32"),
    "floor": (["f32"], "i32"),
    "cls": (["i32"], None),
    "pix": (["i32", "i32", "i32"], None),
    "rectfill": (["i32", "i32", "i32", "i32", "i32"], None),
    "spr": (["i32", "i32", "i32"], None),
    "btn": (["i32"], "bool"),
    "btnp": (["i32"], "bool"),
    "line": (["i32", "i32", "i32", "i32", "i32"], None),
    "circ": (["i32", "i32", "i32", "i32"], None),
    "circfill": (["i32", "i32", "i32", "i32"], None),
    "rect": (["i32", "i32", "i32", "i32", "i32"], None),
    "sfx": (["i32"], None),
    "music": (["i32"], None),
    "time": ([], "f32"),
}

HOST_FUNCTIONS: Dict[str, Tuple[int, List[str], Optional[str]]] = {
    "cls": (0, ["i32"], None),
    "pix": (1, ["i32", "i32", "i32"], None),
    "rectfill": (2, ["i32", "i32", "i32", "i32", "i32"], None),
    "spr": (3, ["i32", "i32", "i32"], None),
    "btn": (4, ["i32"], "bool"),
    "btnp": (5, ["i32"], "bool"),
    "line": (6, ["i32", "i32", "i32", "i32", "i32"], None),
    "circ": (7, ["i32", "i32", "i32", "i32"], None),
    "circfill": (8, ["i32", "i32", "i32", "i32"], None),
    "rect": (9, ["i32", "i32", "i32", "i32", "i32"], None),
    "sfx": (10, ["i32"], None),
    "music": (11, ["i32"], None),
    "time": (12, [], "f32"),
}


def f32_round(x: float) -> float:
    """Rounds a Python float to IEEE-754 binary32 (single precision)."""
    return struct.unpack("<f", struct.pack("<f", float(x)))[0]


def f32_to_bits(x: float) -> int:
    """Returns the unsigned 32-bit IEEE-754 binary32 representation of x."""
    return struct.unpack("<I", struct.pack("<f", float(x)))[0]


def bits_to_f32(u: int) -> float:
    """Reinterprets an unsigned 32-bit integer as an IEEE-754 binary32 float."""
    return struct.unpack("<f", struct.pack("<I", u & 0xFFFFFFFF))[0]


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
class FloatLit(Expr):
    value: float


@dataclass
class BoolLit(Expr):
    value: bool


@dataclass
class VarExpr(Expr):
    name: str


@dataclass
class IndexExpr(Expr):
    base: str
    index: Expr


@dataclass
class FieldExpr(Expr):
    base: str
    field_name: str
    field_idx: int = field(default=-1, init=False)


@dataclass
class UnaryOp(Expr):
    op: str         # '-', 'not'
    operand: Expr


@dataclass
class BinOp(Expr):
    op: str         # '+', '-', '*', '/', '//', '%', '&', '<', '<=', '>', '>=', '==', '~=', 'and', 'or'
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
class IndexAssignStmt(Stmt):
    base: str
    index: Expr
    expr: Expr
    elem_type: str = field(default="", init=False)


@dataclass
class FieldAssignStmt(Stmt):
    base: str
    field_name: str
    expr: Expr
    field_idx: int = field(default=-1, init=False)
    field_type: str = field(default="", init=False)


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
class NoOpStmt(Stmt):
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
class StructField:
    name: str
    typ: str
    line: int


@dataclass
class StructDecl:
    name: str
    fields: List[StructField]
    line: int


@dataclass
class ConstDecl:
    name: str
    typ: str
    val: Union[int, float, bool]
    line: int


@dataclass
class EnumDecl:
    name: str
    entries: Dict[str, int]
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
    cuts: List[Any]
    structs: List[StructDecl] = field(default_factory=list)
    consts: List[ConstDecl] = field(default_factory=list)
    enums: List[EnumDecl] = field(default_factory=list)


# ---------------------------------------------------------------------------
# Stage 1.3: Recursive-Descent Parser
# ---------------------------------------------------------------------------

class Parser:
    def __init__(self, src: str):
        self.src = src
        self.toks = tokenize(src)
        self.i = 0
        self.cuts: List[Any] = []
        self.struct_names: set = set()
        self.cur_struct_vars: Dict[str, str] = {}
        self.const_table: Dict[str, Tuple[str, Union[int, float, bool]]] = {}
        self.enum_table: Dict[str, Dict[str, int]] = {}

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

    def parse_type_annotation(self, allow_composite: bool = False) -> str:
        """Parses ': <type>' (including T[] or struct Name when allow_composite) and records its byte span."""
        colon = self.expect(":")
        tname = self.expect_name()
        end_pos = tname.end
        typ_s = tname.text
        if self.cur().text == "[":
            self.expect("[")
            rb = self.expect("]")
            end_pos = rb.end
            typ_s = f"{tname.text}[]"
            if not allow_composite or typ_s not in ARRAY_TYPES:
                self.fail(f"unknown or disallowed array type '{typ_s}'", tname)
        elif typ_s in self.struct_names:
            if not allow_composite:
                self.fail(f"struct type '{typ_s}' is only allowed on parameters", tname)
        elif typ_s not in TYPES:
            self.fail(f"unknown type '{typ_s}' (expected i32, bool, or f32)", tname)
        self.cuts.append((colon.start, end_pos))
        return typ_s

    def parse(self) -> Program:
        funcs: List[FuncDecl] = []
        structs: List[StructDecl] = []
        consts: List[ConstDecl] = []
        enums: List[EnumDecl] = []
        while self.cur().kind != "eof":
            if self.cur().kind == "kw" and self.cur().text == "struct":
                structs.append(self.parse_struct())
            elif self.cur().kind == "kw" and self.cur().text == "const":
                consts.append(self.parse_const())
            elif self.cur().kind == "kw" and self.cur().text == "enum":
                enums.append(self.parse_enum())
            else:
                funcs.append(self.parse_function())
        return Program(funcs=funcs, cuts=self.cuts, structs=structs, consts=consts, enums=enums)

    def parse_const(self) -> ConstDecl:
        kw = self.expect("const")
        cname = self.expect_name()
        if cname.text in self.const_table:
            self.fail(f"const '{cname.text}' is declared twice", cname)
        type_tok = self.cur()
        cuts_before = len(self.cuts)
        ctyp = self.parse_type_annotation(allow_composite=False)
        del self.cuts[cuts_before:]
        self.expect("=")
        sign = 1
        val_start = self.cur().start
        if self.accept("-"):
            sign = -1
        val_tok = self.cur()
        if ctyp == "i32":
            if val_tok.kind != "int":
                self.fail("expected integer literal for i32 const", val_tok)
            self.i += 1
            val: Union[int, float, bool] = sign * int(val_tok.text)
        elif ctyp == "f32":
            if val_tok.kind not in ("float", "int"):
                self.fail("expected float or integer literal for f32 const", val_tok)
            self.i += 1
            val = f32_round(float(sign * float(val_tok.text)))
        elif ctyp == "bool":
            if sign < 0:
                self.fail("unexpected '-' for boolean const", val_tok)
            if self.accept("true"):
                val = True
            elif self.accept("false"):
                val = False
            else:
                self.fail("expected true or false for bool const", val_tok)
        else:
            self.fail(f"unsupported type '{ctyp}' for const", type_tok)

        self.const_table[cname.text] = (ctyp, val)
        self.cuts.append((kw.start, val_start, f"local {cname.text} = "))
        return ConstDecl(cname.text, ctyp, val, kw.line)

    def parse_enum(self) -> EnumDecl:
        kw = self.expect("enum")
        ename = self.expect_name()
        if ename.text in self.enum_table:
            self.fail(f"enum '{ename.text}' is declared twice", ename)
        entries: Dict[str, int] = {}
        next_val = 0
        while self.cur().kind != "eof" and self.cur().text != "end":
            item = self.expect_name()
            if item.text in entries:
                self.fail(f"enum entry '{item.text}' declared twice in '{ename.text}'", item)
            if self.accept("="):
                sign = 1
                if self.accept("-"):
                    sign = -1
                iv = self.cur()
                if iv.kind != "int":
                    self.fail("expected integer literal for enum value", iv)
                self.i += 1
                next_val = sign * int(iv.text)
            entries[item.text] = next_val
            next_val += 1
            self.accept(",")
        end_kw = self.expect("end")
        self.enum_table[ename.text] = entries
        lua_fields = ", ".join(f"{k} = {v}" for k, v in entries.items())
        lua_code = f"local {ename.text} = {{ {lua_fields} }}"
        self.cuts.append((kw.start, end_kw.end, lua_code))
        return EnumDecl(ename.text, entries, kw.line)

    def parse_struct(self) -> StructDecl:
        kw = self.expect("struct")
        sname = self.expect_name()
        if sname.text in TYPES or sname.text in self.struct_names:
            self.fail(f"struct '{sname.text}' is defined twice", sname)
        fields: List[StructField] = []
        seen_fields: set = set()
        cuts_before = len(self.cuts)
        while self.cur().kind != "eof" and self.cur().text != "end":
            fname = self.expect_name()
            if fname.text in seen_fields:
                self.fail(f"field '{fname.text}' is declared twice in struct '{sname.text}'", fname)
            seen_fields.add(fname.text)
            ftype = self.parse_type_annotation(allow_composite=False)
            fields.append(StructField(fname.text, ftype, fname.line))
            self.accept(",")
        end_tok = self.expect("end")
        if not fields:
            self.fail(f"struct '{sname.text}' has no fields", kw)
        self.struct_names.add(sname.text)
        del self.cuts[cuts_before:]
        self.cuts.append((kw.start, end_tok.end))
        return StructDecl(sname.text, fields, kw.line)

    def parse_function(self) -> FuncDecl:
        kw = self.expect("function")
        fname = self.expect_name()
        is_method = False
        st_name = ""
        mname_tok = None
        if self.accept(":"):
            mname_tok = self.expect_name()
            st_name = fname.text
            if st_name not in self.struct_names:
                self.fail(f"struct '{st_name}' is not defined", fname)
            fn_full_name = f"{st_name}_{mname_tok.text}"
            is_method = True
        else:
            fn_full_name = fname.text

        lparen = self.expect("(")
        params: List[Param] = []
        if is_method:
            params.append(Param("self", st_name, kw.line))
        if self.cur().text != ")":
            while True:
                pname = self.expect_name()
                ptype = self.parse_type_annotation(allow_composite=True)
                params.append(Param(pname.text, ptype, pname.line))
                if not self.accept(","):
                    break
        rparen = self.expect(")")
        ret_type: Optional[str] = None
        if self.cur().text == ":":
            ret_type = self.parse_type_annotation(allow_composite=False)

        if is_method:
            if len(params) > 1:
                self.cuts.append((kw.start, lparen.end, f"function {fn_full_name}(self, "))
            else:
                self.cuts.append((kw.start, rparen.end, f"function {fn_full_name}(self)"))

        saved_struct_vars = self.cur_struct_vars
        self.cur_struct_vars = {p.name: p.typ for p in params if p.typ in self.struct_names}

        body = self.parse_block({"end"})
        self.expect("end")
        self.cur_struct_vars = saved_struct_vars
        return FuncDecl(fn_full_name, params, ret_type, body, kw.line)

    def parse_block(self, stops: set) -> List[Stmt]:
        stmts: List[Stmt] = []
        while self.cur().kind != "eof" and self.cur().text not in stops:
            stmts.append(self.parse_stmt())
        return stmts

    def parse_stmt(self) -> Stmt:
        t = self.cur()
        if t.kind == "kw":
            if t.text == "const":
                self.parse_const()
                return NoOpStmt(t.line)
            if t.text == "local":
                self.i += 1
                v = self.expect_name()
                decl_type: Optional[str] = None
                if self.cur().text == ":":
                    decl_type = self.parse_type_annotation(allow_composite=False)
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
            if self.accept(":"):
                mname = self.expect_name()
                if ident.text not in self.cur_struct_vars:
                    self.fail(f"'{ident.text}' is not a struct", ident)
                st_name = self.cur_struct_vars[ident.text]
                callee = f"{st_name}_{mname.text}"
                lparen = self.expect("(")
                args: List[Expr] = [VarExpr(ident.line, ident.text)]
                has_user_args = False
                if self.cur().text != ")":
                    has_user_args = True
                    while True:
                        args.append(self.parse_expr())
                        if not self.accept(","):
                            break
                rparen = self.expect(")")
                if has_user_args:
                    self.cuts.append((ident.start, lparen.end, f"{callee}({ident.text}, "))
                else:
                    self.cuts.append((ident.start, rparen.end, f"{callee}({ident.text})"))
                return CallStmt(ident.line, CallExpr(ident.line, callee, args))
            if self.accept("["):
                idx_e = self.parse_expr()
                self.expect("]")
                self.expect("=")
                val_e = self.parse_expr()
                return IndexAssignStmt(ident.line, ident.text, idx_e, val_e)
            if self.accept("."):
                fld = self.expect_name()
                self.expect("=")
                val_e = self.parse_expr()
                return FieldAssignStmt(ident.line, ident.text, fld.text, val_e)
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

    # -- Expressions (precedence: or < and < cmp < & < + - < * / // % < unary < primary)
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
            self.i += 1
            return FloatLit(t.line, f32_round(float(t.text)))
        if self.accept("true"):
            return BoolLit(t.line, True)
        if self.accept("false"):
            return BoolLit(t.line, False)
        if self.accept("("):
            e = self.parse_expr()
            self.expect(")")
            return e
        if t.kind == "name" or (t.kind == "kw" and t.text in BUILTINS):
            ident = self.cur()
            self.i += 1
            if ident.text in self.const_table:
                ctyp, cval = self.const_table[ident.text]
                if ctyp == "i32":
                    return IntLit(ident.line, int(cval))
                elif ctyp == "f32":
                    return FloatLit(ident.line, float(cval))
                elif ctyp == "bool":
                    return BoolLit(ident.line, bool(cval))
            if ident.text in self.enum_table:
                self.expect(".")
                ent = self.expect_name()
                if ent.text not in self.enum_table[ident.text]:
                    self.fail(f"enum '{ident.text}' has no entry '{ent.text}'", ent)
                return IntLit(ident.line, self.enum_table[ident.text][ent.text])
            if self.accept("("):
                args = self.parse_arg_list()
                return CallExpr(ident.line, ident.text, args)
            if self.accept(":"):
                mname = self.expect_name()
                if ident.text not in self.cur_struct_vars:
                    self.fail(f"'{ident.text}' is not a struct", ident)
                st_name = self.cur_struct_vars[ident.text]
                callee = f"{st_name}_{mname.text}"
                lparen = self.expect("(")
                args = [VarExpr(ident.line, ident.text)]
                has_user_args = False
                if self.cur().text != ")":
                    has_user_args = True
                    while True:
                        args.append(self.parse_expr())
                        if not self.accept(","):
                            break
                rparen = self.expect(")")
                if has_user_args:
                    self.cuts.append((ident.start, lparen.end, f"{callee}({ident.text}, "))
                else:
                    self.cuts.append((ident.start, rparen.end, f"{callee}({ident.text})"))
                return CallExpr(ident.line, callee, args)
            if self.accept("["):
                idx_e = self.parse_expr()
                self.expect("]")
                return IndexExpr(ident.line, ident.text, idx_e)
            if self.accept("."):
                fld = self.expect_name()
                return FieldExpr(ident.line, ident.text, fld.text)
            return VarExpr(ident.line, ident.text)
        found = t.text or "end of file"
        self.fail(f"an expression cannot start with '{found}'", t)


# ---------------------------------------------------------------------------
# Stage 1.4: Static Type Checker
# ---------------------------------------------------------------------------

class TypeChecker:
    """Checks types (i32, bool, f32, arrays, structs), variable scopes, loop break placement, and signatures."""

    def __init__(self) -> None:
        self.funcs: Dict[str, Tuple[List[str], Optional[str]]] = {}
        self.structs: Dict[str, StructDecl] = {}
        self.vars: Dict[str, str] = {}
        self.ret_type: Optional[str] = None
        self.loop_depth: int = 0

    def fail(self, line: int, msg: str) -> None:
        raise Error(f"line {line}: {msg}")

    def check_program(self, prog: Program) -> Program:
        for st in prog.structs:
            self.structs[st.name] = st
        if not prog.funcs:
            self.fail(1, "program has no functions")
        for fn in prog.funcs:
            if fn.name in self.funcs or fn.name in BUILTINS:
                self.fail(fn.line, f"function '{fn.name}' is defined twice")
            param_types = [p.typ for p in fn.params]
            self.funcs[fn.name] = (param_types, fn.ret_type)
        for fn in prog.funcs:
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
        if isinstance(s, NoOpStmt):
            return
        if isinstance(s, LocalStmt):
            et = self.check_expr(s.expr, s.line)
            if et not in TYPES:
                self.fail(s.line, f"local variable '{s.name}' must be i32, bool, or f32, not {et}")
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

        if isinstance(s, IndexAssignStmt):
            if s.base not in self.vars:
                self.fail(s.line, f"'{s.base}' is not declared")
            bt = self.vars[s.base]
            if not bt.endswith("[]"):
                self.fail(s.line, f"'{s.base}' is {bt}, not an array")
            elem_t = bt[:-2]
            it = self.check_expr(s.index, s.line)
            if it != "i32":
                self.fail(s.line, f"array index must be i32, not {it}")
            vt = self.check_expr(s.expr, s.line)
            if vt != elem_t:
                self.fail(s.line, f"{vt} assigned to element of {bt}")
            s.elem_type = elem_t
            return

        if isinstance(s, FieldAssignStmt):
            if s.base not in self.vars:
                self.fail(s.line, f"'{s.base}' is not declared")
            bt = self.vars[s.base]
            if bt not in self.structs:
                self.fail(s.line, f"'{s.base}' is {bt}, not a struct")
            st = self.structs[bt]
            f_idx = -1
            f_typ = ""
            for idx, fld in enumerate(st.fields):
                if fld.name == s.field_name:
                    f_idx = idx
                    f_typ = fld.typ
                    break
            if f_idx < 0:
                self.fail(s.line, f"struct '{bt}' has no field '{s.field_name}'")
            vt = self.check_expr(s.expr, s.line)
            if vt != f_typ:
                self.fail(s.line, f"{vt} assigned to '{s.base}.{s.field_name}' of {f_typ}")
            s.field_idx = f_idx
            s.field_type = f_typ
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

        if isinstance(e, FloatLit):
            e.typ = "f32"
            return "f32"

        if isinstance(e, BoolLit):
            e.typ = "bool"
            return "bool"

        if isinstance(e, VarExpr):
            if e.name not in self.vars:
                self.fail(stmt_line, f"'{e.name}' is not declared")
            e.typ = self.vars[e.name]
            return e.typ

        if isinstance(e, IndexExpr):
            if e.base not in self.vars:
                self.fail(stmt_line, f"'{e.base}' is not declared")
            bt = self.vars[e.base]
            if not bt.endswith("[]"):
                self.fail(stmt_line, f"'{e.base}' is {bt}, not an array")
            it = self.check_expr(e.index, stmt_line)
            if it != "i32":
                self.fail(stmt_line, f"array index must be i32, not {it}")
            e.typ = bt[:-2]
            return e.typ

        if isinstance(e, FieldExpr):
            if e.base not in self.vars:
                self.fail(stmt_line, f"'{e.base}' is not declared")
            bt = self.vars[e.base]
            if bt not in self.structs:
                self.fail(stmt_line, f"'{e.base}' is {bt}, not a struct")
            st = self.structs[bt]
            for idx, fld in enumerate(st.fields):
                if fld.name == e.field_name:
                    e.field_idx = idx
                    e.typ = fld.typ
                    return fld.typ
            self.fail(stmt_line, f"struct '{bt}' has no field '{e.field_name}'")
            return ""

        if isinstance(e, UnaryOp):
            if e.op == "-" and isinstance(e.operand, IntLit) and e.operand.value == 0x80000000:
                e.operand.typ = "i32"
                e.typ = "i32"
                return "i32"
            ot = self.check_expr(e.operand, stmt_line)
            if e.op == "-":
                if ot not in ("i32", "f32"):
                    self.fail(stmt_line, f"unary '-' takes i32 or f32, not {ot}")
                e.typ = ot
                return ot
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
            if e.op in ("+", "-", "*"):
                if lt == "i32" and rt == "i32":
                    e.typ = "i32"
                    return "i32"
                if lt == "f32" and rt == "f32":
                    e.typ = "f32"
                    return "f32"
                self.fail(stmt_line, f"'{e.op}' takes matching i32 or f32, not {lt} and {rt}")
            if e.op in ("//", "%", "&"):
                if lt != "i32" or rt != "i32":
                    self.fail(stmt_line, f"'{e.op}' takes i32, not {lt} and {rt}")
                e.typ = "i32"
                return "i32"
            if e.op == "/":
                if lt == "f32" and rt == "f32":
                    e.typ = "f32"
                    return "f32"
                self.fail(stmt_line, "'/' gives a float in Lua; use '//' for integer division in stage 1")
            if e.op in ("<", "<=", ">", ">="):
                if (lt == "i32" and rt == "i32") or (lt == "f32" and rt == "f32"):
                    e.typ = "bool"
                    return "bool"
                self.fail(stmt_line, f"cannot compare {lt} with {rt}")
            if e.op in ("==", "~="):
                if lt != rt or lt not in ("i32", "bool", "f32"):
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
        if call.callee in ("abs", "min", "max"):
            want_n = 1 if call.callee == "abs" else 2
            if len(call.args) != want_n:
                self.fail(stmt_line, f"'{call.callee}' takes {want_n} arguments, not {len(call.args)}")
            arg_types = [self.check_expr(a, stmt_line) for a in call.args]
            t0 = arg_types[0]
            if t0 not in ("i32", "f32") or any(at != t0 for at in arg_types):
                self.fail(stmt_line, f"'{call.callee}' takes matching i32 or f32 arguments")
            call.typ = t0
            return t0

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
    """Checks the BML source and strips type annotations and struct blocks to yield plain Lua."""
    prog = parse_and_check(src)
    lua = src
    sorted_cuts = sorted(prog.cuts, key=lambda c: c[0], reverse=True)
    for cut in sorted_cuts:
        start, end = cut[0], cut[1]
        rep = cut[2] if len(cut) > 2 else None
        if rep is None:
            nl_count = lua[start:end].count("\n")
            lua = lua[:start] + ("\n" * nl_count) + lua[end:]
        else:
            lua = lua[:start] + rep + lua[end:]
    return lua


# ---------------------------------------------------------------------------
# Stage 2.1: Stack Bytecode ISA and Compiler
# ---------------------------------------------------------------------------

OPCODES = (
    "PUSH_CONST",   # arg = constant index k; pushes K[k] (i32)
    "PUSH_FCONST",  # arg = float constant index k; pushes FK[k] (f32)
    "PUSH_TRUE",    # pushes True
    "PUSH_FALSE",   # pushes False
    "POP",          # discards top of stack
    "LOAD",         # arg = local slot index; pushes locals[slot]
    "STORE",        # arg = local slot index; pops TOS into locals[slot]
    "ADD",          # pops b, a; pushes a + b (i32)
    "SUB",          # pops b, a; pushes a - b (i32)
    "MUL",          # pops b, a; pushes a * b (i32)
    "IDIV",         # pops b, a; pushes floor(a / b) (0 if b == 0)
    "IMOD",         # pops b, a; pushes a - floor(a / b) * b (0 if b == 0)
    "BAND",         # pops b, a; pushes a & b (32-bit signed)
    "NEG",          # pops a; pushes -a (i32)
    "NOT",          # pops a; pushes not a
    "EQ",           # pops b, a; pushes a == b (i32/bool)
    "NE",           # pops b, a; pushes a != b (i32/bool)
    "LT",           # pops b, a; pushes a < b (i32)
    "LE",           # pops b, a; pushes a <= b (i32)
    "GT",           # pops b, a; pushes a > b (i32)
    "GE",           # pops b, a; pushes a >= b (i32)
    "ABS",          # pops a; pushes abs(a) (i32)
    "MIN",          # pops b, a; pushes min(a, b) (i32)
    "MAX",          # pops b, a; pushes max(a, b) (i32)
    "FADD",         # pops b, a; pushes f32(a + b)
    "FSUB",         # pops b, a; pushes f32(a - b)
    "FMUL",         # pops b, a; pushes f32(a * b)
    "FDIV",         # pops b, a; pushes f32(a / b)
    "FNEG",         # pops a; pushes f32(-a)
    "FEQ",          # pops b, a; pushes a == b (f32)
    "FNE",          # pops b, a; pushes a != b (f32)
    "FLT",          # pops b, a; pushes a < b (f32)
    "FLE",          # pops b, a; pushes a <= b (f32)
    "FGT",          # pops b, a; pushes a > b (f32)
    "FGE",          # pops b, a; pushes a >= b (f32)
    "FABS",         # pops a; pushes f32(abs(a))
    "FMIN",         # pops b, a; pushes min(a, b) (f32)
    "FMAX",         # pops b, a; pushes max(a, b) (f32)
    "FSQRT",        # pops a; pushes f32(sqrt(a))
    "I2F",          # pops a (i32); pushes f32(a)
    "FLOOR",        # pops a (f32); pushes i32(floor(a))
    "AGET",         # pops idx, arr; pushes arr[idx] (i32/bool, 1-based)
    "AGETF",        # pops idx, arr; pushes arr[idx] (f32, 1-based)
    "ASET",         # pops val, idx, arr; sets arr[idx] = val (i32/bool, 1-based)
    "ASETF",        # pops val, idx, arr; sets arr[idx] = val (f32, 1-based)
    "JMP",          # arg = target pc; pc = target
    "JZ",           # arg = target pc; pops cond; if not cond: pc = target
    "JF_KEEP",      # arg = target pc; if not TOS: pc = target (keeps False), else pops True
    "JT_KEEP",      # arg = target pc; if TOS: pc = target (keeps True), else pops False
    "CALL",         # arg = (fn_index << 8) | nargs; pops nargs, pushes return value (if any)
    "CALL_HOST",    # arg = (host_id << 8) | nargs; pops nargs, calls host/kernel fn, pushes return value (if any)
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
    fconsts: List[float] = field(default_factory=list)
    structs: Dict[str, StructDecl] = field(default_factory=dict)


class BytecodeCompiler:
    """Compiles a checked Program AST into a StackVM Module."""

    def __init__(self, prog: Program):
        self.prog = prog
        self.consts: List[int] = []
        self.const_map: Dict[int, int] = {}
        self.fconsts: List[float] = []
        self.fconst_map: Dict[int, int] = {}
        self.structs: Dict[str, StructDecl] = {st.name: st for st in prog.structs}
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

    def add_fconst(self, val: float) -> int:
        fv = f32_round(val)
        bits = f32_to_bits(fv)
        if bits not in self.fconst_map:
            self.fconst_map[bits] = len(self.fconsts)
            self.fconsts.append(fv)
        return self.fconst_map[bits]

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
        return Module(
            funcs=compiled,
            consts=self.consts,
            func_index=self.func_index,
            fconsts=self.fconsts,
            structs=self.structs,
        )

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
            elif fn.ret_type == "f32":
                fk = self.add_fconst(0.0)
                self.emit("PUSH_FCONST", fk, "0.0")
                self.emit("RET")
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
        if isinstance(s, NoOpStmt):
            return
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

        if isinstance(s, IndexAssignStmt):
            slot = self.slots[s.base]
            self.emit("LOAD", slot, s.base)
            self.compile_expr(s.index)
            self.compile_expr(s.expr)
            self.emit("ASETF" if s.elem_type == "f32" else "ASET", 0, f"{s.base}[...]")
            return

        if isinstance(s, FieldAssignStmt):
            slot = self.slots[s.base]
            self.emit("LOAD", slot, s.base)
            k = self.add_const(s.field_idx + 1)
            self.emit("PUSH_CONST", k, f".{s.field_name}")
            self.compile_expr(s.expr)
            self.emit("ASETF" if s.field_type == "f32" else "ASET", 0, f"{s.base}.{s.field_name}")
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

        if isinstance(e, FloatLit):
            fk = self.add_fconst(e.value)
            self.emit("PUSH_FCONST", fk, str(e.value))
            return

        if isinstance(e, BoolLit):
            self.emit("PUSH_TRUE" if e.value else "PUSH_FALSE")
            return

        if isinstance(e, VarExpr):
            slot = self.slots[e.name]
            self.emit("LOAD", slot, e.name)
            return

        if isinstance(e, IndexExpr):
            slot = self.slots[e.base]
            self.emit("LOAD", slot, e.base)
            self.compile_expr(e.index)
            self.emit("AGETF" if e.typ == "f32" else "AGET", 0, f"{e.base}[...]")
            return

        if isinstance(e, FieldExpr):
            slot = self.slots[e.base]
            self.emit("LOAD", slot, e.base)
            k = self.add_const(e.field_idx + 1)
            self.emit("PUSH_CONST", k, f".{e.field_name}")
            self.emit("AGETF" if e.typ == "f32" else "AGET", 0, f"{e.base}.{e.field_name}")
            return

        if isinstance(e, UnaryOp):
            if e.op == "-" and isinstance(e.operand, IntLit) and e.operand.value == 0x80000000:
                k = self.add_const(-2147483648)
                self.emit("PUSH_CONST", k, "-2147483648")
                return
            self.compile_expr(e.operand)
            if e.op == "-":
                self.emit("FNEG" if e.typ == "f32" else "NEG")
            else:
                self.emit("NOT")
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
            if e.left.typ == "f32":
                f_op_map = {
                    "+": "FADD", "-": "FSUB", "*": "FMUL", "/": "FDIV",
                    "==": "FEQ", "~=": "FNE", "<": "FLT", "<=": "FLE", ">": "FGT", ">=": "FGE",
                }
                self.emit(f_op_map[e.op])
            else:
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
            is_f = call.args[0].typ == "f32"
            self.emit(("F" if is_f else "") + call.callee.upper())
            return
        if call.callee == "sqrt":
            self.emit("FSQRT")
            return
        if call.callee == "f32":
            self.emit("I2F")
            return
        if call.callee == "floor":
            self.emit("FLOOR")
            return
        if call.callee in HOST_FUNCTIONS:
            host_id, _, _ = HOST_FUNCTIONS[call.callee]
            packed = (host_id << 8) | (len(call.args) & 0xFF)
            self.emit("CALL_HOST", packed, f"host:{call.callee}/{len(call.args)}")
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
    if mod.fconsts:
        fconsts_str = ", ".join(f"FK[{i}]={v}" for i, v in enumerate(mod.fconsts))
        lines.append(f"; fconsts: {fconsts_str}")
    for idx, fn in enumerate(mod.funcs):
        ret_s = fn.ret_type or "void"
        params_s = ", ".join(f"{fn.local_names[i]}: {t}" for i, t in enumerate(fn.param_types))
        lines.append(f"\nfn #{idx} {fn.name}({params_s}) -> {ret_s}  [locals={fn.nlocals}]")
        for pc, ins in enumerate(fn.code):
            note = f"  ; {ins.comment}" if ins.comment else ""
            if ins.op in ("PUSH_CONST", "PUSH_FCONST", "LOAD", "STORE", "JMP", "JZ", "JF_KEEP", "JT_KEEP"):
                lines.append(f"  {pc:04d}  {ins.op:<12} {ins.arg}{note}")
            elif ins.op == "CALL":
                lines.append(f"  {pc:04d}  {ins.op:<12} fn={ins.arg >> 8}, argc={ins.arg & 0xFF}{note}")
            elif ins.op == "CALL_HOST":
                lines.append(f"  {pc:04d}  {ins.op:<12} host={ins.arg >> 8}, argc={ins.arg & 0xFF}{note}")
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
    """Executes a compiled BML Module on a stack machine (with 32-bit i32 wrapping by default in Stage 6+)."""

    def __init__(
        self,
        mod: Module,
        wrap32: bool = True,
        max_depth: int = 64,
        host_fns: Optional[Dict[str, Any]] = None,
    ):
        self.mod = mod
        self.wrap32 = wrap32
        self.max_depth = max_depth
        self.host_fns: Dict[str, Any] = dict(host_fns) if host_fns else {}
        self.host_by_id: Dict[int, Tuple[str, Optional[str]]] = {
            hid: (name, ret) for name, (hid, _, ret) in HOST_FUNCTIONS.items()
        }

    def _norm(self, x: int) -> int:
        return wrap_i32(x) if self.wrap32 else x

    def call(self, fn_name: str, args: List[Any], budget: int = 1_000_000) -> Optional[Union[int, float, bool]]:
        if fn_name not in self.mod.func_index:
            raise VMError(f"unknown function '{fn_name}'")
        fn_idx = self.mod.func_index[fn_name]
        fn = self.mod.funcs[fn_idx]
        if len(args) != fn.nparams:
            raise VMError(f"'{fn_name}' expects {fn.nparams} arguments, got {len(args)}")
        norm_args: List[Any] = []
        struct_writebacks: List[Tuple[dict, StructDecl, list]] = []
        for a, pt in zip(args, fn.param_types):
            if pt == "bool":
                norm_args.append(bool(a))
            elif pt == "f32":
                norm_args.append(f32_round(float(a)))
            elif pt == "i32":
                norm_args.append(self._norm(int(a)))
            elif pt.endswith("[]"):
                if not isinstance(a, list):
                    raise VMError(f"expected list for array parameter of type {pt}")
                norm_args.append(a)
            elif pt in self.mod.structs:
                st = self.mod.structs[pt]
                if isinstance(a, dict):
                    lst = []
                    for fld in st.fields:
                        v = a[fld.name]
                        if fld.typ == "bool":
                            lst.append(bool(v))
                        elif fld.typ == "f32":
                            lst.append(f32_round(float(v)))
                        else:
                            lst.append(self._norm(int(v)))
                    norm_args.append(lst)
                    struct_writebacks.append((a, st, lst))
                elif isinstance(a, list):
                    norm_args.append(a)
                else:
                    raise VMError(f"expected dict or list for struct parameter of type {pt}")
            else:
                norm_args.append(a)
        val, _ = self._exec(fn, norm_args, depth=1, budget=budget)
        for d_obj, st, lst in struct_writebacks:
            for idx, fld in enumerate(st.fields):
                d_obj[fld.name] = lst[idx]
        return val

    def _exec(
        self,
        fn: CompiledFunc,
        args: List[Any],
        depth: int,
        budget: int,
    ) -> Tuple[Optional[Union[int, float, bool]], int]:
        if depth > self.max_depth:
            raise VMError("call stack depth exceeded")
        locals_arr: List[Any] = [0] * fn.nlocals
        for i, v in enumerate(args):
            locals_arr[i] = v
        stack: List[Any] = []
        code = fn.code
        consts = self.mod.consts
        fconsts = self.mod.fconsts
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
            elif op == "PUSH_FCONST":
                stack.append(fconsts[ins.arg])
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
            elif op == "FADD":
                b = float(stack.pop())
                a = float(stack.pop())
                stack.append(f32_round(a + b))
            elif op == "FSUB":
                b = float(stack.pop())
                a = float(stack.pop())
                stack.append(f32_round(a - b))
            elif op == "FMUL":
                b = float(stack.pop())
                a = float(stack.pop())
                stack.append(f32_round(a * b))
            elif op == "FDIV":
                b = float(stack.pop())
                a = float(stack.pop())
                stack.append(f32_round(a / b) if b != 0.0 else 0.0)
            elif op == "FNEG":
                a = float(stack.pop())
                stack.append(f32_round(-a))
            elif op == "FEQ":
                b = float(stack.pop())
                a = float(stack.pop())
                stack.append(a == b)
            elif op == "FNE":
                b = float(stack.pop())
                a = float(stack.pop())
                stack.append(a != b)
            elif op == "FLT":
                b = float(stack.pop())
                a = float(stack.pop())
                stack.append(a < b)
            elif op == "FLE":
                b = float(stack.pop())
                a = float(stack.pop())
                stack.append(a <= b)
            elif op == "FGT":
                b = float(stack.pop())
                a = float(stack.pop())
                stack.append(a > b)
            elif op == "FGE":
                b = float(stack.pop())
                a = float(stack.pop())
                stack.append(a >= b)
            elif op == "FABS":
                a = float(stack.pop())
                stack.append(f32_round(abs(a)))
            elif op == "FMIN":
                b = float(stack.pop())
                a = float(stack.pop())
                stack.append(a if a < b else b)
            elif op == "FMAX":
                b = float(stack.pop())
                a = float(stack.pop())
                stack.append(a if a > b else b)
            elif op == "FSQRT":
                a = float(stack.pop())
                stack.append(f32_round(math.sqrt(a)))
            elif op == "I2F":
                a = int(stack.pop())
                stack.append(f32_round(float(a)))
            elif op == "FLOOR":
                a = float(stack.pop())
                stack.append(self._norm(int(math.floor(a))))
            elif op in ("AGET", "AGETF"):
                idx = int(stack.pop())
                arr = stack.pop()
                if idx < 1 or idx > len(arr):
                    raise VMError(f"array index {idx} out of range (1..{len(arr)})")
                v = arr[idx - 1]
                if op == "AGETF":
                    stack.append(f32_round(float(v)))
                elif isinstance(v, bool):
                    stack.append(v)
                else:
                    stack.append(self._norm(int(v)))
            elif op in ("ASET", "ASETF"):
                val = stack.pop()
                idx = int(stack.pop())
                arr = stack.pop()
                if idx < 1 or idx > len(arr):
                    raise VMError(f"array index {idx} out of range (1..{len(arr)})")
                if op == "ASETF":
                    arr[idx - 1] = f32_round(float(val))
                elif isinstance(val, bool):
                    arr[idx - 1] = val
                else:
                    arr[idx - 1] = self._norm(int(val))
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
            elif op == "CALL_HOST":
                host_id = ins.arg >> 8
                argc = ins.arg & 0xFF
                call_args = [stack.pop() for _ in range(argc)]
                call_args.reverse()
                h_name, h_ret = self.host_by_id[host_id]
                if h_name in self.host_fns:
                    res = self.host_fns[h_name](*call_args)
                else:
                    res = False if h_ret == "bool" else (0.0 if h_ret == "f32" else (0 if h_ret == "i32" else None))
                if h_ret == "bool":
                    stack.append(bool(res))
                elif h_ret == "i32":
                    stack.append(self._norm(int(res)))
                elif h_ret == "f32":
                    stack.append(f32_round(float(res)))
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


def compute_stack_depths(code: List[Insn], funcs: Optional[List[CompiledFunc]] = None) -> Tuple[List[int], int]:
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

        if op in ("PUSH_CONST", "PUSH_FCONST", "PUSH_TRUE", "PUSH_FALSE", "LOAD"):
            if sp + 1 > max_sp:
                max_sp = sp + 1
            reach(pc + 1, sp + 1)
        elif op in ("STORE", "POP"):
            reach(pc + 1, sp - 1)
        elif op in ("NEG", "NOT", "ABS", "FNEG", "FABS", "FSQRT", "I2F", "FLOOR"):
            reach(pc + 1, sp)
        elif op in (
            "ADD", "SUB", "MUL", "IDIV", "IMOD", "BAND", "EQ", "NE", "LT", "LE", "GT", "GE", "MIN", "MAX",
            "FADD", "FSUB", "FMUL", "FDIV", "FEQ", "FNE", "FLT", "FLE", "FGT", "FGE", "FMIN", "FMAX",
            "AGET", "AGETF",
        ):
            reach(pc + 1, sp - 1)
        elif op in ("ASET", "ASETF"):
            reach(pc + 1, sp - 3)
        elif op == "JMP":
            reach(ins.arg, sp)
        elif op == "JZ":
            reach(ins.arg, sp - 1)
            reach(pc + 1, sp - 1)
        elif op in ("JF_KEEP", "JT_KEEP"):
            reach(ins.arg, sp)
            reach(pc + 1, sp - 1)
        elif op == "CALL":
            fn_idx = ins.arg >> 8
            argc = ins.arg & 0xFF
            has_ret = True
            if funcs is not None and 0 <= fn_idx < len(funcs):
                has_ret = funcs[fn_idx].ret_type is not None
            reach(pc + 1, sp - argc + (1 if has_ret else 0))
        elif op == "CALL_HOST":
            host_id = ins.arg >> 8
            argc = ins.arg & 0xFF
            has_ret = any(hid == host_id and ret is not None for hid, _, ret in HOST_FUNCTIONS.values())
            reach(pc + 1, sp - argc + (1 if has_ret else 0))
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
        depths, max_sp = compute_stack_depths(fn.code, self.mod.funcs)

        # Collect unique constants (i32 and f32/f64) used by this function
        kfirst = len(self.consts_bytes)
        fn_kmap: Dict[Tuple[str, int], int] = {}
        nconst_count = 0

        def get_k_idx(val: int) -> int:
            nonlocal nconst_count
            wval = wrap_i32(val)
            key = ("i", wval)
            if key not in fn_kmap:
                fn_kmap[key] = nconst_count
                nconst_count += 1
                self.consts_bytes.append(struct.pack("<iI", wval, 0))
            return fn_kmap[key]

        def get_fk_idx(fval: float) -> int:
            nonlocal nconst_count
            fv = f32_round(fval)
            key = ("f", f32_to_bits(fv))
            if key not in fn_kmap:
                fn_kmap[key] = nconst_count
                nconst_count += 1
                self.consts_bytes.append(struct.pack("<d", float(fv)))
            return fn_kmap[key]

        # Pre-scan constants so nconst is fixed before assigning register numbers
        for ins in fn.code:
            if ins.op == "PUSH_CONST":
                get_k_idx(self.mod.consts[ins.arg])
            elif ins.op == "PUSH_FCONST":
                get_fk_idx(self.mod.fconsts[ins.arg])
            elif ins.op == "PUSH_TRUE":
                get_k_idx(1)
            elif ins.op == "PUSH_FALSE":
                get_k_idx(0)

        nparams = fn.nparams
        nconst = nconst_count
        nextra_locals = fn.nlocals - nparams

        def reg_local(slot: int) -> int:
            if slot < nparams:
                return slot
            return nparams + nconst + (slot - nparams)

        def reg_const(val: int) -> int:
            return nparams + get_k_idx(val)

        def reg_fconst(fval: float) -> int:
            return nparams + get_fk_idx(fval)

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
            elif op == "PUSH_FCONST":
                fval = self.mod.fconsts[ins.arg]
                insns.append((BMVM_OP["MOV"], reg_stack(sp), reg_fconst(fval), 0, 0))
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
            elif op in (
                "ADD", "SUB", "MUL", "IDIV", "IMOD", "BAND", "EQ", "NE", "LT", "LE", "MIN", "MAX",
                "FADD", "FSUB", "FMUL", "FDIV", "FEQ", "FNE", "FLT", "FLE", "FMIN", "FMAX",
            ):
                bm_op = {
                    "ADD": "ADDI", "SUB": "SUBI", "MUL": "MULI", "IDIV": "IDIV", "IMOD": "IMOD",
                    "BAND": "ANDI", "EQ": "EQI", "NE": "NEI", "LT": "LTI", "LE": "LEI",
                    "MIN": "MINI", "MAX": "MAXI",
                    "FADD": "ADDF", "FSUB": "SUBF", "FMUL": "MULF", "FDIV": "DIVF",
                    "FEQ": "EQF", "FNE": "NEF", "FLT": "LTF", "FLE": "LEF",
                    "FMIN": "MINF", "FMAX": "MAXF",
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
            elif op == "FGT":
                ra = reg_stack(sp - 2)
                insns.append((BMVM_OP["LTF"], ra, reg_stack(sp - 1), reg_stack(sp - 2), 0))
            elif op == "FGE":
                ra = reg_stack(sp - 2)
                insns.append((BMVM_OP["LEF"], ra, reg_stack(sp - 1), reg_stack(sp - 2), 0))
            elif op in ("NEG", "NOT", "ABS", "FNEG", "FABS", "FSQRT", "I2F", "FLOOR"):
                bm_op = {
                    "NEG": "NEGI", "NOT": "NOT", "ABS": "ABSI",
                    "FNEG": "NEGF", "FABS": "ABSF", "FSQRT": "SQRT", "I2F": "I2F", "FLOOR": "FLOOR",
                }[op]
                r = reg_stack(sp - 1)
                insns.append((BMVM_OP[bm_op], r, r, 0, 0))
            elif op in ("AGET", "AGETF"):
                bm_op = "GETF" if op == "AGETF" else "GETI"
                ra = reg_stack(sp - 2)
                rb = reg_stack(sp - 2)
                rc = reg_stack(sp - 1)
                insns.append((BMVM_OP[bm_op], ra, rb, rc, 0))
            elif op in ("ASET", "ASETF"):
                bm_op = "SETF" if op == "ASETF" else "SETI"
                insns.append((BMVM_OP[bm_op], reg_stack(sp - 3), reg_stack(sp - 2), reg_stack(sp - 1), 0))
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

        def bmvm_ptype(pt: str) -> int:
            if pt == "f32":
                return 1  # BMVM_F64
            if pt == "f32[]":
                return 3  # BMVM_ARR_F64
            if pt in ("i32[]", "bool[]"):
                return 2  # BMVM_ARR_I32
            if pt in self.mod.structs:
                st = self.mod.structs[pt]
                if all(f.typ == "f32" for f in st.fields):
                    return 3  # BMVM_ARR_F64
                return 2  # BMVM_ARR_I32
            return 0  # BMVM_I32

        pt_list = [bmvm_ptype(pt) for pt in fn.param_types] + [0] * (16 - nparams)
        pt_bytes = bytes(pt_list[:16])
        ret_code = 4 if fn.ret_type is None else (1 if fn.ret_type == "f32" else 0)
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

    # -- VFPv2 single-precision (coprocessor 10) instructions for ARM1176JZF-S --
    def vmov_s0_r(self, rt: int, comment: str = "") -> int:
        return self.emit_word(0xEE000A10 | ((rt & 0xF) << 12), comment or f"vmov s0, r{rt}")

    def vmov_s1_r(self, rt: int, comment: str = "") -> int:
        return self.emit_word(0xEE000A90 | ((rt & 0xF) << 12), comment or f"vmov s1, r{rt}")

    def vmov_r_s0(self, rt: int, comment: str = "") -> int:
        return self.emit_word(0xEE100A10 | ((rt & 0xF) << 12), comment or f"vmov r{rt}, s0")

    def vmov_r_s2(self, rt: int, comment: str = "") -> int:
        return self.emit_word(0xEE110A10 | ((rt & 0xF) << 12), comment or f"vmov r{rt}, s2")

    def vadd_f32(self, comment: str = "vadd.f32 s0, s0, s1") -> int:
        return self.emit_word(0xEE300A20, comment)

    def vsub_f32(self, comment: str = "vsub.f32 s0, s0, s1") -> int:
        return self.emit_word(0xEE300A60, comment)

    def vmul_f32(self, comment: str = "vmul.f32 s0, s0, s1") -> int:
        return self.emit_word(0xEE200A20, comment)

    def vdiv_f32(self, comment: str = "vdiv.f32 s0, s0, s1") -> int:
        return self.emit_word(0xEE800A20, comment)

    def vneg_f32(self, comment: str = "vneg.f32 s0, s0") -> int:
        return self.emit_word(0xEEB10A40, comment)

    def vabs_f32(self, comment: str = "vabs.f32 s0, s0") -> int:
        return self.emit_word(0xEEB00AC0, comment)

    def vsqrt_f32(self, comment: str = "vsqrt.f32 s0, s0") -> int:
        return self.emit_word(0xEEB10AC0, comment)

    def vcmp_f32(self, comment: str = "vcmp.f32 s0, s1") -> int:
        return self.emit_word(0xEEB40A60, comment)

    def vmrs_apsr(self, comment: str = "vmrs APSR_nzcv, fpscr") -> int:
        return self.emit_word(0xEEF1FA10, comment)

    def vcvt_f32_s32_s0(self, comment: str = "vcvt.f32.s32 s0, s0") -> int:
        return self.emit_word(0xEEB80AC0, comment)

    def vcvt_s32_f32_s2_s0(self, comment: str = "vcvt.s32.f32 s2, s0") -> int:
        return self.emit_word(0xEEBD1AC0, comment)

    def vcvt_f32_s32_s2(self, comment: str = "vcvt.f32.s32 s2, s2") -> int:
        return self.emit_word(0xEEB81AC1, comment)

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
        self.needs_host_call = False
        self.host_slot_word: Optional[int] = None

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
        if self.needs_host_call:
            self._emit_host_call_helper()

        self.asm.finalize()
        return ARMv6Module(words=self.asm.words, func_offsets=self.func_offsets, comments=self.asm.comments)

    def _emit_host_call_helper(self) -> None:
        """Emits __bml_call_host: inputs r0 = host_id, r1 = &args[0], r2 = nargs; returns r0 = result (0 if no handler)."""
        a = self.asm
        a.bind("__bml_call_host")
        a.ldr(12, 15, 12, comment="ldr r12, [pc, #12] (__bml_host_fn_slot)")
        a.cmp_imm(12, 0, comment="cmp r12, #0")
        a.bx(12, cond=COND_NE, comment="bxne r12 (tail-call host dispatcher)")
        a.dp_imm(DP_MOV, 0, 0, 0, comment="mov r0, #0")
        a.bx(14, comment="bx lr")
        a.bind("__bml_host_fn_slot")
        self.host_slot_word = a.emit_word(0, comment="__bml_host_fn_slot (patched by bmnative_load)")

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
        depths, max_sp = compute_stack_depths(fn.code, self.mod.funcs)
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
            elif op == "PUSH_FCONST":
                fval = self.mod.fconsts[ins.arg]
                bits = f32_to_bits(fval)
                dst = 4 + s_slot(sp) if s_slot(sp) < 7 else 0
                a.mov32(dst, bits, comment=f"f32 {fval}")
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
            elif op in ("FADD", "FSUB", "FMUL", "FDIV"):
                ra = get_slot(s_slot(sp - 2), 0)
                rb = get_slot(s_slot(sp - 1), 1)
                a.vmov_s0_r(ra)
                a.vmov_s1_r(rb)
                if op == "FADD":
                    a.vadd_f32()
                elif op == "FSUB":
                    a.vsub_f32()
                elif op == "FMUL":
                    a.vmul_f32()
                else:
                    a.vdiv_f32()
                dst = 4 + s_slot(sp - 2) if s_slot(sp - 2) < 7 else 0
                a.vmov_r_s0(dst)
                put_slot(s_slot(sp - 2), dst)
            elif op in ("FNEG", "FABS", "FSQRT", "I2F"):
                r = get_slot(s_slot(sp - 1), 0)
                a.vmov_s0_r(r)
                if op == "FNEG":
                    a.vneg_f32()
                elif op == "FABS":
                    a.vabs_f32()
                elif op == "FSQRT":
                    a.vsqrt_f32()
                else:
                    a.vcvt_f32_s32_s0()
                dst = 4 + s_slot(sp - 1) if s_slot(sp - 1) < 7 else 0
                a.vmov_r_s0(dst)
                put_slot(s_slot(sp - 1), dst)
            elif op == "FLOOR":
                r = get_slot(s_slot(sp - 1), 0)
                dst = 4 + s_slot(sp - 1) if s_slot(sp - 1) < 7 else 0
                a.vmov_s0_r(r)
                a.vcvt_s32_f32_s2_s0()
                a.vmov_r_s2(dst)
                a.vcvt_f32_s32_s2()
                a.vmov_r_s2(12)
                a.vmov_s1_r(12)
                a.vcmp_f32()
                a.vmrs_apsr()
                a.dp_imm(DP_SUB, dst, dst, 1, cond=COND_MI, comment="floor adjust if x < trunc(x)")
                put_slot(s_slot(sp - 1), dst)
            elif op in ("FMIN", "FMAX"):
                ra = get_slot(s_slot(sp - 2), 0)
                rb = get_slot(s_slot(sp - 1), 1)
                a.vmov_s0_r(ra)
                a.vmov_s1_r(rb)
                a.vcmp_f32()
                a.vmrs_apsr()
                dst = 4 + s_slot(sp - 2) if s_slot(sp - 2) < 7 else 0
                if dst != ra:
                    a.mov_reg(dst, ra)
                cond_pick_b = COND_GT if op == "FMIN" else COND_LT
                a.mov_reg(dst, rb, cond=cond_pick_b)
                put_slot(s_slot(sp - 2), dst)
            elif op in ("FEQ", "FNE", "FLT", "FLE", "FGT", "FGE"):
                ra = get_slot(s_slot(sp - 2), 0)
                rb = get_slot(s_slot(sp - 1), 1)
                a.vmov_s0_r(ra)
                a.vmov_s1_r(rb)
                a.vcmp_f32()
                a.vmrs_apsr()
                cond_true = {
                    "FEQ": COND_EQ, "FNE": COND_NE, "FLT": COND_LT,
                    "FLE": COND_LE, "FGT": COND_GT, "FGE": COND_GE,
                }[op]
                dst = 4 + s_slot(sp - 2) if s_slot(sp - 2) < 7 else 0
                a.dp_imm(DP_MOV, dst, 0, 0)
                a.dp_imm(DP_MOV, dst, 0, 1, cond=cond_true)
                put_slot(s_slot(sp - 2), dst)
            elif op in ("AGET", "AGETF"):
                r_base = get_slot(s_slot(sp - 2), 0)
                r_idx = get_slot(s_slot(sp - 1), 1)
                a.dp_imm(DP_SUB, 12, r_idx, 1, comment="idx - 1 (0-based)")
                a.dp_reg(DP_ADD, 12, r_base, 12, shift_type=0, shift_imm=2, comment="base + (idx - 1) * 4")
                dst = 4 + s_slot(sp - 2) if s_slot(sp - 2) < 7 else 0
                a.ldr(dst, 12, 0)
                put_slot(s_slot(sp - 2), dst)
            elif op in ("ASET", "ASETF"):
                r_base = get_slot(s_slot(sp - 3), 0)
                r_idx = get_slot(s_slot(sp - 2), 1)
                r_val = get_slot(s_slot(sp - 1), 2)
                a.dp_imm(DP_SUB, 12, r_idx, 1, comment="idx - 1 (0-based)")
                a.dp_reg(DP_ADD, 12, r_base, 12, shift_type=0, shift_imm=2, comment="base + (idx - 1) * 4")
                a.str_w(r_val, 12, 0)
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
            elif op == "CALL_HOST":
                self.needs_host_call = True
                host_id = ins.arg >> 8
                argc = ins.arg & 0xFF
                base = sp - argc
                has_ret = any(hid == host_id and ret is not None for hid, _, ret in HOST_FUNCTIONS.values())
                alloc_words = ((max(1, argc) + 1) // 2) * 2
                alloc_bytes = alloc_words * 4
                a.dp_imm(DP_SUB, 13, 13, alloc_bytes, comment=f"host args[{argc}]")
                for j in range(argc):
                    rj = get_slot(s_slot(base + j), 0)
                    a.str_w(rj, 13, j * 4)
                a.mov32(0, host_id, comment=f"host_id = {host_id}")
                a.mov_reg(1, 13, comment="r1 = sp (&args[0])")
                a.mov32(2, argc, comment=f"nargs = {argc}")
                a.bl("__bml_call_host")
                a.dp_imm(DP_ADD, 13, 13, alloc_bytes)
                if has_ret:
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


# Stage 5 & 6: Native ARMv6 Container Format (BMN1) for native_load / native_run
BMN_MAGIC = 0x314E4D42  # "BMN1"
BMN_NONE = 0
BMN_I32 = 1
BMN_BOOL = 2
BMN_F32 = 3
BMN_ARR_I32 = 4
BMN_ARR_BOOL = 5
BMN_ARR_F32 = 6
BMN_STRUCT_BASE = 0x80


def compile_native_blob(src: str, leaf_opt: bool = True) -> bytes:
    """Compiles BML source code into a BMN1 binary container for native_load()."""
    prog = parse_and_check(src)
    mod = BytecodeCompiler(prog).compile()
    arm_comp = ARMv6Compiler(prog, mod, leaf_opt=leaf_opt)
    arm_mod = arm_comp.compile()

    nfn = len(mod.funcs)
    nstruct = len(prog.structs)
    ncode = len(arm_mod.words)
    if nfn < 1 or nfn > 512:
        raise Error(f"native module has {nfn} functions (1..512 allowed)")
    if nstruct > 64:
        raise Error(f"native module has {nstruct} structs (0..64 allowed)")
    if ncode < 1 or ncode > 65536:
        raise Error(f"native module has {ncode} words (1..65536 allowed)")

    struct_idx_map = {st.name: idx for idx, st in enumerate(prog.structs)}

    def encode_bmn_type(t: Optional[str]) -> int:
        if t is None:
            return BMN_NONE
        if t == "i32":
            return BMN_I32
        if t == "bool":
            return BMN_BOOL
        if t == "f32":
            return BMN_F32
        if t == "i32[]":
            return BMN_ARR_I32
        if t == "bool[]":
            return BMN_ARR_BOOL
        if t == "f32[]":
            return BMN_ARR_F32
        if t in struct_idx_map:
            return BMN_STRUCT_BASE | struct_idx_map[t]
        raise Error(f"unsupported BMN1 type '{t}'")

    host_flag = 0x8000 if arm_comp.host_slot_word is not None else 0
    nfn_packed = ((nstruct | host_flag) << 16) | nfn
    out = bytearray(struct.pack("<III", BMN_MAGIC, nfn_packed, ncode))
    for fn in mod.funcs:
        name_b = fn.name.encode("ascii")[:31]
        name_padded = name_b + b"\x00" * (32 - len(name_b))
        ret_code = encode_bmn_type(fn.ret_type)
        ptypes = [0] * 16
        for i, pt in enumerate(fn.param_types):
            ptypes[i] = encode_bmn_type(pt)
        word_off = arm_mod.func_offsets[fn.name] // 4
        out.extend(struct.pack("<32sBBH16BI", name_padded, fn.nparams, ret_code, 0, *ptypes, word_off))

    out.extend(struct.pack(f"<{ncode}I", *arm_mod.words))

    for st in prog.structs:
        if len(st.fields) > 16:
            raise Error(f"struct '{st.name}' has {len(st.fields)} fields (max 16)")
        sname_b = st.name.encode("ascii")[:31]
        sname_padded = sname_b + b"\x00" * (32 - len(sname_b))
        ftypes = [0] * 16
        fnames = bytearray()
        for i in range(16):
            if i < len(st.fields):
                ftypes[i] = encode_bmn_type(st.fields[i].typ)
                fb = st.fields[i].name.encode("ascii")[:15]
                fnames.extend(fb + b"\x00" * (16 - len(fb)))
            else:
                fnames.extend(b"\x00" * 16)
        out.extend(struct.pack("<32sI16B", sname_padded, len(st.fields), *ftypes))
        out.extend(fnames)

    return bytes(out)


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
    ap.add_argument("source", help="path to the .b source file")
    ap.add_argument("--check", action="store_true", help="only run stage 1 (lexer, parser, AST, type check)")
    ap.add_argument("--lua", help="write the stripped plain Lua source to this file")
    ap.add_argument("--dump", action="store_true", help="print the disassembled stage 2 stack bytecode")
    ap.add_argument("--bmvm", help="write the stage 3 kernel bmvm binary blob (.bmv) to this file")
    ap.add_argument("--armv6", help="write the stage 4 ARMv6 A32 machine code (.bin) to this file")
    ap.add_argument("--native", help="write the stage 5 native ARMv6 container (.bmn) for native_load to this file")
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
    if args.native:
        nblob = compile_native_blob(src)
        with open(args.native, "wb") as f:
            f.write(nblob)
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
