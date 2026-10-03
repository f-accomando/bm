#!/usr/bin/env python3
"""A small assembler for the QPUs of the VideoCore IV (M33): the subset the
fragment shaders of bm need, in the syntax of vc4asm.

    tools/qpuasm.py --test              check against shaders run on a Pi
    tools/qpuasm.py -o src/gpu/shaders.h   write the shaders of bm as C arrays

One instruction per line: "add-op ; mul-op [; signal]". The add ALU does
fadd fsub fmin fmax ftoi itof add sub shr asr ror shl min max and or xor
not v8adds v8subs; the mul ALU fmul mul24 v8muld v8min v8max (and v8adds,
v8subs when the add ALU is busy). "mov d, s" goes to the ALU of its slot.
Sources: r0-r5, ra0-ra31, rb0-rb31, vary, unif, x_coord (regfile A 41: the
pixel's x), y_coord (regfile B 41: its y), small immediates (0..15,
-16..-1, 1.0 2.0 ... 128.0, 1/256 ... 0.5). Destinations: r0-r3, r5,
ra0-ra31, rb0-rb31, tlbc (colour), tlb_z, t0s t0t t0r t0b, t1s ..., nop;
a mul destination can pack a float into one byte of a colour: r3.8a ..
r3.8d, or into all four with .8888. Signals: thrend sbwait sbdone ldtmu0
ldtmu1 loadc; "small" is added by itself with an immediate.
Reference: VideoCore IV 3D Architecture Reference Guide (Broadcom), chapter 3.
"""
import argparse
import re
import sys

ADD_OPS = {"nop": 0, "fadd": 1, "fsub": 2, "fmin": 3, "fmax": 4, "fminabs": 5, "fmaxabs": 6,
           "ftoi": 7, "itof": 8, "add": 12, "sub": 13, "shr": 14, "asr": 15, "ror": 16, "shl": 17,
           "min": 18, "max": 19, "and": 20, "or": 21, "xor": 22, "not": 23, "clz": 24,
           "v8adds": 30, "v8subs": 31}
MUL_OPS = {"nop": 0, "fmul": 1, "mul24": 2, "v8muld": 3, "v8min": 4, "v8max": 5, "v8adds": 6,
           "v8subs": 7}
SIGNALS = {"bkpt": 0, "nosig": 1, "thrsw": 2, "thrend": 3, "sbwait": 4, "sbdone": 5, "lthrsw": 6,
           "loadcv": 7, "loadc": 8, "ldcend": 9, "ldtmu0": 10, "ldtmu1": 11, "loadam": 12,
           "small": 13}
WADDR = {"r0": 32, "r1": 33, "r2": 34, "r3": 35, "tmu_noswap": 36, "r5": 37, "host_int": 38,
         "nop": 39, "unif_addr": 40, "tlb_stencil": 43, "tlb_z": 44, "tlbc_ms": 45, "tlbc": 46,
         "tlb_alpha": 47, "vpm": 48, "mutex_release": 51, "sfu_recip": 52, "sfu_recipsqrt": 53,
         "sfu_exp": 54, "sfu_log": 55, "t0s": 56, "t0t": 57, "t0r": 58, "t0b": 59, "t1s": 60,
         "t1t": 61, "t1r": 62, "t1b": 63}
RADDR = {"unif": 32, "vary": 35, "elem_num": 38, "nop": 39}     # readable from file A or B
ACC = {"r0": 0, "r1": 1, "r2": 2, "r3": 3, "r4": 4, "r5": 5}
COND = {"never": 0, "always": 1, "zs": 2, "zc": 3, "ns": 4, "nc": 5, "cs": 6, "cc": 7,
        "ifz": 2, "ifnz": 3, "ifn": 4, "ifnn": 5, "ifc": 6, "ifnc": 7}
PACK_MUL = {"8888": 3, "8a": 4, "8b": 5, "8c": 6, "8d": 7}
MUX_A, MUX_B = 6, 7


