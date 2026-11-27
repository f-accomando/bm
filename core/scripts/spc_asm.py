#!/usr/bin/env python3
"""A small assembler for the SPC700, the sound CPU of the SNES, for the sound driver of the Shell SDK. Original code.

    python core/scripts/spc_asm.py core/src/kernel/shell_sdk_spc.asm core/src/kernel/shell_sdk_spc_bin.h
    python core/scripts/spc_asm.py --check SOURCE HEADER      # fails when the header is not up to date

It reads the instructions that the driver uses: labels, name = value, .org, .db, and the operands
#value, dp, dp+x, !abs, !abs+x, !abs+y, [dp]+y, a, x, y. A bare value is in the direct page ($00-$FF); an absolute
address starts with !. Numbers: $hex, %binary or decimal; + and - between them; < and > the low and high byte.
"""
import re
import sys

ALU = {"or": 0x00, "and": 0x20, "eor": 0x40, "cmp": 0x60, "adc": 0x80, "sbc": 0xA0}
IMPLIED = {"ret": 0x6F, "clrc": 0x60, "setc": 0x80, "clrp": 0x20, "di": 0xC0, "nop": 0x00}
BRANCH = {"bra": 0x2F, "beq": 0xF0, "bne": 0xD0, "bcs": 0xB0, "bcc": 0x90, "bmi": 0x30, "bpl": 0x10}
STACK = {("push", "a"): 0x2D, ("push", "x"): 0x4D, ("push", "y"): 0x6D,
         ("pop", "a"): 0xAE, ("pop", "x"): 0xCE, ("pop", "y"): 0xEE}
UNARY = {  # op: (a, dp, dp+x, abs)
    "inc": (0xBC, 0xAB, 0xBB, 0xAC), "dec": (0x9C, 0x8B, 0x9B, 0x8C),
    "asl": (0x1C, 0x0B, 0x1B, 0x0C), "lsr": (0x5C, 0x4B, 0x5B, 0x4C),
    "rol": (0x3C, 0x2B, 0x3B, 0x2C), "ror": (0x7C, 0x6B, 0x7B, 0x6C)}
WORD = {"addw": 0x7A, "subw": 0x9A, "cmpw": 0x5A, "incw": 0x3A, "decw": 0x1A}


class Fail(Exception):
    pass


def value(text, symbols, final):
    text = text.strip()
    if not text:
        raise Fail("a value is missing")
    if text[0] in "<>":
        v = value(text[1:], symbols, final)
        return v & 0xFF if text[0] == "<" else (v >> 8) & 0xFF
    total, sign = 0, 1
    for part in re.split(r"([+-])", text):
        part = part.strip()
        if part == "+":
            sign = 1
        elif part == "-":
            sign = -1
        elif part:
            if part.startswith("$"):
                v = int(part[1:], 16)
            elif part.startswith("%"):
                v = int(part[1:], 2)
            elif part[0].isdigit():
                v = int(part)
            elif part in symbols:
                v = symbols[part]
            elif final:
                raise Fail("unknown name '%s'" % part)
            else:
                v = 0x100                       # not yet known: an address
            total += sign * v
    return total


def operand(text, symbols, final):
    """(mode, value) of one operand."""
    t = text.strip()
    low = t.lower()
    if low in ("a", "x", "y", "ya"):
        return low, None
    if t.startswith("#"):
        return "imm", value(t[1:], symbols, final)
    m = re.match(r"^\[(.*)\]\s*\+\s*[yY]$", t)
    if m:
        return "indy", value(m.group(1), symbols, final)
    index = ""
    m = re.match(r"^(.*)\+\s*([xXyY])$", t)
    if m:
        t, index = m.group(1), m.group(2).lower()
    if t.startswith("!"):
        return "abs" + index, value(t[1:], symbols, final)
    v = value(t, symbols, final)
    if final and not 0 <= v <= 0xFF:
        raise Fail("'%s' is not in the direct page: write !%s" % (t, t))
    if index == "y":
        raise Fail("dp+y is not used: write !address+y")
    return "dp" + index, v


def byte(v):
    return [v & 0xFF]


