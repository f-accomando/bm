#!/usr/bin/env python3
"""bmt.py: a typed subset of Lua for the hot functions of a game (prototype).

One source gives two outputs:
  - plain Lua: the same text with the types taken out. The VM runs it.
  - C: the same function in native code, with checked types.

  bmt.py hot.bmt --lua hot.lua --c hot.c

The language
  types       i32  f32  f64  bool   and arrays of them: f64[]  i32[]  bool[]
  function    function name(a: f64, v: f64[], n: i32): bool ... end   (the return type is optional)
  statements  local x = e     for i = a, b do ... end     while c do ... end     if / elseif / else / end
              return e        break        x = e        v[i] = e
  expressions + - * / // %  &  < <= > >= == ~=  and or not  v[i]  numbers  true false
              sqrt(e)  abs(e)  floor(e)  min(a, b)  max(a, b)  name(args) (a function defined above)
              (& is the bitwise and of two i32, below + and - and above the comparisons, as in Lua)
              // and % round toward minus infinity, as in Lua. A division by zero gives 0 in C (Lua stops with an
              error on integers): the source must not divide by zero. floor gives an i32.
Rules
  - Arrays count from 1, as in Lua. The C code subtracts 1. The size of an array is a separate i32.
  - A local has the type of its first value. i32 widens to f32 and f64. A float never narrows to i32:
    that is an error, not a silent cut.
  - `/` always gives f64, as in Lua. `and`, `or` and `not` take bool only.
  - No closures, no strings, no tables. A function calls the functions defined above it and sqrt, abs, floor, min, max.
    An array goes to a function by its name. The Lua output needs sqrt, abs, floor, min and max as names of the host
    (math.sqrt, math.abs, math.floor, math.min, math.max).
"""
import argparse
import re
import sys

TOKEN = re.compile(
    r"(?P<ws>\s+)|(?P<comment>--[^\n]*)|(?P<num>\d+\.\d*|\.\d+|\d+)|(?P<name>[A-Za-z_]\w*)"
    r"|(?P<op><=|>=|==|~=|//|[-+*/%&(),:\[\]=<>])"
)
KEYWORDS = {"function", "local", "for", "do", "while", "end", "if", "then", "elseif", "else", "return", "break", "and", "or",
            "not", "true", "false"}
SCALARS = ("i32", "f32", "f64", "bool")
CTYPE = {"i32": "int32_t", "f32": "float", "f64": "double", "bool": "bool"}
RANK = {"i32": 0, "f32": 1, "f64": 2}


class Error(Exception):
    pass


def numeric(t):
    return t in RANK


def widens(dst, src):
    return dst == src or (numeric(dst) and numeric(src) and RANK[src] < RANK[dst])