def small_imm(tok):
    """the 6-bit small immediate of a literal, or None"""
    try:
        if re.fullmatch(r"-?\d+", tok):
            v = int(tok)
            if 0 <= v <= 15:
                return v
            if -16 <= v <= -1:
                return 32 + v
            return None
        f = float(tok)
    except ValueError:
        return None
    for e in range(8):
        if f == 2.0 ** e:
            return 32 + e
    for e in range(8):
        if f == 2.0 ** (e - 8):
            return 40 + e
    return None


class Inst:
    def __init__(self):
        self.sig = SIGNALS["nosig"]
        self.raddr_a = None
        self.raddr_b = None
        self.small = None


def assemble_line(line, lineno):
    parts = [p.strip() for p in line.split(";")]
    if len(parts) < 2 or len(parts) > 3:
        raise SyntaxError(f"line {lineno}: need 'add ; mul [; signal]': {line}")
    ins = Inst()
    fields = {"op_add": 0, "op_mul": 0, "waddr_add": 39, "waddr_mul": 39, "cond_add": 0,
              "cond_mul": 0, "add_a": 0, "add_b": 0, "mul_a": 0, "mul_b": 0, "pm": 0, "pack": 0,
              "ws": 0, "sf": 0}
    if len(parts) == 3 and parts[2] and parts[2] != "nop":
        if parts[2] not in SIGNALS:
            raise SyntaxError(f"line {lineno}: unknown signal {parts[2]}")
        ins.sig = SIGNALS[parts[2]]

    def source(tok):
        """input mux for a source, allocating raddr_a / raddr_b"""
        tok = tok.strip()
        if tok in ACC:
            return ACC[tok]
        m = re.fullmatch(r"r([ab])(\d+)", tok)
        if m:
            n = int(m[2])
            if m[1] == "a":
                if ins.raddr_a not in (None, n):
                    raise SyntaxError(f"line {lineno}: two regfile A reads")
                ins.raddr_a = n
                return MUX_A
            if ins.raddr_b not in (None, n) or ins.small is not None:
                raise SyntaxError(f"line {lineno}: two regfile B reads")
            ins.raddr_b = n
            return MUX_B
        if tok == "x_coord":                # X_PIXEL_COORD: regfile A only
            if ins.raddr_a not in (None, 41):
                raise SyntaxError(f"line {lineno}: two regfile A reads")
            ins.raddr_a = 41
            return MUX_A
        if tok == "y_coord":                # Y_PIXEL_COORD: regfile B only
            if ins.raddr_b not in (None, 41) or ins.small is not None:
                raise SyntaxError(f"line {lineno}: two regfile B reads")
            ins.raddr_b = 41
            return MUX_B
        if tok in RADDR:
            n = RADDR[tok]
            if ins.raddr_a in (None, n):
                ins.raddr_a = n
                return MUX_A
            if ins.raddr_b in (None, n) and ins.small is None:
                ins.raddr_b = n
                return MUX_B
            raise SyntaxError(f"line {lineno}: no read port left for {tok}")
        imm = small_imm(tok)
        if imm is not None:
            if ins.raddr_b is not None or (ins.small is not None and ins.small != imm):
                raise SyntaxError(f"line {lineno}: small immediate needs regfile B")
            ins.small = imm
            return MUX_B
        raise SyntaxError(f"line {lineno}: unknown source {tok}")

    def dest(tok, alu):
        """write address, and the file it needs (A, B or None)"""
        tok = tok.strip()
        pack = None
        if "." in tok:
            tok, pack = tok.split(".", 1)
            if alu != "mul" or pack not in PACK_MUL:
                raise SyntaxError(f"line {lineno}: pack .{pack} only on the mul ALU")
            fields["pm"] = 1
            fields["pack"] = PACK_MUL[pack]
        m = re.fullmatch(r"r([ab])(\d+)", tok)
        if m:
            return int(m[2]), m[1].upper()
        if tok in WADDR:
            return WADDR[tok], None
        raise SyntaxError(f"line {lineno}: unknown destination {tok}")

    files = {}
    for slot, text in enumerate(parts[:2]):
        text = text.strip()
        if text == "nop":
            continue
        m = re.fullmatch(r"(\w+)((?:\.\w+)*)\s+(.*)", text)
        if not m:
            raise SyntaxError(f"line {lineno}: {text}")
        op, args, cond = m[1], [a.strip() for a in m[3].split(",")], "always"
        for suffix in m[2].split(".")[1:]:
            if suffix == "setf":                # flags from this result
                fields["sf"] = 1
            elif suffix in COND:
                cond = suffix
            else:
                raise SyntaxError(f"line {lineno}: unknown suffix .{suffix}")
        if op == "mov":
            alu = "add" if slot == 0 else "mul"
            op = "or" if alu == "add" else "v8min"
            args = [args[0], args[1], args[1]]
        elif op in ADD_OPS and op in MUL_OPS:      # v8adds, v8subs: the mul ALU if free
            alu = "mul" if slot == 1 or parts[1].strip() == "nop" else "add"
        elif op in ADD_OPS:
            alu = "add"
        elif op in MUL_OPS:
            alu = "mul"
        else:
            raise SyntaxError(f"line {lineno}: unknown op {op}")
        if alu == "add" and fields["cond_add"]:
            raise SyntaxError(f"line {lineno}: two add ALU ops")
        if alu == "mul" and fields["cond_mul"]:
            raise SyntaxError(f"line {lineno}: two mul ALU ops")
        if op in ("not", "clz", "ftoi", "itof") and len(args) == 2:
            args = [args[0], args[1], args[1]]
        if len(args) != 3:
            raise SyntaxError(f"line {lineno}: {op} needs dst, a, b")
        waddr, wfile = dest(args[0], alu)
        a, b = source(args[1]), source(args[2])
        if alu == "add":
            fields.update(op_add=ADD_OPS[op], waddr_add=waddr, cond_add=COND[cond], add_a=a, add_b=b)
        else:
            fields.update(op_mul=MUL_OPS[op], waddr_mul=waddr, cond_mul=COND[cond], mul_a=a, mul_b=b)
        files[alu] = wfile
    # write swap: the add ALU writes file A and the mul ALU file B, or swapped
    if files.get("add") == "B" or files.get("mul") == "A":
        if files.get("add") == "A" or files.get("mul") == "B":
            raise SyntaxError(f"line {lineno}: both ALUs write the same register file")
        fields["ws"] = 1
    if ins.small is not None:
        if ins.sig != SIGNALS["nosig"]:
            raise SyntaxError(f"line {lineno}: a small immediate takes the signal field")
        ins.sig = SIGNALS["small"]
        raddr_b = ins.small
    else:
        raddr_b = 39 if ins.raddr_b is None else ins.raddr_b
    raddr_a = 39 if ins.raddr_a is None else ins.raddr_a
    hi = (ins.sig << 28 | 0 << 25 | fields["pm"] << 24 | fields["pack"] << 20 |
          fields["cond_add"] << 17 | fields["cond_mul"] << 14 | fields["sf"] << 13 |
          fields["ws"] << 12 | fields["waddr_add"] << 6 | fields["waddr_mul"])
    lo = (fields["op_mul"] << 29 | fields["op_add"] << 24 | raddr_a << 18 | raddr_b << 12 |
          fields["add_a"] << 9 | fields["add_b"] << 6 | fields["mul_a"] << 3 | fields["mul_b"])
    return lo, hi


