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
import struct
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
RADDR = {"unif": 32, "vary": 35, "elem_num": 38, "nop": 39, "vpm": 48}   # readable from file A or B
# written through one register file: (address, file)
WFILE = {"vr_setup": (49, "A"), "vw_setup": (49, "B"), "vr_addr": (50, "A"), "vw_addr": (50, "B")}
PACK_A = {"16a": 1, "16b": 2}           # regfile A pack (pm 0): the low or high half of the register
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


def load_imm(line, lineno):
    """ldi dst, value: a 32-bit immediate (integer, hex or float) written by
    the add ALU's slot (regfile A, B or an accumulator)"""
    m = re.fullmatch(r"ldi\s+(\S+?)\s*,\s*(\S+)", line)
    if not m:
        raise SyntaxError(f"line {lineno}: ldi dst, value: {line}")
    dst, val = m[1], m[2]
    if re.fullmatch(r"-?(0x[0-9a-fA-F]+|\d+)", val):
        imm = int(val, 0) & 0xFFFFFFFF
    else:
        imm = struct.unpack("<I", struct.pack("<f", float(val)))[0]
    ws = 0
    mm = re.fullmatch(r"r([ab])(\d+)", dst)
    if mm:
        waddr, ws = int(mm[2]), 1 if mm[1] == "b" else 0
    elif dst in WFILE:
        waddr, f = WFILE[dst]
        ws = 1 if f == "B" else 0
    elif dst in WADDR:
        waddr = WADDR[dst]
    else:
        raise SyntaxError(f"line {lineno}: unknown destination {dst}")
    # signal 14 (load immediate, 32 bits), cond_add always, cond_mul never
    hi = 14 << 28 | COND["always"] << 17 | ws << 12 | waddr << 6 | WADDR["nop"]
    return imm, hi


def assemble_line(line, lineno):
    if line.startswith("ldi "):
        return load_imm(line, lineno)
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
            if pack in PACK_A and re.fullmatch(r"ra\d+", tok):
                fields["pm"] = 0                # a half of a regfile A register
                fields["pack"] = PACK_A[pack]
            elif alu != "mul" or pack not in PACK_MUL:
                raise SyntaxError(f"line {lineno}: pack .{pack} only on the mul ALU")
            else:
                fields["pm"] = 1
                fields["pack"] = PACK_MUL[pack]
        m = re.fullmatch(r"r([ab])(\d+)", tok)
        if m:
            return int(m[2]), m[1].upper()
        if tok in WFILE:
            return WFILE[tok]
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

# Vertex and coordinate shaders for the GL shader state (M36): the V3D
# reads the vertices of a mesh, the QPUs place them. The VCD puts word k of
# the attributes of 16 vertices in row k of the VPM; a read of "vpm" gives
# the next row (vr_setup), a write fills the next (vw_setup). Uniforms, in
# the order read: the object-to-camera matrix by rows (m00 m01 m02 m03, m10
# .., m20 ..), f*16, 0.5, -f*16, 0.5, -NEAR; the coordinate shader then
# reads f/hw and f/hh (its clip coordinates). Screen x, y in 12.4 from the
# screen's centre (VIEWPORT_OFFSET adds it, as with Mesa's shaders: the
# corners made by the clipper are placed the same way), rounded as the NV
# vertices (floor(v + 0.5): ftoi, then one less where that went up), and
# z = 1 - NEAR/depth as the ARM's: the two kinds of batches share a job.

