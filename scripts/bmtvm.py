#!/usr/bin/env python3
"""bmtvm.py: the typed subset of Lua (see bmt.py) to the code of the bmvm register machine (src/bm/bmvm.h).

EXPERIMENT (claude/bm-vm-test). The source is checked by bmt.py first (types, names); this program trusts it and only
chooses the operations. One .bmt file gives one module: a blob that a cartridge can carry as data and the kernel runs.

  bmtvm.py yblocked.bmt --out yblocked.bmv [--dump]

The registers of a function: its parameters, then its constants (copied from the module each time the function is
entered), then its locals and temporaries. A condition (if, while) is code that jumps: no true/false value is made.

Limits: arrays of i32 and f64 only (no bool[] or f32[]); f32 is computed as f64; a function has at most 16 parameters and
255 registers; no recursion check (the machine stops at depth 32).
"""
import argparse
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bmt  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
_H = open(os.path.join(HERE, "..", "src", "bm", "bmvm.h"), encoding="utf-8").read()
OPS = re.findall(r"X\((\w+)\)", _H.split("#define BMVM_OPS(X)")[1].split("enum {")[0])
OP = {n: i for i, n in enumerate(OPS)}
PT = {"i32": 0, "bool": 0, "f32": 1, "f64": 1, "i32[]": 2, "f64[]": 3}
FLOAT = ("f32", "f64")

# which of a, b, c are registers (the same as op_use in bmvm.c)
REGS = {n: "abc" for n in OPS}
for _n in ("LK", ):
    REGS[_n] = "a"
for _n in ("MOV", "NEGI", "NEGF", "I2F", "NOT", "FLOOR", "SQRT", "ABSI", "ABSF", "FORI"):
    REGS[_n] = "ab"
for _n in ("JMP", "RET0"):
    REGS[_n] = ""
for _n in ("JF", "JT", "CALL", "RET"):
    REGS[_n] = "a"
for _n in OPS:
    if _n.startswith("JFL") or _n.startswith("JFE") or _n.startswith("JFN"):
        REGS[_n] = "ab"
JUMPS = {"JMP", "JF", "JT", "FORI"} | {n for n in OPS if n.startswith("JF") and len(n) == 5}
FUSED = {("<", "I"): "JFLTI", ("<=", "I"): "JFLEI", ("==", "I"): "JFEQI", ("~=", "I"): "JFNEI",
         ("<", "F"): "JFLTF", ("<=", "F"): "JFLEF", ("==", "F"): "JFEQF", ("~=", "F"): "JFNEF"}


class Error(Exception):
    pass


def is_float(t):
    return t in FLOAT