def assemble(src):
    out = []
    for n, line in enumerate(src.splitlines(), 1):
        line = line.split("#")[0].strip()
        if line:
            out.append((assemble_line(line, n), line))
    return out


# ---------------------------------------------------------------- shaders of bm
# Fragment shaders for the NV shader state (vertices shaded on the ARM).
# A varying is interpolated as VP * W + C: VP comes from "vary", C is in r5
# after the read, W (perspective) in ra15; rb15 holds Z.

SHADERS = {
    # colour per vertex (r, g, b: 3 varyings), with depth; it starts with
    # two nops as the shaders run on a Pi Zero W by kumaashi
    "fs_colour": """
        nop                 ; nop
        nop                 ; nop
        mov r3, ra15        ; nop                       # W
        mov r0, vary        ; nop
        fmul r0, r0, r3     ; nop
        fadd r0, r0, r5     ; mov r1, vary  ; sbwait
        fmul r1, r1, r3     ; nop
        fadd r1, r1, r5     ; mov r2, vary
        fmul r2, r2, r3     ; nop
        fadd r2, r2, r5     ; mov r3.8a, r0
        nop                 ; mov r3.8b, r1
        nop                 ; mov r3.8c, r2
        nop                 ; mov r3.8d, 1.0
        mov tlb_z, rb15     ; nop
        mov tlbc, r3        ; nop           ; thrend
        nop                 ; nop
        nop                 ; nop           ; sbdone
    """,
    # texture 0 at (s, t) (2 varyings, the texture parameters P0 P1 as
    # uniforms, read by the TMU when s is written), with depth
    "fs_texture": """
        nop                 ; nop
        nop                 ; nop
        mov r3, ra15        ; nop                       # W
        mov r0, vary        ; nop                       # s
        fmul r0, r0, r3     ; nop
        fadd r0, r0, r5     ; nop
        mov r1, vary        ; nop                       # t
        fmul r1, r1, r3     ; nop
        fadd r1, r1, r5     ; nop
        mov t0t, r1         ; nop
        mov t0s, r0         ; nop                       # starts the lookup
        nop                 ; nop           ; sbwait
        mov tlb_z, rb15     ; nop
        nop                 ; nop           ; ldtmu0   # r4 = texel
        mov tlbc, r4        ; nop           ; thrend
        nop                 ; nop
        nop                 ; nop           ; sbdone
    """,
    # the faces of draw3d: texture 0 at (s, t) times the light k (3
    # varyings), opaque texels
    "fs_tex_lit": """
        nop                 ; nop
        nop                 ; nop
        mov r3, ra15        ; nop                       # W
        mov r0, vary        ; nop                       # s
        fmul r0, r0, r3     ; nop
        fadd r0, r0, r5     ; nop
        mov r1, vary        ; nop                       # t
        fmul r1, r1, r3     ; nop
        fadd r1, r1, r5     ; nop
        mov t0t, r1         ; nop
        mov t0s, r0         ; nop                       # starts the lookup
        mov r2, vary        ; nop                       # k
        fmul r2, r2, r3     ; nop
        fadd r2, r2, r5     ; nop
        nop                 ; mov r1.8888, r2           # k in the four bytes
        nop                 ; nop           ; sbwait
        mov tlb_z, rb15     ; nop
        nop                 ; nop           ; ldtmu0   # r4 = texel
        v8muld r0, r4, r1   ; nop                       # texel * k
        mov tlbc, r0        ; nop           ; thrend
        nop                 ; nop
        nop                 ; nop           ; sbdone
    """,
    # the same where the texture has transparent texels: alpha 0 (byte d)
    # writes neither colour nor depth (the binning list turns early z off)
    "fs_tex_lit_alpha": """
        nop                 ; nop
        nop                 ; nop
        mov r3, ra15        ; nop                       # W
        mov r0, vary        ; nop                       # s
        fmul r0, r0, r3     ; nop
        fadd r0, r0, r5     ; nop
        mov r1, vary        ; nop                       # t
        fmul r1, r1, r3     ; nop
        fadd r1, r1, r5     ; nop
        mov t0t, r1         ; nop
        mov t0s, r0         ; nop                       # starts the lookup
        mov r2, vary        ; nop                       # k
        fmul r2, r2, r3     ; nop
        fadd r2, r2, r5     ; nop
        nop                 ; mov r1.8888, r2           # k in the four bytes
        nop                 ; nop           ; sbwait
        nop                 ; nop           ; ldtmu0   # r4 = texel
        shr r2, r4, 15      ; v8muld r0, r4, r1         # texel * k
        shr.setf nop, r2, 9 ; nop                       # Z: alpha is 0
        mov.ifnz tlb_z, rb15 ; nop
        mov.ifnz tlbc, r0   ; nop           ; thrend
        nop                 ; nop
        nop                 ; nop           ; sbdone
    """,
    # Overbit (M34): screen-door transparency, colour per vertex drawn on
    # the pixels with x + y even only (the others keep what is behind, as
    # the software's R3D_SCREEN); the binning list turns early z off
    "fs_colour_screen": """
        nop                 ; nop
        nop                 ; nop
        mov r3, ra15        ; nop                       # W
        mov r0, vary        ; nop
        fmul r0, r0, r3     ; nop
        fadd r0, r0, r5     ; mov r1, vary  ; sbwait
        fmul r1, r1, r3     ; nop
        fadd r1, r1, r5     ; mov r2, vary
        fmul r2, r2, r3     ; nop
        fadd r2, r2, r5     ; mov r3.8a, r0
        nop                 ; mov r3.8b, r1
        nop                 ; mov r3.8c, r2
        nop                 ; mov r3.8d, 1.0
        add r0, x_coord, y_coord ; nop
        and.setf nop, r0, 1 ; nop                       # Z: x + y even
        mov.ifz tlb_z, rb15 ; nop
        mov.ifz tlbc, r3    ; nop           ; thrend
        nop                 ; nop
        nop                 ; nop           ; sbdone
    """,
    # Overbit's map (M34): texture 0 at (s, t) times the light baked at the
    # corners (3 varyings, half of it: up to 2x) plus the fog (3 varyings):
    # texel * light * 2 + fog, each byte saturated
    "fs_tex_rgb": """
        nop                 ; nop
        nop                 ; nop
        mov r0, vary        ; nop                       # s
        fmul r0, r0, ra15   ; nop
        fadd r0, r0, r5     ; nop
        mov r1, vary        ; nop                       # t
        fmul r1, r1, ra15   ; nop
        fadd r1, r1, r5     ; nop
        mov t0t, r1         ; nop
        mov t0s, r0         ; nop                       # starts the lookup
        mov r0, vary        ; nop                       # light, byte a
        fmul r0, r0, ra15   ; nop
        fadd r0, r0, r5     ; nop
        mov r1, vary        ; mov r2.8a, r0             # light, byte b
        fmul r1, r1, ra15   ; nop
        fadd r1, r1, r5     ; nop
        mov r0, vary        ; mov r2.8b, r1             # light, byte c
        fmul r0, r0, ra15   ; nop
        fadd r0, r0, r5     ; nop
        mov r1, vary        ; mov r2.8c, r0             # fog, byte a
        fmul r1, r1, ra15   ; nop
        fadd r1, r1, r5     ; nop
        mov r0, vary        ; mov r3.8a, r1             # fog, byte b
        fmul r0, r0, ra15   ; nop
        fadd r0, r0, r5     ; nop
        mov r1, vary        ; mov r3.8b, r0             # fog, byte c
        fmul r1, r1, ra15   ; nop
        fadd r1, r1, r5     ; nop
        nop                 ; mov r3.8c, r1
        nop                 ; mov r2.8d, 1.0            # the texel's alpha stays
        nop                 ; mov r3.8d, 0
        nop                 ; nop           ; sbwait
        mov tlb_z, rb15     ; nop
        nop                 ; nop           ; ldtmu0    # r4 = texel
        v8muld r0, r4, r2   ; nop                       # texel * light / 2
        v8adds r0, r0, r0   ; nop                       # * 2
        v8adds r0, r0, r3   ; nop                       # + fog
        mov tlbc, r0        ; nop           ; thrend
        nop                 ; nop
        nop                 ; nop           ; sbdone
    """,
    # the same where the texture has transparent texels: alpha 0 writes
    # neither colour nor depth (early z off)
    "fs_tex_rgb_alpha": """
        nop                 ; nop
        nop                 ; nop
        mov r0, vary        ; nop                       # s
        fmul r0, r0, ra15   ; nop
        fadd r0, r0, r5     ; nop
        mov r1, vary        ; nop                       # t
        fmul r1, r1, ra15   ; nop
        fadd r1, r1, r5     ; nop
        mov t0t, r1         ; nop
        mov t0s, r0         ; nop                       # starts the lookup
        mov r0, vary        ; nop                       # light, byte a
        fmul r0, r0, ra15   ; nop
        fadd r0, r0, r5     ; nop
        mov r1, vary        ; mov r2.8a, r0             # light, byte b
        fmul r1, r1, ra15   ; nop
        fadd r1, r1, r5     ; nop
        mov r0, vary        ; mov r2.8b, r1             # light, byte c
        fmul r0, r0, ra15   ; nop
        fadd r0, r0, r5     ; nop
        mov r1, vary        ; mov r2.8c, r0             # fog, byte a
        fmul r1, r1, ra15   ; nop
        fadd r1, r1, r5     ; nop
        mov r0, vary        ; mov r3.8a, r1             # fog, byte b
        fmul r0, r0, ra15   ; nop
        fadd r0, r0, r5     ; nop
        mov r1, vary        ; mov r3.8b, r0             # fog, byte c
        fmul r1, r1, ra15   ; nop
        fadd r1, r1, r5     ; nop
        nop                 ; mov r3.8c, r1
        nop                 ; mov r2.8d, 1.0
        nop                 ; mov r3.8d, 0
        nop                 ; nop           ; sbwait
        nop                 ; nop           ; ldtmu0    # r4 = texel
        shr r1, r4, 15      ; v8muld r0, r4, r2         # texel * light / 2
        shr.setf nop, r1, 9 ; v8adds r0, r0, r0         # Z: alpha is 0; * 2
        v8adds r0, r0, r3   ; nop                       # + fog
        mov.ifnz tlb_z, rb15 ; nop
        mov.ifnz tlbc, r0   ; nop           ; thrend
        nop                 ; nop
        nop                 ; nop           ; sbdone
    """,
}