XFORM = """
        nop                 ; fmul r0, ra0, unif        # m00 x
        nop                 ; fmul r1, ra1, unif        # m01 y
        fadd r0, r0, r1     ; fmul r1, ra2, unif        # m02 z
        fadd r0, r0, r1     ; nop
        fadd ra6, r0, unif  ; nop                       # + m03: x in the camera
        nop                 ; fmul r0, ra0, unif
        nop                 ; fmul r1, ra1, unif
        fadd r0, r0, r1     ; fmul r1, ra2, unif
        fadd r0, r0, r1     ; nop
        fadd rb6, r0, unif  ; nop                       # y in the camera
        nop                 ; fmul r0, ra0, unif
        nop                 ; fmul r1, ra1, unif
        fadd r0, r0, r1     ; fmul r1, ra2, unif
        fadd r0, r0, r1     ; nop
        fadd r2, r0, unif   ; nop                       # depth
        mov sfu_recip, r2   ; nop                       # r4 = 1 / depth, 2 instructions on
        mov rb7, r2         ; nop
        nop                 ; nop
        nop                 ; fmul r0, ra6, r4          # x / depth
        nop                 ; fmul r0, r0, unif         # * f*16
        fadd r0, r0, unif   ; fmul r1, rb6, r4          # + 0.5; y / depth
        ftoi r2, r0         ; fmul r1, r1, unif         # x truncated; * -f*16
        fadd r1, r1, unif   ; nop                       # + 0.5
        itof r3, r2         ; nop
        fsub.setf nop, r0, r3 ; nop                     # N: below its truncation
        sub.ifn r2, r2, 1   ; nop                       # screen x (12.4), floored
        ftoi r0, r1         ; nop
        itof r3, r0         ; nop
        fsub.setf nop, r1, r3 ; nop
        sub.ifn r0, r0, 1   ; nop                       # screen y
        mov ra9.16a, r2     ; nop
        mov ra9.16b, r0     ; fmul r2, r4, unif         # -NEAR / depth
        fadd rb9, r2, 1.0   ; nop                       # z = 1 - NEAR / depth
"""

# The lamps (M36): four, each read as x y z (camera space), 1/r^2 and its
# colour times k (bytes a b c): s = max(0, 1 - d^2/r^2), the light in
# ra5 rb3 rb4 (bytes a b c) += s * colour. A lamp that is off has k 0.
LAMP = """
        fsub r0, ra6, unif  ; nop                       # dx
        fsub r1, rb6, unif  ; fmul r0, r0, r0           # dy; dx^2
        fsub r2, rb7, unif  ; fmul r1, r1, r1           # dz; dy^2
        fadd r0, r0, r1     ; fmul r2, r2, r2
        fadd r0, r0, r2     ; nop                       # d^2
        nop                 ; fmul r0, r0, unif         # / r^2
        fsub r0, 1.0, r0    ; nop
        fmax r0, r0, 0      ; nop                       # s
        nop                 ; fmul r1, r0, unif
        fadd ra5, ra5, r1   ; fmul r2, r0, unif
        fadd rb3, rb3, r2   ; fmul r3, r0, unif
        fadd rb4, rb4, r3   ; nop
"""

# The fog: f = clamp((depth - near) * k, 0, 1) in r0 and ra10 (readable
# from the second instruction after; near and k read)
FOG = """
        fsub r0, rb7, unif  ; nop                       # depth - near
        nop                 ; fmul r0, r0, unif         # * k
        fmax r0, r0, 0      ; nop
        fmin r0, r0, 1.0    ; nop                       # f
        nop                 ; mov ra10, r0
"""