class Parser:
    def __init__(self, src):
        self.src = src
        self.toks = []
        pos = 0
        while pos < len(src):
            m = TOKEN.match(src, pos)
            if not m:
                raise Error("line %d: unexpected character %r" % (self.line_at(pos), src[pos]))
            if m.lastgroup not in ("ws", "comment"):
                text = m.group()
                kind = "kw" if m.lastgroup == "name" and text in KEYWORDS else m.lastgroup
                self.toks.append((kind, text, m.start(), m.end()))
            pos = m.end()
        self.i = 0
        self.cuts = []          # spans of the source that are types: they leave the Lua output
        self.vars = {}
        self.ret = None
        self.funcs = {}         # the functions read so far: name -> (parameter types, return type)
        self.stmt_start = 0     # the token where the statement being read begins: a type error points there

    # -- helpers
    def line_at(self, pos):
        return self.src.count("\n", 0, pos) + 1

    def fail(self, msg, here=False):
        """A syntax error points at the token it found (here=True). A type error points at the start of its statement."""
        at = self.i if here else self.stmt_start
        pos = self.toks[min(at, len(self.toks) - 1)][2] if self.toks else 0
        raise Error("line %d: %s" % (self.line_at(pos), msg))

    def peek(self):
        return self.toks[self.i][1] if self.i < len(self.toks) else ""

    def kind(self):
        return self.toks[self.i][0] if self.i < len(self.toks) else ""

    def take(self):
        if self.i >= len(self.toks):
            self.fail("the file ends too soon", True)
        t = self.toks[self.i]
        self.i += 1
        return t

    def accept(self, text):
        if self.peek() == text and self.kind() in ("kw", "op"):
            self.i += 1
            return True
        return False

    def expect(self, text):
        if not self.accept(text):
            self.fail("expected '%s', found '%s'" % (text, self.peek()), True)

    def name(self):
        if self.kind() != "name":
            self.fail("expected a name, found '%s'" % self.peek(), True)
        return self.take()[1]

    # -- types
    def type_after_colon(self):
        """Reads ': type' and records its span as a cut for the Lua output."""
        start = self.toks[self.i][2]
        self.expect(":")
        base = self.name()
        if base not in SCALARS:
            self.fail("unknown type '%s'" % base, True)
        arr = False
        if self.peek() == "[":
            self.i += 1
            self.expect("]")
            arr = True
        end = self.toks[self.i - 1][3]
        self.cuts.append((start, end))
        return base + "[]" if arr else base

    # -- program
    def program(self):
        out = []
        while self.i < len(self.toks):
            out.append(self.function())
        return out

    def function(self):
        self.stmt_start = self.i
        self.expect("function")
        fname = self.name()
        self.expect("(")
        self.vars = {}
        params = []
        if self.peek() != ")":
            while True:
                pname = self.name()
                ptype = self.type_after_colon()
                if pname in self.vars:
                    self.fail("the parameter '%s' is twice" % pname)
                self.vars[pname] = ptype
                params.append((pname, ptype))
                if not self.accept(","):
                    break
        self.expect(")")
        self.ret = None
        if self.peek() == ":":
            self.ret = self.type_after_colon()
            if self.ret.endswith("[]"):
                self.fail("a function cannot return an array")
        self.funcs[fname] = ([t for _, t in params], self.ret)    # (known inside its own body: no recursion is checked)
        body = self.block({"end"})
        self.expect("end")
        cparams = ", ".join(self.cdecl(t, n) for n, t in params) or "void"
        head = "%s bmt_%s(%s)" % (CTYPE[self.ret] if self.ret else "void", fname, cparams)
        return "%s\n{\n%s\n}\n" % (head, "\n".join(indent(body)))

    @staticmethod
    def cdecl(t, n):
        return "%s *%s" % (CTYPE[t[:-2]], n) if t.endswith("[]") else "%s %s" % (CTYPE[t], n)

    # -- statements
    def block(self, stops):
        lines = []
        while self.i < len(self.toks) and self.peek() not in stops:
            lines += self.statement()
        return lines

    def statement(self):
        self.stmt_start = self.i
        t = self.peek()
        if t == "local":
            self.i += 1
            n = self.name()
            self.expect("=")
            e, et = self.expr()
            if et.endswith("[]"):
                self.fail("a local cannot hold an array")
            if n in self.vars:
                self.fail("'%s' is declared twice" % n)
            self.vars[n] = et
            return ["%s %s = %s;" % (CTYPE[et], n, e)]
        if t == "for":
            self.i += 1
            n = self.name()
            self.expect("=")
            a, at = self.expr()
            self.expect(",")
            b, bt = self.expr()
            if at != "i32" or bt != "i32":
                self.fail("the bounds of a for loop are i32")
            self.expect("do")
            if n in self.vars:
                self.fail("'%s' is declared twice" % n)
            self.vars[n] = "i32"
            body = self.block({"end"})
            self.expect("end")
            del self.vars[n]
            end = n + "__end"
            return (["{", "    int32_t %s = %s;" % (end, b),
                     "    for (int32_t %s = %s; %s <= %s; %s++) {" % (n, a, n, end, n)]
                    + indent(indent(body)) + ["    }", "}"])
        if t == "while":
            self.i += 1
            c, ct = self.expr()
            if ct != "bool":
                self.fail("the test of a while is bool, not %s" % ct)
            self.expect("do")
            body = self.block({"end"})
            self.expect("end")
            return ["while (%s) {" % c] + indent(body) + ["}"]
        if t == "if":
            self.i += 1
            c, ct = self.expr()
            if ct != "bool":
                self.fail("the test of an if is bool, not %s" % ct)
            self.expect("then")
            lines = ["if (%s) {" % c] + indent(self.block({"elseif", "else", "end"}))
            while self.peek() == "elseif":
                self.i += 1
                c, ct = self.expr()
                if ct != "bool":
                    self.fail("the test of an elseif is bool, not %s" % ct)
                self.expect("then")
                lines += ["} else if (%s) {" % c] + indent(self.block({"elseif", "else", "end"}))
            if self.accept("else"):
                lines += ["} else {"] + indent(self.block({"end"}))
            self.expect("end")
            return lines + ["}"]
        if t == "return":
            self.i += 1
            if self.peek() in ("end", "else", "elseif") or self.i >= len(self.toks):
                if self.ret:
                    self.fail("return needs a value of type %s" % self.ret)
                return ["return;"]
            e, et = self.expr()
            if not self.ret:
                self.fail("this function has no return type")
            if not widens(self.ret, et):
                self.fail("return of %s in a function of %s" % (et, self.ret))
            return ["return %s;" % e]
        if t == "break":
            self.i += 1
            return ["break;"]
        if self.kind() == "name":
            n = self.name()
            if n not in self.vars:
                self.fail("'%s' is not declared" % n)
            vt = self.vars[n]
            if self.accept("["):
                if not vt.endswith("[]"):
                    self.fail("'%s' is not an array" % n)
                idx, it = self.expr()
                self.expect("]")
                if it != "i32":
                    self.fail("an index is i32, not %s" % it)
                self.expect("=")
                e, et = self.expr()
                if not widens(vt[:-2], et):
                    self.fail("%s stored in an array of %s" % (et, vt[:-2]))
                return ["%s[(%s) - 1] = %s;" % (n, idx, e)]
            if vt.endswith("[]"):
                self.fail("'%s' is an array: write an element" % n)
            self.expect("=")
            e, et = self.expr()
            if not widens(vt, et):
                self.fail("%s assigned to '%s' of %s (no silent narrowing)" % (et, n, vt))
            return ["%s = %s;" % (n, e)]
        self.fail("a statement cannot start with '%s'" % t, True)

    # -- expressions: each returns (C text, type)
    def expr(self):
        return self.or_()

    def or_(self):
        l, lt = self.and_()
        while self.accept("or"):
            r, rt = self.and_()
            self.need_bool(lt, rt, "or")
            l, lt = "(%s || %s)" % (l, r), "bool"
        return l, lt

    def and_(self):
        l, lt = self.cmp()
        while self.accept("and"):
            r, rt = self.cmp()
            self.need_bool(lt, rt, "and")
            l, lt = "(%s && %s)" % (l, r), "bool"
        return l, lt

    def need_bool(self, a, b, op):
        if a != "bool" or b != "bool":
            self.fail("'%s' takes bool, not %s and %s" % (op, a, b))

    def band(self):
        l, lt = self.add()
        while self.kind() == "op" and self.peek() == "&":
            self.i += 1
            r, rt = self.add()
            if lt != "i32" or rt != "i32":
                self.fail("'&' takes i32, not %s and %s" % (lt, rt))
            l, lt = "(%s & %s)" % (l, r), "i32"
        return l, lt

    def cmp(self):
        l, lt = self.band()
        if self.kind() == "op" and self.peek() in ("<", "<=", ">", ">=", "==", "~="):
            op = self.take()[1]
            r, rt = self.band()
            if not ((numeric(lt) and numeric(rt)) or (lt == rt == "bool" and op in ("==", "~="))):
                self.fail("cannot compare %s with %s" % (lt, rt))
            return "(%s %s %s)" % (l, "!=" if op == "~=" else op, r), "bool"
        return l, lt

    def arith(self, l, lt, op, r, rt):
        if not (numeric(lt) and numeric(rt)):
            self.fail("'%s' takes numbers, not %s and %s" % (op, lt, rt))
        if op == "/":
            return "((double)(%s) / (double)(%s))" % (l, r), "f64"
        t = max(lt, rt, key=lambda x: RANK[x])
        if op in ("//", "%"):
            if lt == "i32" and rt == "i32":
                return "bmt_%s(%s, %s)" % ("idiv" if op == "//" else "imod", l, r), "i32"
            return "bmt_%s((double)(%s), (double)(%s))" % ("fdiv" if op == "//" else "fmod", l, r), "f64"

        return "(%s %s %s)" % (l, op, r), t

    def add(self):
        l, lt = self.mul()
        while self.kind() == "op" and self.peek() in ("+", "-"):
            op = self.take()[1]
            r, rt = self.mul()
            l, lt = self.arith(l, lt, op, r, rt)
        return l, lt

    def mul(self):
        l, lt = self.unary()
        while self.kind() == "op" and self.peek() in ("*", "/", "//", "%"):
            op = self.take()[1]
            r, rt = self.unary()
            l, lt = self.arith(l, lt, op, r, rt)
        return l, lt

    def unary(self):
        if self.accept("-"):
            e, et = self.unary()
            if not numeric(et):
                self.fail("unary '-' takes a number, not %s" % et)
            return "(-%s)" % e, et
        if self.accept("not"):
            e, et = self.unary()
            if et != "bool":
                self.fail("'not' takes bool, not %s" % et)
            return "(!%s)" % e, "bool"
        return self.primary()

    def call_arg(self):
        """An argument: an expression, or the name of an array (the whole array goes to the function)."""
        if self.kind() == "name" and self.peek() in self.vars and self.vars[self.peek()].endswith("[]"):
            nxt = self.toks[self.i + 1][1] if self.i + 1 < len(self.toks) else ""
            if nxt != "[":
                n = self.name()
                return n, self.vars[n]
        return self.expr()

    def call(self, n, args):
        def num(k):
            if len(args) != k:
                self.fail("'%s' takes %d argument%s" % (n, k, "" if k == 1 else "s"))
            for a, at in args:
                if not numeric(at):
                    self.fail("'%s' takes numbers, not %s" % (n, at))
        if n == "sqrt":
            num(1)
            return "sqrt((double)(%s))" % args[0][0], "f64"
        if n == "abs":
            num(1)
            return ("abs(%s)" % args[0][0] if args[0][1] == "i32" else "fabs(%s)" % args[0][0]), args[0][1]
        if n == "floor":
            num(1)
            return ("(%s)" % args[0][0] if args[0][1] == "i32" else "bmt_floor(%s)" % args[0][0]), "i32"
        if n in ("min", "max"):
            num(2)
            t = max(args[0][1], args[1][1], key=lambda x: RANK[x])
            return "bmt_%s_%s(%s, %s)" % (n, t, args[0][0], args[1][0]), t
        if n not in self.funcs:
            self.fail("'%s' is not a function of the subset (define it above)" % n)
        ptypes, ret = self.funcs[n]
        if ret is None:
            self.fail("'%s' gives no value" % n)
        if len(args) != len(ptypes):
            self.fail("'%s' takes %d arguments, not %d" % (n, len(ptypes), len(args)))
        for (a, at), pt in zip(args, ptypes):
            ok = at == pt if (pt.endswith("[]") or at.endswith("[]")) else widens(pt, at)
            if not ok:
                self.fail("'%s' gets %s where %s is expected" % (n, at, pt))
        return "bmt_%s(%s)" % (n, ", ".join(a for a, _ in args)), ret

    def primary(self):
        k = self.kind()
        if k == "num":
            text = self.take()[1]
            return text, ("f64" if "." in text else "i32")
        if self.accept("("):
            e, et = self.expr()
            self.expect(")")
            return e, et
        if self.accept("true"):
            return "true", "bool"
        if self.accept("false"):
            return "false", "bool"
        if k == "name":
            n = self.name()
            if self.peek() == "(" and self.kind() == "op":
                self.i += 1
                args = []
                if self.peek() != ")":
                    while True:
                        args.append(self.call_arg())
                        if not self.accept(","):
                            break
                self.expect(")")
                return self.call(n, args)
            if n not in self.vars:
                self.fail("'%s' is not declared" % n)
            vt = self.vars[n]
            if self.accept("["):
                if not vt.endswith("[]"):
                    self.fail("'%s' is not an array" % n)
                idx, it = self.expr()
                self.expect("]")
                if it != "i32":
                    self.fail("an index is i32, not %s" % it)
                return "%s[(%s) - 1]" % (n, idx), vt[:-2]
            if vt.endswith("[]"):
                self.fail("'%s' is an array: write an element" % n)
            return n, vt
        self.fail("an expression cannot start with '%s'" % self.peek(), True)