# shaders run on a Pi (Zero W) by others, as the reference of the encoding
REFERENCE = [
    # Peter Lemon, V3D/ControlList/NV/Vertex_Array/Triangle/VertexColor
    ("""
        mov r0, vary        ; mov r3.8d, 1.0
        fadd r0, r0, r5     ; mov r1, vary  ; sbwait
        fadd r1, r1, r5     ; mov r2, vary
        fadd r2, r2, r5     ; mov r3.8a, r0
        nop                 ; mov r3.8b, r1
        nop                 ; mov r3.8c, r2
        mov tlbc, r3        ; nop           ; thrend
        nop                 ; nop
        nop                 ; nop           ; sbdone
     """, [0x958E0DBF, 0xD1724823, 0x818E7176, 0x40024821, 0x818E7376, 0x10024862, 0x819E7540,
           0x114248A3, 0x809E7009, 0x115049E3, 0x809E7012, 0x116049E3, 0x159E76C0, 0x30020BA7,
           0x009E7000, 0x100009E7, 0x009E7000, 0x500009E7]),
    # kumaashi, RPIZEROW/Sample_V3D_RT_07/fs_normal_texture_z
    ("""
        nop                 ; nop
        nop                 ; nop
        mov r3, ra15        ; nop
        mov r0, vary        ; nop
        fmul r0, r0, r3     ; nop
        fadd r0, r0, r5     ; nop
        mov t0t, r0         ; nop           ; nop
        mov r0, vary        ; nop
        fmul r0, r0, r3     ; nop
        fadd r0, r0, r5     ; nop
        mov t0s, r0         ; nop           ; nop
        mov r0, vary        ; nop
        fmul r0, r0, r3     ; nop
        fadd r0, r0, r5     ; mov r1, vary  ; sbwait
        fmul r1, r1, r3     ; nop
        fadd r1, r1, r5     ; mov r2, vary
        fmul r2, r2, r3     ; nop
        fadd r2, r2, r5     ; mov r3.8a, r0
        nop                 ; mov r3.8b, r1
        nop                 ; mov r3.8c, r2
        mov tlb_z, rb15     ; nop
        nop                 ; nop           ; ldtmu0
        v8muld r3, r3, r4   ; nop
        v8adds r3, r3, r4   ; nop
        mov tlbc, r3        ; nop           ; thrend
        nop                 ; nop           ; nop
        nop                 ; nop           ; sbdone
     """, [0x009e7000, 0x100009e7, 0x009e7000, 0x100009e7, 0x153e7d80, 0x100208e7, 0x158e7d80,
           0x10020827, 0x209e7003, 0x100049e0, 0x019e7140, 0x10020827, 0x159e7000, 0x10020e67,
           0x158e7d80, 0x10020827, 0x209e7003, 0x100049e0, 0x019e7140, 0x10020827, 0x159e7000,
           0x10020e27, 0x158e7d80, 0x10020827, 0x209e7003, 0x100049e0, 0x818e7176, 0x40024821,
           0x209e700b, 0x100049e1, 0x818e7376, 0x10024862, 0x209e7013, 0x100049e2, 0x819e7540,
           0x11c248a3, 0x809e7009, 0x11d049e3, 0x809e7012, 0x11e049e3, 0x159cffc0, 0x10020b27,
           0x009e7000, 0xa00009e7, 0x609e701c, 0x100049e3, 0xc09e701c, 0x100049e3, 0x159e76c0,
           0x30020ba7, 0x009e7000, 0x100009e7, 0x009e7000, 0x500009e7]),
]