# A "lit" model's textured faces (the world of a map): the light baked at
# the corner (bytes a b c) plus the lamps, halved and less the fog's share
# (the fragment shader doubles it), and the fog's colour times f: the
# varyings of fs_tex_rgb. Attributes x y z, s t, light (8 words); uniforms
# after the placing ones: the fog (near, k, colour a b c), four lamps.
SHADERS["vs_tex_rgb"] = """
        ldi vr_setup, 0x801a00                          # 8 rows
        ldi vw_setup, 0x1a00
        nop                 ; nop
        nop                 ; nop
        mov ra0, vpm        ; nop
        mov ra1, vpm        ; nop
        mov ra2, vpm        ; nop
        mov ra3, vpm        ; nop                       # s
        mov ra4, vpm        ; nop                       # t
        mov ra5, vpm        ; nop                       # light a
        mov rb3, vpm        ; nop                       # light b
        mov rb4, vpm        ; nop                       # light c
""" + XFORM + FOG + """
        nop                 ; fmul ra11, r0, unif       # fog a * f
        nop                 ; fmul rb11, r0, unif       # fog b * f
        nop                 ; fmul ra12, r0, unif       # fog c * f
""" + LAMP * 4 + """
        fsub r3, 1.0, ra10  ; nop                       # 1 - f
        nop                 ; fmul r3, r3, 0.5          # kl
        nop                 ; fmul r0, ra5, r3          # light * kl
        nop                 ; fmul r1, rb3, r3
        nop                 ; fmul r2, rb4, r3
        fmin r0, r0, 1.0    ; nop
        fmin r1, r1, 1.0    ; nop
        fmin r2, r2, 1.0    ; nop
        mov vpm, ra9        ; nop                       # screen x, y
        mov vpm, rb9        ; nop                       # z
        mov vpm, r4         ; nop                       # 1 / w
        mov vpm, ra3        ; nop                       # s
        mov vpm, ra4        ; nop                       # t
        mov vpm, r0         ; nop                       # light a b c
        mov vpm, r1         ; nop
        mov vpm, r2         ; nop
        mov vpm, ra11       ; nop                       # fog a b c
        mov vpm, rb11       ; nop
        mov vpm, ra12       ; nop
        nop                 ; nop           ; thrend
        nop                 ; nop
        nop                 ; nop
"""

# Faces of a colour (bytes a b c, 0..1) times a light at the corner (baked,
# or 1 for unlit meshes) plus the lamps, then the fog (colour + (fog -
# colour) * f), at most 1: the varyings of fs_colour. Attributes x y z, colour,
# light (9 words); uniforms as vs_tex_rgb.
SHADERS["vs_baked"] = """
        ldi vr_setup, 0x901a00                          # 9 rows
        ldi vw_setup, 0x1a00
        nop                 ; nop
        nop                 ; nop
        mov ra0, vpm        ; nop
        mov ra1, vpm        ; nop
        mov ra2, vpm        ; nop
        mov ra3, vpm        ; nop                       # colour a
        mov ra4, vpm        ; nop                       # colour b
        mov rb2, vpm        ; nop                       # colour c
        mov ra5, vpm        ; nop                       # light a
        mov rb3, vpm        ; nop                       # light b
        mov rb4, vpm        ; nop                       # light c
""" + XFORM + FOG + """
        mov ra11, unif      ; nop                       # fog a b c
        mov rb11, unif      ; nop
        mov ra12, unif      ; nop
""" + LAMP * 4 + """
        mov r0, ra3         ; nop                       # colour * light
        mov r2, rb2         ; fmul r0, r0, ra5
        nop                 ; fmul r1, ra4, rb3
        nop                 ; fmul r2, r2, rb4
        fsub r3, ra11, r0   ; nop                       # (fog - colour) * f + colour
        nop                 ; fmul r3, r3, ra10
        fadd r0, r0, r3     ; nop
        fsub r3, rb11, r1   ; nop
        nop                 ; fmul r3, r3, ra10
        fadd r1, r1, r3     ; nop
        fsub r3, ra12, r2   ; nop
        nop                 ; fmul r3, r3, ra10
        fadd r2, r2, r3     ; nop
        fmin r0, r0, 1.0    ; nop                       # at most 1, as the ARM after its fog
        fmin r1, r1, 1.0    ; nop
        fmin r2, r2, 1.0    ; nop
        mov vpm, ra9        ; nop
        mov vpm, rb9        ; nop
        mov vpm, r4         ; nop
        mov vpm, r0         ; nop
        mov vpm, r1         ; nop
        mov vpm, r2         ; nop
        nop                 ; nop           ; thrend
        nop                 ; nop
        nop                 ; nop
"""

