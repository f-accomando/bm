"""Counts the tokens of PICO-8 code as PICO-8 does: a name, a number, a
string, an operator is one token each; a pair of brackets one; not counted:
comments, commas, dots, colons, semicolons, closing brackets, `local`,
`end`, and the minus of a negative number.

  python3 tokens.py FILE.lua
"""
import re
import sys

# longest first
OPS = ["..=", ">>>=", "<<>=", ">><=", ">>>", "<<>", ">><", "^^=", "<<=", ">>=", "...", "..", "==", "~=", "!=",
       "<=", ">=", "+=", "-=", "*=", "/=", "\\=", "%=", "^=", "|=", "&=", "<<", ">>", "^^", "::"] + \
      list("+-*/\\%^#&|~<>=(){}[];:,.@$?")
FREE_OPS = {",", ".", ";", ":", ")", "]", "}"}
FREE_WORDS = {"local", "end"}


def lex(src):
    i, n, out = 0, len(src), []
    while i < n:
        c = src[i]
        if c in " \t\r\n":
            i += 1
            continue
        if src.startswith("--", i) or src.startswith("//", i):
            m = re.match(r"--\[(=*)\[", src[i:])
            if m:
                i = src.find("]" + m.group(1) + "]", i) + len(m.group(1)) + 2
            else:
                j = src.find("\n", i)
                i = n if j < 0 else j
            continue
        m = re.match(r"\[(=*)\[", src[i:])
        if m:
            i = src.find("]" + m.group(1) + "]", i) + len(m.group(1)) + 2
            out.append(("str", ""))
            continue
        if c in "\"'":
            j = i + 1
            while src[j] != c:
                j += 2 if src[j] == "\\" else 1
            out.append(("str", ""))
            i = j + 1
            continue
        m = re.match(r"0[xX][0-9a-fA-F]*\.?[0-9a-fA-F]*|0[bB][01]*\.?[01]*|\d+\.?\d*(?:[eE][-+]?\d+)?|\.\d+",
                     src[i:])
        if m:
            out.append(("num", m.group()))
            i += len(m.group())
            continue
        m = re.match(r"[A-Za-z_][A-Za-z_0-9]*", src[i:])
        if m:
            out.append(("name", m.group()))
            i += len(m.group())
            continue
        if ord(c) > 127:                 # a glyph (the buttons, the patterns): a name
            j = i
            while j < n and ord(src[j]) > 127:
                j += 1
            out.append(("name", src[i:j]))
            i = j
            continue
        for o in OPS:
            if src.startswith(o, i):
                out.append(("op", o))
                i += len(o)
                break
        else:
            raise SystemExit("bad char %r at %d" % (c, i))
    return out


def count(src):
    t = lex(src)
    k = 0
    for i, (kind, v) in enumerate(t):
        if kind == "op" and v in FREE_OPS:
            continue
        if kind == "name" and v in FREE_WORDS:
            continue
        if kind == "op" and v == "-" and i + 1 < len(t) and t[i + 1][0] == "num":
            prev = t[i - 1] if i else ("op", "(")
            if prev[0] == "op" and prev[1] not in (")", "]", "}") or prev[1] in ("return", "and", "or", "not",
                                                                                  "then", "do", "else"):
                continue
        k += 1
    return k


if __name__ == "__main__":
    src = open(sys.argv[1], encoding="utf-8").read()
    print(count(src), "tokens,", len(src), "chars")
