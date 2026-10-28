#!/usr/bin/env python3
"""Packs the test descriptors of tests/rgb30/mali_desc_test.c straight from
Mesa's v7.xml (src/panfrost/genxml/v7.xml, MIT), the way the XML says, and
writes them as tests/rgb30/mali_desc_golden.h. Run when a descriptor is
added:  python3 tests/rgb30/v7pack.py /path/to/v7.xml

The C packers (src/rgb30/mali_desc.c) are written by hand from the same
file; the test compares the two."""
import struct
import sys
import xml.etree.ElementTree as ET


def load(path):
    out = {}
    for s in ET.parse(path).getroot().iter("struct"):
        fields = {}
        for f in s.findall("field"):
            st = f.get("start")
            if ":" in st:
                w, b = st.split(":")
                bit = int(w) * 32 + int(b)
            else:
                bit = int(st)
            fields[f.get("name")] = (bit, int(f.get("size")), f.get("modifier"), f.get("type"))
        out[s.get("name")] = (int(s.get("size", "0")) * 4, fields)
    # a field that is a struct of its own: its fields are reachable by name too (the first of that name)
    changed = True
    while changed:
        changed = False
        for _, fields in out.values():
            for bit, sz, mod, ty in list(fields.values()):
                if ty in out:
                    for k, (b2, s2, m2, t2) in out[ty][1].items():
                        if k not in fields:
                            fields[k] = (bit + b2, s2, m2, t2)
                            changed = True
    return out


def pack(structs, name, values, size=None):
    n, fields = structs[name]
    buf = bytearray(size or n)
    for k, v in values.items():
        bit, sz, mod, ty = fields[k]
        if ty == "padded":      # __gen_padded: shift | odd << 5
            sh = (v & -v).bit_length() - 1
            v = sh | ((v >> (sh + 1)) << 5)
        if isinstance(v, float):
            v = struct.unpack("<I", struct.pack("<f", v))[0]
        if mod and mod.startswith("minus("):
            v -= int(mod[6:-1])
        if mod and mod.startswith("shr("):
            v >>= int(mod[4:-1])
        if mod == "log2":
            v = v.bit_length() - 1
        v &= (1 << sz) - 1
        for i in range(sz):
            if v >> i & 1:
                b = bit + i
                buf[b >> 3] |= 1 << (b & 7)
    return bytes(buf)