# The point of the lamps and the fog (ra20 ra21 ra22: the corner itself,
# or the middle of a flat face, as the ARM lights it) into the camera's
# axes (ra6 rb6 rb7, where XFORM leaves the corner), by the matrix again
XPOINT = """
        nop                 ; fmul r0, ra20, unif
        nop                 ; fmul r1, ra21, unif
        fadd r0, r0, r1     ; fmul r1, ra22, unif
        fadd r0, r0, r1     ; nop
        fadd ra6, r0, unif  ; nop
        nop                 ; fmul r0, ra20, unif
        nop                 ; fmul r1, ra21, unif
        fadd r0, r0, r1     ; fmul r1, ra22, unif
        fadd r0, r0, r1     ; nop
        fadd rb6, r0, unif  ; nop
        nop                 ; fmul r0, ra20, unif
        nop                 ; fmul r1, ra21, unif
        fadd r0, r0, r1     ; fmul r1, ra22, unif
        fadd r0, r0, r1     ; nop
        fadd rb7, r0, unif  ; nop
"""

# A dot product of the normal (ra3 ra4 rb0) with the next three uniforms
def DOT(dst):
    return f"""
        nop                 ; fmul r0, ra3, unif
        nop                 ; fmul r1, ra4, unif
        fadd r0, r0, r1     ; fmul r1, rb0, unif
        fadd {dst}, r0, r1  ; nop
"""

# The light of one byte of the colour: u * B + max(d, 0) * D + e^2 * R + A
# (u, the normal's height, in ra14; max(d, 0) in r3; e^2 in r2)
def SHADE(dst):
    return f"""
        nop                 ; fmul r0, ra14, unif
        nop                 ; fmul r1, r3, unif
        fadd r0, r0, r1     ; fmul r1, r2, unif
        fadd r0, r0, r1     ; nop
        fadd {dst}, r0, unif ; nop
"""