class Compiler:
    def __init__(self, src):
        bmt.translate(src)                       # the checks of bmt.py: a bad source stops here
        self.toks = bmt.Parser(src).toks
        self.i = 0
        self.consts = []                         # all the constants of the module, a pool for each function in a row
        self.fns = []                            # (name, params, ret, code, nregs, nconst, kfirst)
        self.funcsig = {}

    # -- tokens
    def peek(self):
        return self.toks[self.i][1] if self.i < len(self.toks) else ""

    def kind(self):
        return self.toks[self.i][0] if self.i < len(self.toks) else ""

    def take(self):
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
            raise Error("expected '%s', found '%s'" % (text, self.peek()))

    def name(self):
        if self.kind() != "name":
            raise Error("expected a name, found '%s'" % self.peek())
        return self.take()[1]

    # -- emitting
    def emit(self, op, a=0, b=0, c=0, k=0):
        self.code.append([OP[op], a, b, c, k])
        return len(self.code) - 1

    def kreg(self, v, is_f):
        """A constant of this function: a placeholder (negative) that becomes its register at the end."""
        key = (is_f, v)
        if key not in self.kdict:
            self.kdict[key] = len(self.kpool)
            self.kpool.append(struct.pack("<d", v) if is_f else struct.pack("<iI", v, 0))
            self.kvals.append(v)
        return -(1 + self.kdict[key])

    def tmp(self):
        r = self.top
        self.top += 1
        if self.top > 200:
            raise Error("a function needs too many registers")
        self.maxr = max(self.maxr, self.top)
        return r

    def local(self, name, typ):
        r = self.live
        self.live += 1
        self.top = max(self.top, self.live)
        self.maxr = max(self.maxr, self.top)
        self.vars[name] = (r, typ)
        return r

    def here(self):
        return len(self.code)

    def patch(self, at, to):
        self.code[at][4] = to

    def patch_all(self, lst, to):
        for j in lst:
            self.patch(j, to)

    # -- program
    def program(self):
        while self.i < len(self.toks):
            self.function()
        return self

    def function(self):
        self.expect("function")
        name = self.name()
        self.expect("(")
        self.code, self.vars, self.live, self.top, self.maxr = [], {}, 0, 0, 0
        self.kdict, self.kpool, self.kvals = {}, [], []
        params = []
        if self.peek() != ")":
            while True:
                pn = self.name()
                self.expect(":")
                pt = self.name()
                if self.accept("["):
                    self.expect("]")
                    pt += "[]"
                if pt not in PT:
                    raise Error("type '%s' is not in the machine" % pt)
                self.local(pn, pt)
                params.append((pn, pt))
                if not self.accept(","):
                    break
        self.expect(")")
        ret = None
        if self.accept(":"):
            ret = self.name()
        self.funcsig[name] = (len(self.fns), [t for _, t in params], ret)
        self.fns.append(None)
        self.cur = len(self.fns) - 1
        self.ret = ret
        self.loops = []
        self.block({"end"})
        self.expect("end")
        self.emit("RET0")
        self.finish(name, params, ret)

    def finish(self, name, params, ret):
        """Puts the constants after the parameters and moves the other registers up."""
        np_, nk = len(params), len(self.kpool)

        def remap(r):
            if r < 0:
                return np_ + (-r - 1)
            return r + nk if r >= np_ else r

        for ins in self.code:
            regs = REGS[OPS[ins[0]]]
            for pos, ch in ((1, "a"), (2, "b"), (3, "c")):
                if ch in regs:
                    ins[pos] = remap(ins[pos])
        nregs = max(self.maxr, np_) + nk
        if nregs > 250:
            raise Error("function %s needs too many registers" % name)
        kfirst = len(self.consts)
        self.consts.extend(self.kpool)
        self.fns[self.cur] = (name, params, ret, self.code, max(nregs, 1), nk, kfirst)

    def block(self, stops):
        saved_vars, saved_live = dict(self.vars), self.live
        while self.i < len(self.toks) and self.peek() not in stops:
            self.statement()
            self.top = self.live
        self.vars, self.live = saved_vars, saved_live
        self.top = self.live

    # -- statements
    def statement(self):
        t = self.peek()
        if t == "local":
            self.i += 1
            n = self.name()
            self.expect("=")
            e = self.expr()
            typ = self.guess(e)
            reg = self.local(n, typ)             # the value goes straight into the register of the local
            r, et = self.gen(e, reg)
            if is_float(typ) and not is_float(et):
                self.emit("I2F", reg, r)
            elif r != reg:
                self.emit("MOV", reg, r)
            return
        if t == "for":
            self.i += 1
            n = self.name()
            self.expect("=")
            a = self.expr()
            self.expect(",")
            b = self.expr()
            self.expect("do")
            saved_vars, saved_live = dict(self.vars), self.live
            ri = self.local(n, "i32")
            r, _ = self.gen(a, ri)
            if r != ri:
                self.emit("MOV", ri, r)
            re_ = self.local(n + "__end", "i32")
            r, _ = self.gen(b, re_)
            if r != re_:
                self.emit("MOV", re_, r)
            self.top = self.live
            jexit = self.emit("JFLEI", ri, re_)
            start = self.here()
            self.loops.append([])
            self.block({"end"})
            self.expect("end")
            self.emit("FORI", ri, re_, 0, start)
            brk = self.loops.pop()
            self.patch(jexit, self.here())
            self.patch_all(brk, self.here())
            self.vars, self.live = saved_vars, saved_live
            self.top = self.live
            return
        if t == "while":
            self.i += 1
            c = self.expr()
            self.expect("do")
            start = self.here()
            jf = self.cond_false(c)
            self.top = self.live
            self.loops.append([])
            self.block({"end"})
            self.expect("end")
            self.emit("JMP", k=start)
            brk = self.loops.pop()
            self.patch_all(jf, self.here())
            self.patch_all(brk, self.here())
            return
        if t == "if":
            self.i += 1
            ends = []
            while True:
                c = self.expr()
                self.expect("then")
                jf = self.cond_false(c)
                self.top = self.live
                self.block({"elseif", "else", "end"})
                if self.peek() in ("elseif", "else"):
                    ends.append(self.emit("JMP"))
                self.patch_all(jf, self.here())
                if self.accept("elseif"):
                    continue
                if self.accept("else"):
                    self.block({"end"})
                break
            self.expect("end")
            self.patch_all(ends, self.here())
            return
        if t == "return":
            self.i += 1
            if self.peek() in ("end", "else", "elseif") or self.i >= len(self.toks):
                self.emit("RET0")
                return
            e = self.expr()
            r, et = self.gen(e, None)
            if is_float(self.ret) and not is_float(et):
                nr = self.tmp()
                self.emit("I2F", nr, r)
                r = nr
            self.emit("RET", r)
            return
        if t == "break":
            self.i += 1
            self.loops[-1].append(self.emit("JMP"))
            return
        # assignment: name = e, or name[i] = e
        n = self.name()
        reg, vt = self.vars[n]
        if self.accept("["):
            ie = self.expr()
            self.expect("]")
            self.expect("=")
            ve = self.expr()
            ri, _ = self.gen(ie, None)
            rv, vtt = self.gen(ve, None)
            if vt == "f64[]" and not is_float(vtt):
                nr = self.tmp()
                self.emit("I2F", nr, self.fixk(rv))
                rv = nr
            self.emit("SETF" if vt == "f64[]" else "SETI", reg, ri, rv)
            return
        self.expect("=")
        e = self.expr()
        r, et = self.gen(e, reg)
        if is_float(vt) and not is_float(et):
            self.emit("I2F", reg, r)
        elif r != reg:
            self.emit("MOV", reg, r)

    def fixk(self, r):
        return r

    # -- conditions: code that jumps. cond_false(e): the places to patch for "jump when e is false"
    def cond_false(self, e):
        k = e[0]
        if k == "cmp":
            op = e[1]
            a, at = self.gen(e[2], None)
            b, bt = self.gen(e[3], None)
            fl = is_float(at) or is_float(bt)
            if fl:
                a, b = self.to_float(a, at), self.to_float(b, bt)
            if op in (">", ">="):
                a, b = b, a
                op = "<" if op == ">" else "<="
            return [self.emit(FUSED[(op, "F" if fl else "I")], a, b)]
        if k == "and":
            return self.cond_false(e[1]) + self.cond_false(e[2])
        if k == "or":
            jt = self.cond_true(e[1])
            jf = self.cond_false(e[2])
            self.patch_all(jt, self.here())
            return jf
        if k == "not":
            return self.cond_true(e[1])
        r, _ = self.gen(e, None)
        return [self.emit("JF", r)]

    def cond_true(self, e):
        k = e[0]
        if k == "cmp":
            op = e[1]
            a, at = self.gen(e[2], None)
            b, bt = self.gen(e[3], None)
            fl = is_float(at) or is_float(bt)
            if fl:
                a, b = self.to_float(a, at), self.to_float(b, bt)
                if op in (">", ">="):
                    a, b = b, a
                    op = "<" if op == ">" else "<="
                d = self.tmp()
                self.emit({"<": "LTF", "<=": "LEF", "==": "EQF", "~=": "NEF"}[op], d, a, b)
                return [self.emit("JT", d)]
            # integers: a < b is not (b <= a), and so on
            if op == "<":
                return [self.emit("JFLEI", b, a)]
            if op == "<=":
                return [self.emit("JFLTI", b, a)]
            if op == ">":
                return [self.emit("JFLEI", a, b)]
            if op == ">=":
                return [self.emit("JFLTI", a, b)]
            return [self.emit("JFNEI" if op == "==" else "JFEQI", a, b)]
        if k == "and":
            jf = self.cond_false(e[1])
            jt = self.cond_true(e[2])
            self.patch_all(jf, self.here())
            return jt
        if k == "or":
            return self.cond_true(e[1]) + self.cond_true(e[2])
        if k == "not":
            return self.cond_false(e[1])
        r, _ = self.gen(e, None)
        return [self.emit("JT", r)]

    # -- expressions: a tree first
    def expr(self):
        return self.or_()

    def or_(self):
        l = self.and_()
        while self.accept("or"):
            l = ("or", l, self.and_())
        return l

    def and_(self):
        l = self.cmp()
        while self.accept("and"):
            l = ("and", l, self.cmp())
        return l

    def cmp(self):
        l = self.band()
        if self.kind() == "op" and self.peek() in ("<", "<=", ">", ">=", "==", "~="):
            op = self.take()[1]
            return ("cmp", op, l, self.band())
        return l

    def band(self):
        l = self.add()
        while self.kind() == "op" and self.peek() == "&":
            self.i += 1
            l = ("bin", "&", l, self.add())
        return l

    def add(self):
        l = self.mul()
        while self.kind() == "op" and self.peek() in ("+", "-"):
            op = self.take()[1]
            l = ("bin", op, l, self.mul())
        return l

    def mul(self):
        l = self.unary()
        while self.kind() == "op" and self.peek() in ("*", "/", "//", "%"):
            op = self.take()[1]
            l = ("bin", op, l, self.unary())
        return l

    def unary(self):
        if self.accept("-"):
            return ("neg", self.unary())
        if self.accept("not"):
            return ("not", self.unary())
        return self.primary()

    def primary(self):
        k = self.kind()
        if k == "num":
            tx = self.take()[1]
            return ("num", float(tx), True) if "." in tx else ("num", int(tx), False)
        if self.accept("("):
            e = self.expr()
            self.expect(")")
            return e
        if self.accept("true"):
            return ("num", 1, False, "bool")
        if self.accept("false"):
            return ("num", 0, False, "bool")
        n = self.name()
        if self.peek() == "(" and self.kind() == "op":
            self.i += 1
            args = []
            if self.peek() != ")":
                while True:
                    args.append(self.expr())
                    if not self.accept(","):
                        break
            self.expect(")")
            return ("call", n, args)
        if self.accept("["):
            ie = self.expr()
            self.expect("]")
            return ("idx", n, ie)
        return ("var", n)

    # -- the type of a tree without emitting
    def guess(self, e):
        k = e[0]
        if k == "num":
            return "bool" if len(e) > 3 else ("f64" if e[2] else "i32")
        if k == "var":
            return self.vars[e[1]][1]
        if k == "idx":
            return self.vars[e[1]][1][:-2]
        if k in ("cmp", "and", "or", "not"):
            return "bool"
        if k == "neg":
            return self.guess(e[1])
        if k == "bin":
            lt, rt = self.guess(e[2]), self.guess(e[3])
            if e[1] == "/":
                return "f64"
            if e[1] == "&":
                return "i32"
            return "f64" if is_float(lt) or is_float(rt) else "i32"
        if k == "call":
            n = e[1]
            if n == "sqrt":
                return "f64"
            if n == "floor":
                return "i32"
            if n == "abs":
                return self.guess(e[2][0])
            if n in ("min", "max"):
                return "f64" if any(is_float(self.guess(a)) for a in e[2]) else "i32"
            return self.funcsig[n][2]
        raise Error("cannot type %r" % (e,))

    # -- code for a tree: gives (register, type); want: the register the result should be in (or None)
    def dst(self, want):
        return want if want is not None else self.tmp()

    def to_float(self, r, t):
        if is_float(t):
            return r
        if r < 0:                                # a constant: make it a float constant, no instruction
            return self.kreg(float(self.kvals[-r - 1]), True)
        nr = self.tmp()
        self.emit("I2F", nr, r)
        return nr

    def gen(self, e, want):
        k = e[0]
        if k == "num":
            is_f = e[2]
            kr = self.kreg(e[1], is_f)
            t = "bool" if len(e) > 3 else ("f64" if is_f else "i32")
            if want is not None:
                self.emit("MOV", want, kr)
                return want, t
            return kr, t
        if k == "var":
            r, t = self.vars[e[1]]
            if want is not None and want != r and not t.endswith("[]"):
                self.emit("MOV", want, r)
                return want, t
            return r, t
        if k == "idx":
            ar, at = self.vars[e[1]]
            ri, _ = self.gen(e[2], None)
            d = self.dst(want)
            self.emit("GETF" if at == "f64[]" else "GETI", d, ar, ri)
            return d, at[:-2]
        if k == "neg":
            r, t = self.gen(e[1], None)
            d = self.dst(want)
            self.emit("NEGF" if is_float(t) else "NEGI", d, r)
            return d, t
        if k == "cmp":
            op = e[1]
            a, at = self.gen(e[2], None)
            b, bt = self.gen(e[3], None)
            fl = is_float(at) or is_float(bt)
            if fl:
                a, b = self.to_float(a, at), self.to_float(b, bt)
            if op in (">", ">="):
                a, b = b, a
                op = "<" if op == ">" else "<="
            d = self.dst(want)
            self.emit({"<": "LT", "<=": "LE", "==": "EQ", "~=": "NE"}[op] + ("F" if fl else "I"), d, a, b)
            return d, "bool"
        if k in ("not", "and", "or"):
            # a true/false value: the code of the condition, then 1 or 0
            d = self.dst(want)
            jf = self.cond_false(e)
            self.emit("MOV", d, self.kreg(1, False))
            jend = self.emit("JMP")
            self.patch_all(jf, self.here())
            self.emit("MOV", d, self.kreg(0, False))
            self.patch(jend, self.here())
            return d, "bool"
        if k == "bin":
            op = e[1]
            a, at = self.gen(e[2], None)
            b, bt = self.gen(e[3], None)
            if op == "&":
                d = self.dst(want)
                self.emit("ANDI", d, a, b)
                return d, "i32"
            if op == "/":
                a, b = self.to_float(a, at), self.to_float(b, bt)
                d = self.dst(want)
                self.emit("DIVF", d, a, b)
                return d, "f64"
            fl = is_float(at) or is_float(bt)
            if fl:
                a, b = self.to_float(a, at), self.to_float(b, bt)
            name = {"+": "ADD", "-": "SUB", "*": "MUL"}.get(op)
            d = self.dst(want)
            if name:
                self.emit(name + ("F" if fl else "I"), d, a, b)
            elif op == "//":
                self.emit("FFDIV" if fl else "IDIV", d, a, b)
            else:
                self.emit("FFMOD" if fl else "IMOD", d, a, b)
            return d, "f64" if fl else "i32"
        if k == "call":
            n, args = e[1], e[2]
            if n in ("sqrt", "abs", "floor"):
                r, t = self.gen(args[0], None)
                d = self.dst(want)
                if n == "sqrt":
                    self.emit("SQRT", d, self.to_float(r, t))
                    return d, "f64"
                if n == "floor":
                    if is_float(t):
                        self.emit("FLOOR", d, r)
                    else:
                        self.emit("MOV", d, r)
                    return d, "i32"
                self.emit("ABSF" if is_float(t) else "ABSI", d, r)
                return d, t
            if n in ("min", "max"):
                a, at = self.gen(args[0], None)
                b, bt = self.gen(args[1], None)
                fl = is_float(at) or is_float(bt)
                if fl:
                    a, b = self.to_float(a, at), self.to_float(b, bt)
                d = self.dst(want)
                self.emit(n.upper() + ("F" if fl else "I"), d, a, b)
                return d, "f64" if fl else "i32"
            fi, ptypes, ret = self.funcsig[n]
            base = self.top
            for _ in args:
                self.tmp()
            for j, (a, pt) in enumerate(zip(args, ptypes)):
                r, t = self.gen(a, base + j)
                if pt in FLOAT and not is_float(t):
                    self.emit("I2F", base + j, r)
                elif r != base + j:
                    self.emit("MOV", base + j, r)
            self.emit("CALL", base, len(args), 0, fi)
            if want is not None and want != base:
                self.emit("MOV", want, base)
                return want, ret
            return base, ret
        raise Error("cannot compile %r" % (e,))

    # -- the module
    def blob(self):
        hdr = struct.pack("<IHHI", 0x31564D42, len(self.fns), len(self.consts), sum(len(f[3]) for f in self.fns))
        ftab, code, base = b"", b"", 0
        for name, params, ret, ins, nregs, nk, kfirst in self.fns:
            pt = bytes(PT[t] for _, t in params) + bytes(16 - len(params))
            rt = 4 if ret is None else PT[ret]
            ftab += struct.pack("<24sBBBB16sIII", name.encode(), len(params), nregs, rt, nk, pt, base, len(ins), kfirst)
            for op, a, b, c, kk in ins:
                if OPS[op] in JUMPS:
                    kk += base
                code += struct.pack("<BBBBi", op, a, b, c, kk)
            base += len(ins)
        return hdr + ftab + b"".join(self.consts) + code


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("source")
    ap.add_argument("--out", required=True)
    ap.add_argument("--dump", action="store_true", help="print the code")
    a = ap.parse_args(argv)
    try:
        c = Compiler(open(a.source, encoding="utf-8").read()).program()
    except (Error, bmt.Error) as e:
        print("%s: %s" % (a.source, e), file=sys.stderr)
        return 1
    open(a.out, "wb").write(c.blob())
    if a.dump:
        for name, params, ret, ins, nregs, nk, kfirst in c.fns:
            print("function %s (%d registers, %d constants, %d instructions)" % (name, nregs, nk, len(ins)))
            for n, (op, x, y, z, kk) in enumerate(ins):
                print("  %3d %-6s %3d %3d %3d  %d" % (n, OPS[op], x, y, z, kk))
    return 0


if __name__ == "__main__":
    sys.exit(main())