def word(v):
    return [v & 0xFF, (v >> 8) & 0xFF]


def encode(op, args, symbols, pc, final):
    if op in IMPLIED and not args:
        return [IMPLIED[op]]
    if op in BRANCH:
        target = value(args[0], symbols, final)
        d = target - (pc + 2)
        if final and not -128 <= d <= 127:
            raise Fail("the branch is too far (%d)" % d)
        return [BRANCH[op], d & 0xFF]
    if op == "dbnz":
        target = value(args[1], symbols, final)
        if args[0].strip().lower() == "y":
            d = target - (pc + 2)
            first = [0xFE]
        else:
            d = target - (pc + 3)
            first = [0x6E, value(args[0], symbols, final) & 0xFF]
        if final and not -128 <= d <= 127:
            raise Fail("the branch is too far (%d)" % d)
        return first + [d & 0xFF]
    if op in ("jmp", "call"):
        t = args[0].strip()
        m = re.match(r"^\[!(.*)\+\s*[xX]\]$", t)
        if m and op == "jmp":
            return [0x1F] + word(value(m.group(1), symbols, final))
        return [0x5F if op == "jmp" else 0x3F] + word(value(t.lstrip("!"), symbols, final))
    if (op, args[0].strip().lower() if args else "") in STACK:
        return [STACK[(op, args[0].strip().lower())]]
    if op == "mov" and [a.strip().lower() for a in args] == ["sp", "x"]:
        return [0xBD]
    modes = [operand(a, symbols, final) for a in args]
    if op == "mul":
        return [0xCF]
    if op == "xcn":
        return [0x9F]
    if op in UNARY:
        mode, v = modes[0]
        a, dp, dpx, ab = UNARY[op]
        if mode == "a":
            return [a]
        if mode in ("x", "y"):
            if op == "inc":
                return [0x3D if mode == "x" else 0xFC]
            if op == "dec":
                return [0x1D if mode == "x" else 0xDC]
        if mode == "dp":
            return [dp] + byte(v)
        if mode == "dpx":
            return [dpx] + byte(v)
        if mode == "abs":
            return [ab] + word(v)
    if op in WORD:
        mode, v = modes[-1]
        return [WORD[op]] + byte(v)
    (m1, v1), (m2, v2) = modes
    if op == "movw":
        if m1 == "ya":
            return [0xBA] + byte(v2)
        return [0xDA] + byte(v1)
    if op == "cmp" and m1 in ("x", "y"):
        table = {"x": {"imm": 0xC8, "dp": 0x3E, "abs": 0x1E}, "y": {"imm": 0xAD, "dp": 0x7E, "abs": 0x5E}}
        code = table[m1][m2]
        return [code] + (word(v2) if m2 == "abs" else byte(v2))
    if op in ALU:
        base = ALU[op]
        if m1 == "a":
            table = {"imm": 8, "dp": 4, "abs": 5, "dpx": 0x14, "absx": 0x15, "absy": 0x16, "indy": 0x17}
            return [base + table[m2]] + (word(v2) if m2.startswith("abs") else byte(v2))
        if m1 == "dp" and m2 == "dp":
            return [base + 9] + byte(v2) + byte(v1)
        if m1 == "dp" and m2 == "imm":
            return [base + 0x18] + byte(v2) + byte(v1)
    if op == "mov":
        pairs = {("a", "x"): 0x7D, ("a", "y"): 0xDD, ("x", "a"): 0x5D, ("y", "a"): 0xFD}
        if (m1, m2) in pairs:
            return [pairs[(m1, m2)]]
        loads = {"a": {"imm": 0xE8, "dp": 0xE4, "dpx": 0xF4, "abs": 0xE5, "absx": 0xF5, "absy": 0xF6, "indy": 0xF7},
                 "x": {"imm": 0xCD, "dp": 0xF8, "abs": 0xE9},
                 "y": {"imm": 0x8D, "dp": 0xEB, "dpx": 0xFB, "abs": 0xEC}}
        if m1 in loads and m2 in loads[m1]:
            return [loads[m1][m2]] + (word(v2) if m2.startswith("abs") else byte(v2))
        stores = {"a": {"dp": 0xC4, "dpx": 0xD4, "abs": 0xC5, "absx": 0xD5, "absy": 0xD6, "indy": 0xD7},
                  "x": {"dp": 0xD8, "abs": 0xC9}, "y": {"dp": 0xCB, "dpx": 0xDB, "abs": 0xCC}}
        if m2 in stores and m1 in stores[m2]:
            return [stores[m2][m1]] + (word(v1) if m1.startswith("abs") else byte(v1))
        if m1 == "dp" and m2 == "imm":
            return [0x8F] + byte(v2) + byte(v1)
        if m1 == "dp" and m2 == "dp":
            return [0xFA] + byte(v2) + byte(v1)
    raise Fail("no such instruction: %s %s" % (op, ", ".join(args)))