# Models lit by the sun (M36, the heroes): r3d's light_fast at each corner,
# with the corner's normal (the vertex's for Gouraud, the face's else) in
# the axes of its bone: sky and ground by its height, the sun, the rim
# (1 - n.V)^2, the highlight (n.H)^p where the sun lights it, the lamps;
# an emissive corner at full light. colour * light + highlight, the fog,
# at most 1: the varyings of fs_colour. Attributes x y z, the normal, the
# colour (bytes a b c), emissive (0/1), glossy and not emissive (0/1), the
# point of the lamps and the fog: 14 words. Uniforms after the placing
# ones: the matrix again (XPOINT), H, the sun, the up axis and V in
# the bone's axes (3 each), p, then per byte of the colour B D R A (u * B
# + max(d, 0) * D + e^2 * R + A), the fog (near, k, colour a b c), four
# lamps, the highlight's colour (a b c).
SHADERS["vs_lit"] = """
        ldi vr_setup, 0xe01a00                          # 14 rows
        ldi vw_setup, 0x1a00
        nop                 ; nop
        nop                 ; nop
        mov ra0, vpm        ; nop
        mov ra1, vpm        ; nop
        mov ra2, vpm        ; nop
        mov ra3, vpm        ; nop                       # the normal
        mov ra4, vpm        ; nop
        mov rb0, vpm        ; nop
        mov rb1, vpm        ; nop                       # colour a
        mov ra17, vpm       ; nop                       # colour b
        mov ra7, vpm        ; nop                       # colour c
        mov ra8, vpm        ; nop                       # emissive
        mov rb5, vpm        ; nop                       # glossy
        mov ra20, vpm       ; nop                       # the point of the lamps and the fog
        mov ra21, vpm       ; nop
        mov ra22, vpm       ; nop
""" + XFORM + """
        mov ra13, r4        ; nop                       # 1 / depth (the SFU works again below)
""" + XPOINT + DOT("ra15") + DOT("ra18") + DOT("ra14") + DOT("ra19") + """
        fmax r0, ra15, 0.00390625 ; nop                 # n.H, at least 1/256
        mov sfu_log, r0     ; nop
        nop                 ; nop
        nop                 ; nop
        nop                 ; fmul r0, r4, unif         # log2(n.H) * p
        mov sfu_exp, r0     ; nop
        mov.setf nop, ra18  ; nop                       # flags of d = n.sun
        nop                 ; nop
        mov r1, r4          ; nop                       # (n.H)^p
        mov.ifn r1, 0       ; nop                       # none where the sun does not light it
        mov.ifz r1, 0       ; nop
        mov.setf nop, ra15  ; nop
        mov.ifn r1, 0       ; nop
        mov.ifz r1, 0       ; nop
        nop                 ; fmul ra16, r1, rb5        # the highlight of a glossy corner
        fmax r3, ra18, 0    ; nop                       # max(d, 0)
        fmax r2, ra19, 0    ; nop
        fsub r2, 1.0, r2    ; nop
        nop                 ; fmul r2, r2, r2           # e^2: the rim
""" + SHADE("ra5") + SHADE("rb3") + SHADE("rb4") + FOG + """
        mov ra11, unif      ; nop                       # fog a b c
        mov rb11, unif      ; nop
        mov ra12, unif      ; nop
""" + LAMP * 4 + """
        fsub r0, 1.0, ra5   ; nop                       # emissive: full light
        mov r1, rb3         ; fmul r0, r0, ra8
        fadd ra5, ra5, r0   ; nop
        fsub r1, 1.0, r1    ; nop
        mov r2, rb4         ; fmul r1, r1, ra8
        fadd rb3, rb3, r1   ; nop
        fsub r2, 1.0, r2    ; nop
        nop                 ; fmul r2, r2, ra8
        fadd rb4, rb4, r2   ; nop
        nop                 ; fmul r0, rb1, ra5         # colour * light + highlight
        nop                 ; fmul r3, ra16, unif
        fadd r0, r0, r3     ; fmul r1, ra17, rb3
        nop                 ; fmul r3, ra16, unif
        fadd r1, r1, r3     ; fmul r2, ra7, rb4
        nop                 ; fmul r3, ra16, unif
        fadd r2, r2, r3     ; nop
        fsub r3, ra11, r0   ; nop                       # (fog - colour) * f + colour
        nop                 ; fmul r3, r3, ra10
        fadd r0, r0, r3     ; nop
        fsub r3, rb11, r1   ; nop
        nop                 ; fmul r3, r3, ra10
        fadd r1, r1, r3     ; nop
        fsub r3, ra12, r2   ; nop
        nop                 ; fmul r3, r3, ra10
        fadd r2, r2, r3     ; nop
        fmin r0, r0, 1.0    ; nop
        fmin r1, r1, 1.0    ; nop
        fmin r2, r2, 1.0    ; nop
        mov vpm, ra9        ; nop
        mov vpm, rb9        ; nop
        mov vpm, ra13       ; nop
        mov vpm, r0         ; nop
        mov vpm, r1         ; nop
        mov vpm, r2         ; nop
        nop                 ; nop           ; thrend
        nop                 ; nop
        nop                 ; nop
"""