def same(got, words):
    """equal, but for the mul pack of a byte, which some vc4asm versions
    encode as 12..15 instead of the documented 4..7 (both run on a Pi)"""
    if len(got) != len(words):
        return False
    for k in range(0, len(got), 2):
        g, w = got[k + 1], words[k + 1]
        if (g >> 24 & 1) and (g >> 20 & 0xF) in (4, 5, 6, 7):
            w &= ~(8 << 20)
        if got[k] != words[k] or g != w:
            return False
    return True


def self_test():
    ok = True
    for i, (src, words) in enumerate(REFERENCE):
        got = [w for (lo, hi), _ in assemble(src) for w in (lo, hi)]
        if not same(got, words):
            ok = False
            for k, (g, w) in enumerate(zip(got, words)):
                if g != w:
                    print(f"reference {i}: word {k}: got {g:08x}, expected {w:08x}")
            if len(got) != len(words):
                print(f"reference {i}: {len(got)} words, expected {len(words)}")
    for name, src in SHADERS.items():
        assemble(src)
    print("qpuasm: " + ("ok" if ok else "FAILED"))
    return ok


def write_header(path):
    lines = ["/* Generated by tools/qpuasm.py from its SHADERS: do not edit. */",
             "#ifndef GPU_SHADERS_H", "#define GPU_SHADERS_H", "", "#include <stdint.h>", ""]
    for name, src in SHADERS.items():
        code = assemble(src)
        lines.append(f"static const uint32_t {name}[{2 * len(code)}] __attribute__((aligned(8))) = {{")
        for (lo, hi), text in code:
            lines.append(f"    0x{lo:08x}, 0x{hi:08x},     /* {text} */")
        lines.append("};")
        lines.append("")
    lines.append("#endif")
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--test", action="store_true")
    ap.add_argument("-o", "--output")
    a = ap.parse_args()
    if a.test:
        return 0 if self_test() else 1
    if a.output:
        if not self_test():
            return 1
        write_header(a.output)
        return 0
    ap.print_help()
    return 0


if __name__ == "__main__":
    sys.exit(main())