# the same values as tests/rgb30/mali_desc_test.c
def vectors(x):
    ms = {"Sample mask": 0xffff, "Fixed-function depth range fixed": 1, "Shader depth range fixed": 1,
          "Depth function": 7, "Fixed-function near discard": 1, "Fixed-function far discard": 1,
          "Alpha test compare function": 7, "Stencil front": 0xff00, "Stencil back": 0xff00}
    return {
        "primitive": pack(x, "Primitive", {
            "Draw mode": 8, "Index type": 2, "First provoking vertex": 1, "Low Depth Cull": 1,
            "High Depth Cull": 1, "Job Task Split": 6, "Base vertex offset": 5, "Index count": 12,
            "Indices": 0x0000_0001_2345_6780}, 24),
        "draw": pack(x, "Draw", {
            "Four Components Per Vertex": 1, "Draw Descriptor Is 64b": 1, "Front face CCW": 1, "Cull back face": 1,
            "Instance Size": 1, "Instance Primitive Size": 1, "Position": 0x1111_0000_0040,
            "Uniform buffers": 0x9999_0000_0240, "Push uniforms": 0xaaaa_0000_0280,
            "State": 0x2222_0000_0080, "Attribute buffers": 0x3333_0000_00c0, "Attributes": 0x4444_0000_0100,
            "Varying buffers": 0x5555_0000_0140, "Varyings": 0x6666_0000_0180, "Viewport": 0x7777_0000_01c0,
            "Thread Storage": 0x8888_0000_0200, "Textures": 0xbbbb_0000_0300, "Samplers": 0xcccc_0000_0340}),
        "renderer_state_fs": pack(x, "Renderer State", dict(ms, **{
            "Shader": 0x0000_0009_8765_4300, "Attribute count": 0, "Varying count": 0,
            "Uniform buffer count": 1, "Shader register allocation": 2, "Allow forward pixel to be killed": 1,
            "Pixel kill operation": 2, "ZS update operation": 2, "Shader wait dependency 6": 1,
            "Shader wait dependency 7": 1, "Coverage": 1, "Uniform count": 4}), 64),
        "renderer_state_vs": pack(x, "Renderer State", dict(ms, **{
            "Shader": 0x0000_0009_8765_4400, "Attribute count": 1, "Varying count": 1,
            "Uniform buffer count": 0, "Shader register allocation": 2, "Vertex ID": 1, "Instance ID": 1,
            "Uniform count": 0}), 64),
        "blend": pack(x, "Blend", {
            "Enable": 1, "Round to FB precision": 1, "RGB": 0x122, "Alpha": 0x122, "Color Mask": 0xf,
            "Mode": 1, "Num Comps": 4, "RT": 0, "Memory Format": (187 << 12) | 4, "Register Format": 1}, 16),
        "local_storage": pack(x, "Local Storage", {"WLS Instances": 0x80000000}, 64),
        "uniform_buffer": pack(x, "Uniform Buffer", {"Entries": 2, "Pointer": 0x0000_0003_c000_4100}, 8),
        "invocation": pack(x, "Invocation", {
            "Invocations": 3 - 1, "Workgroups Z shift": 32, "Thread group split": 2}, 8),
        "viewport": pack(x, "Viewport", {
            "Minimum X": 0.0, "Minimum Y": 0.0, "Maximum X": 360.0, "Maximum Y": 360.0,
            "Minimum Z": 0.0, "Maximum Z": 1.0, "Scissor Minimum X": 0, "Scissor Minimum Y": 0,
            "Scissor Maximum X": 359, "Scissor Maximum Y": 359}, 32),
        "tiler_heap": pack(x, "Tiler Heap", {
            "Size": 0x100000, "Base": 0x0000_0003_c010_0000, "Bottom": 0x0000_0003_c010_0000,
            "Top": 0x0000_0003_c020_0000}, 32),
        "tiler_context": pack(x, "Tiler Context", {
            "Hierarchy Mask": 0x28, "FB Width": 360, "FB Height": 360, "Heap": 0x0000_0003_c000_8000}),
        "attribute": pack(x, "Attribute", {"Buffer index": 3, "Offset enable": 1, "Format": 0x3ab5, "Offset": -4}, 8),
        "texture": pack(x, "Texture", {
            "Type": 2, "Dimension": 2, "Format": (64 << 12) | 20, "Width": 128, "Height": 128,
            "Swizzle": 0 | 1 << 3 | 2 << 6 | 3 << 9, "Texel ordering": 2, "Levels": 1, "Sample count": 1,
            "Surfaces": 0x0000_0000_3f40_0040, "Array size": 1, "Depth": 1}, 32),
        "surface": pack(x, "Surface With Stride", {
            "Pointer": 0x0000_0000_3f40_00c0, "Row stride": 256, "Surface stride": 32768}, 16),
        "sampler": pack(x, "Sampler", {
            "Type": 1, "Wrap Mode R": 9, "Wrap Mode T": 9, "Wrap Mode S": 9, "Round to nearest even": 1,
            "Seamless Cube Map": 1, "Normalized Coordinates": 1, "Clamp integer array indices": 1,
            "Minify nearest": 1, "Magnify nearest": 1, "Mipmap Mode": 1, "Maximum anisotropy": 1}, 32),
        "renderer_state_fs_tex": pack(x, "Renderer State", dict(ms, **{
            "Shader": 0x0000_0009_8765_4300, "Sampler count": 1, "Texture count": 1, "Attribute count": 0,
            "Varying count": 1, "Uniform buffer count": 0, "Shader register allocation": 2,
            "Allow forward pixel to be killed": 1, "Pixel kill operation": 2, "ZS update operation": 2,
            "Coverage": 1, "Uniform count": 2}), 64),
        "attribute_buffer": pack(x, "Attribute Buffer", {
            "Type": 1, "Pointer": 0x0000_0003_c012_3400, "Stride": 16, "Size": 4800, "Divisor": 1}, 16),
    }


def main():
    x = load(sys.argv[1])
    lines = ["/* made by v7pack.py from Mesa's v7.xml: do not edit */"]
    for k, v in vectors(x).items():
        lines.append("static const uint8_t golden_%s[%d] = { %s };" % (k, len(v), ", ".join("0x%02x" % b for b in v)))
    path = __file__.replace("v7pack.py", "mali_desc_golden.h")
    open(path, "w").write("\n".join(lines) + "\n")
    print("wrote", path)


main()