# Textured faces of models lit by the sun (Overbit's Meshy heroes): the
# light of vs_lit at the corner without its highlight, grey as r3d's
# lit_grey ((r + g + b) / 3, at most 1) and halved, no fog (as the ARM's
# textured faces): the varyings of fs_tex_rgb. Attributes x y z, the
# normal, s t, emissive (0/1), the point of the lamps: 12 words. The
# uniforms of vs_lit (H, p, the fog's colour and the highlight's read or
# left unused).
SHADERS["vs_lit_tex"] = """
        ldi vr_setup, 0xc01a00                          # 12 rows
        ldi vw_setup, 0x1a00
        nop                 ; nop
        nop                 ; nop
        mov ra0, vpm        ; nop
        mov ra1, vpm        ; nop
        mov ra2, vpm        ; nop
        mov ra3, vpm        ; nop                       # the normal
        mov ra4, vpm        ; nop
        mov rb0, vpm        ; nop
        mov rb1, vpm        ; nop                       # s
        mov ra17, vpm       ; nop                       # t
        mov ra8, vpm        ; nop                       # emissive
        mov ra20, vpm       ; nop                       # the point of the lamps
        mov ra21, vpm       ; nop
        mov ra22, vpm       ; nop
""" + XFORM + """
        mov ra13, r4        ; nop                       # 1 / depth
""" + XPOINT + """
        mov r0, unif        ; nop                       # H: no highlight on a texture
        mov r0, unif        ; nop
        mov r0, unif        ; nop
""" + DOT("ra18") + DOT("ra14") + DOT("ra19") + """
        mov r0, unif        ; nop                       # p
        fmax r3, ra18, 0    ; nop                       # max(d, 0)
        fmax r2, ra19, 0    ; nop
        fsub r2, 1.0, r2    ; nop
        nop                 ; fmul r2, r2, r2           # e^2: the rim
""" + SHADE("ra5") + SHADE("rb3") + SHADE("rb4") + FOG + """
        mov r0, unif        ; nop                       # the fog's colour: not used
        mov r0, unif        ; nop
        mov r0, unif        ; nop
""" + LAMP * 4 + """
        fsub r0, 1.0, ra5   ; nop                       # emissive: full light
        mov r1, rb3         ; fmul r0, r0, ra8
        fadd ra5, ra5, r0   ; nop
        fsub r1, 1.0, r1    ; nop
        mov r2, rb4         ; fmul r1, r1, ra8
        fadd rb3, rb3, r1   ; nop
        fsub r2, 1.0, r2    ; nop
        nop                 ; fmul r2, r2, ra8
        fadd rb4, rb4, r2   ; nop
        fadd r0, ra5, rb3   ; nop                       # grey: (a + b + c) / 3
        ldi r1, 0x3eaaaaab                              # 1/3
        fadd r0, r0, rb4    ; nop
        nop                 ; fmul r0, r0, r1
        fmin r0, r0, 1.0    ; nop                       # at most 1
        nop                 ; fmul r0, r0, 0.5          # halved: fs_tex_rgb doubles it
        mov vpm, ra9        ; nop                       # screen x, y
        mov vpm, rb9        ; nop                       # z
        mov vpm, ra13       ; nop                       # 1 / w
        mov vpm, rb1        ; nop                       # s
        mov vpm, ra17       ; nop                       # t
        mov vpm, r0         ; nop                       # light a b c
        mov vpm, r0         ; nop
        mov vpm, r0         ; nop
        mov vpm, 0          ; nop                       # fog a b c: none
        mov vpm, 0          ; nop
        mov vpm, 0          ; nop
        nop                 ; nop           ; thrend
        nop                 ; nop
        nop                 ; nop
"""