def indent(lines):
    return ["    " + l if l else l for l in lines]


HELPERS = """/* Lua semantics: // and % round toward minus infinity; a zero divisor gives 0 */
#ifndef BMT_HELPERS
#define BMT_HELPERS
static inline int32_t bmt_idiv(int32_t a, int32_t b)
{
    if (b == 0)
        return 0;
    int32_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0)))
        q--;
    return q;
}

static inline int32_t bmt_imod(int32_t a, int32_t b)
{
    if (b == 0)
        return 0;
    int32_t r = a % b;
    if (r != 0 && ((r < 0) != (b < 0)))
        r += b;
    return r;
}

static inline double bmt_fdiv(double a, double b) { return floor(a / b); }
static inline double bmt_fmod(double a, double b) { return a - floor(a / b) * b; }
static inline int32_t bmt_floor(double a) { return (int32_t)floor(a); }
static inline int32_t bmt_min_i32(int32_t a, int32_t b) { return a < b ? a : b; }
static inline int32_t bmt_max_i32(int32_t a, int32_t b) { return a > b ? a : b; }
static inline float bmt_min_f32(float a, float b) { return a < b ? a : b; }
static inline float bmt_max_f32(float a, float b) { return a > b ? a : b; }
static inline double bmt_min_f64(double a, double b) { return a < b ? a : b; }
static inline double bmt_max_f64(double a, double b) { return a > b ? a : b; }
#endif

"""


def translate(src):
    p = Parser(src)
    funcs = p.program()
    lua = src
    for start, end in sorted(p.cuts, reverse=True):
        lua = lua[:start] + lua[end:]
    c = ("/* made by scripts/bmt.py from a .bmt source: do not edit */\n#include <math.h>\n#include <stdbool.h>\n"
         "#include <stdint.h>\n#include <stdlib.h>\n\n" + HELPERS + "\n".join(funcs))
    return lua, c


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("source")
    ap.add_argument("--lua", help="write the plain Lua here")
    ap.add_argument("--c", help="write the C here")
    a = ap.parse_args(argv)
    try:
        lua, c = translate(open(a.source).read())
    except Error as e:
        print("%s: %s" % (a.source, e), file=sys.stderr)
        return 1
    if a.lua:
        open(a.lua, "w").write(lua)
    if a.c:
        open(a.c, "w").write(c)
    return 0


if __name__ == "__main__":
    sys.exit(main())
