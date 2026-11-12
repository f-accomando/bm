#!/usr/bin/env python3
"""
asm_rom_test.py - the assembler against the Python generator of tests/snes/make_testrom.py, and the core.

  python3 tests/snes/asm_rom_test.py build/host/luahost build/host/snes_asm_run

1. make_testrom.build() writes the reference ROM.
2. The data tables of that ROM go to data.bin; consts.inc names their addresses and sizes.
3. luahost runs tests/snes/asm_cli.lua on tests/snes/testrom.s (the same program in 65C816 assembly).
4. The code and data area ($0000-$7FBF of the ROM) must be equal byte for byte; so must the header fields and the
   vectors that the generator sets (the builder puts an RTI at $FFE0 and points the unused slots there).
5. The ROM runs in the core for 30 frames (snes_asm_run).
Run it from the repository root.
"""
import os, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import make_testrom


def main():
    luahost, runner = sys.argv[1:3]
    ref = make_testrom.build()
    # the data tables: the same offsets as in make_testrom.build()
    pal_n, tiles_n, tmap_n, spr_n, oam_n = 512, 16 * 5, 2048, 32, 544
    d0 = make_testrom.DATA - 0x8000
    pos = d0
    names = [("pal", 512), ("tiles", tiles_n), ("tmap", tmap_n), ("spr", spr_n), ("oam", oam_n)]
    with tempfile.TemporaryDirectory() as td:
        consts = []
        for name, n in names:
            consts.append("%s = $%X" % (name, 0x8000 + pos))
            if name != "pal":
                consts.append("%s_len = %d" % (name, n))
            pos += n
        consts.append("hdma = $%X" % (0x8000 + pos))
        data_end = pos + 14 * 3 + 1
        open(os.path.join(td, "consts.inc"), "w").write("\n".join(consts) + "\n")
        open(os.path.join(td, "data.bin"), "wb").write(ref[d0:data_end])
        src = os.path.join(HERE, "testrom.s")
        out = os.path.join(td, "out.sfc")
        os.makedirs(os.path.join(td, "x"), exist_ok=True)
        # the source includes consts.inc and data.bin: they sit next to a copy of the source
        open(os.path.join(td, "testrom.s"), "wb").write(open(src, "rb").read())
        r = subprocess.run([luahost, os.path.join(HERE, "asm_cli.lua"), os.path.join(td, "testrom.s"), out,
                            "--title", "BM SNES TEST ROM", "--dev", "51", "--sym", os.path.join(td, "t.sym")],
                           capture_output=True, text=True)
        if r.returncode:
            print("assembler failed:\n" + r.stdout + r.stderr); return 1
        mine = open(out, "rb").read()
        fails = 0

        def check(name, ok):
            nonlocal fails
            if not ok:
                fails += 1; print("FAIL", name)
        check("the size is 32 KB", len(mine) == len(ref) == 0x8000)
        check("code and data are equal ($0000-$7FBF)", mine[:0x7FC0] == ref[:0x7FC0])
        if mine[:0x7FC0] != ref[:0x7FC0]:
            for i in range(0x7FC0):
                if mine[i] != ref[i]:
                    print("  first difference at $%04X: %02X (assembler) %02X (generator)" % (i, mine[i], ref[i])); break
        check("the header fields are equal ($FFC0-$FFDB)", mine[0x7FC0:0x7FDC] == ref[0x7FC0:0x7FDC])
        for name, a, b in (("native vectors", 0x7FE4, 0x7FEC), ("native IRQ", 0x7FEE, 0x7FF0),
                           ("emulation COP", 0x7FF4, 0x7FF6), ("emulation ABORT NMI RESET IRQ", 0x7FF8, 0x8000)):
            check(name, mine[a:b] == ref[a:b])
        s = sum(mine) & 0xFFFF
        cs = mine[0x7FDE] | (mine[0x7FDF] << 8)
        cc = mine[0x7FDC] | (mine[0x7FDD] << 8)
        check("the checksum is the sum of the ROM", cs == s and cc == (s ^ 0xFFFF))
        sym = open(os.path.join(td, "t.sym")).read()
        check("the symbol table lists reset and nmi", "$00:8000 reset" in sym and "$00:8200 nmi" in sym)
        rr = subprocess.run([runner, out, "30"], capture_output=True, text=True)
        print(rr.stdout.strip())
        check("the ROM runs in the core", rr.returncode == 0)
    print("asm_rom_test: ok" if fails == 0 else "asm_rom_test: %d failed" % fails)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