# Shadows (M36): the corner (ra0 ra1 ra2) into world axes relative to the
# camera by its bone (12 uniforms), then down along the sun to the ground's
# plane, as r3d's draw_shadow: h = max(0, (y - plane) / Ly), x - Lx h,
# plane + 0.01, z - Lz h, back into ra0 ra1 ra2 for XFORM (the camera's
# turn, no move). Uniforms: the matrix, plane, 1/Ly, Lx, Lz, plane + 0.01.
SHADOW = """
        nop                 ; fmul r0, ra0, unif
        nop                 ; fmul r1, ra1, unif
        fadd r0, r0, r1     ; fmul r1, ra2, unif
        fadd r0, r0, r1     ; nop
        fadd ra3, r0, unif  ; nop                       # x in the world
        nop                 ; fmul r0, ra0, unif
        nop                 ; fmul r1, ra1, unif
        fadd r0, r0, r1     ; fmul r1, ra2, unif
        fadd r0, r0, r1     ; nop
        fadd r3, r0, unif   ; nop                       # y
        nop                 ; fmul r0, ra0, unif
        nop                 ; fmul r1, ra1, unif
        fadd r0, r0, r1     ; fmul r1, ra2, unif
        fadd r0, r0, r1     ; nop
        fadd ra4, r0, unif  ; nop                       # z
        fsub r0, r3, unif   ; nop                       # y - plane
        nop                 ; fmul r0, r0, unif         # / Ly
        fmax r0, r0, 0      ; nop                       # h
        nop                 ; fmul r1, r0, unif         # Lx h
        fsub ra0, ra3, r1   ; fmul r2, r0, unif         # x - Lx h; Lz h
        fsub ra2, ra4, r2   ; nop                       # z - Lz h
        mov ra1, unif       ; nop                       # on the plane
"""

# The shadow's corner: black (fs_colour_screen), its depth a little nearer
# (XFORM's -NEAR uniform is -1.035 NEAR, as the ARM's 1.035 / depth)
SHADERS["vs_shadow"] = """
        ldi vr_setup, 0x301a00                          # 3 rows: x y z
        ldi vw_setup, 0x1a00
        nop                 ; nop
        nop                 ; nop
        mov ra0, vpm        ; nop
        mov ra1, vpm        ; nop
        mov ra2, vpm        ; nop
""" + SHADOW + XFORM + """
        mov vpm, ra9        ; nop
        mov vpm, rb9        ; nop
        mov vpm, r4         ; nop
        mov vpm, 0          ; nop
        mov vpm, 0          ; nop
        mov vpm, 0          ; nop
        nop                 ; nop           ; thrend
        nop                 ; nop
        nop                 ; nop
"""

SHADERS["cs_colour"] = """
        ldi vr_setup, 0x301a00                          # 3 rows: x y z
        ldi vw_setup, 0x1a00
        nop                 ; nop
        nop                 ; nop
        mov ra0, vpm        ; nop
        mov ra1, vpm        ; nop
        mov ra2, vpm        ; nop
""" + XFORM + """
        nop                 ; fmul r0, ra6, unif        # clip x = x * f / (width/2)
        nop                 ; fmul r1, rb6, unif        # clip y = y * f / (height/2)
        nop                 ; fmul r3, r2, rb7          # clip z = -NEAR: at w = NEAR the near plane
        mov vpm, r0         ; nop
        mov vpm, r1         ; nop
        mov vpm, r3         ; nop
        mov vpm, rb7        ; nop                       # clip w = depth
        mov vpm, ra9        ; nop
        mov vpm, rb9        ; nop
        mov vpm, r4         ; nop
        nop                 ; nop           ; thrend
        nop                 ; nop
        nop                 ; nop
"""

# the coordinate shader of the shadows
SHADERS["cs_shadow"] = """
        ldi vr_setup, 0x301a00                          # 3 rows: x y z
        ldi vw_setup, 0x1a00
        nop                 ; nop
        nop                 ; nop
        mov ra0, vpm        ; nop
        mov ra1, vpm        ; nop
        mov ra2, vpm        ; nop
""" + SHADOW + XFORM + """
        nop                 ; fmul r0, ra6, unif        # clip x = x * f / (width/2)
        nop                 ; fmul r1, rb6, unif        # clip y = y * f / (height/2)
        nop                 ; fmul r3, r2, rb7          # clip z = -NEAR: at w = NEAR the near plane
        mov vpm, r0         ; nop
        mov vpm, r1         ; nop
        mov vpm, r3         ; nop
        mov vpm, rb7        ; nop                       # clip w = depth
        mov vpm, ra9        ; nop
        mov vpm, rb9        ; nop
        mov vpm, r4         ; nop
        nop                 ; nop           ; thrend
        nop                 ; nop
        nop                 ; nop
"""

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