def split_args(text):
    out, depth, start = [], 0, 0
    for i, c in enumerate(text):
        depth += c == "["
        depth -= c == "]"
        if c == "," and depth == 0:
            out.append(text[start:i])
            start = i + 1
    if text.strip():
        out.append(text[start:])
    return [a.strip() for a in out]


def assemble(source):
    symbols = {}
    for final in (False, False, True):
        pc, origin, data, scope = 0, None, bytearray(), ""
        for n, line in enumerate(source.split("\n"), 1):
            line = line.split(";")[0].rstrip()
            try:
                m = re.match(r"^\s*([@A-Za-z_][\w]*):(.*)$", line)
                if m:
                    name = m.group(1)
                    if name.startswith("@"):
                        name = scope + name
                    else:
                        scope = name
                    symbols[name] = pc
                    line = m.group(2)
                line = line.strip()
                if not line:
                    continue
                m = re.match(r"^([A-Za-z_]\w*)\s*=\s*(.+)$", line)
                if m:
                    symbols[m.group(1)] = value(m.group(2), symbols, final)
                    continue
                line = re.sub(r"@\w+", lambda g: scope + g.group(0), line)
                parts = line.split(None, 1)
                op = parts[0].lower()
                args = split_args(parts[1]) if len(parts) > 1 else []
                if op == ".org":
                    pc = value(args[0], symbols, final)
                    if origin is None:
                        origin = pc
                    elif pc < origin + len(data):
                        raise Fail(".org goes back")
                    else:
                        data.extend(bytes(pc - origin - len(data)))
                    continue
                if op == ".db":
                    code = [value(a, symbols, final) & 0xFF for a in args]
                else:
                    code = encode(op, args, symbols, pc, final)
                data.extend(code)
                pc += len(code)
            except Fail as e:
                raise SystemExit("line %d: %s: %s" % (n, e, line))
            except (KeyError, ValueError, IndexError) as e:
                raise SystemExit("line %d: cannot assemble (%r): %s" % (n, e, line))
    return origin, bytes(data), symbols


def header(origin, data):
    rows = [",".join("0x%02X" % b for b in data[i:i + 16]) for i in range(0, len(data), 16)]
    return ("/* The sound driver of the SNES, assembled from shell_sdk_spc.asm by core/scripts/spc_asm.py.\n"
            " * Do not edit: change the source and run the script. */\n"
            "#define SDK_SPC_ORG 0x%04X\n"
            "static const uint8_t sdk_spc_driver[%d] = {\n    %s };\n" % (origin, len(data), ",\n    ".join(rows)))


def main(argv):
    check = argv[:1] == ["--check"]
    if check:
        argv = argv[1:]
    if len(argv) != 2:
        raise SystemExit(__doc__)
    origin, data, _ = assemble(open(argv[0], encoding="utf-8").read().replace("\r\n", "\n"))
    text = header(origin, data)
    if check:
        have = open(argv[1], encoding="utf-8").read().replace("\r\n", "\n")
        if have != text:
            raise SystemExit("%s is not up to date: run core/scripts/spc_asm.py" % argv[1])
        print("spc driver: up to date, %d bytes" % len(data))
        return
    open(argv[1], "w", encoding="utf-8", newline="\n").write(text)
    print("%s: %d bytes from $%04X" % (argv[1], len(data), origin))


if __name__ == "__main__":
    main(sys.argv[1:])
