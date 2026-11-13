#!/usr/bin/env python3
"""
m7test_arm.py - the Mode 7 test ROM (tests/snes/m7test/main.s) on the host and on an ARM1176 in QEMU: for each of the five variants, the
pictures of frames 40, 41 and 60 must be equal (md5 of the PPM) on both, and the three pictures of a variant must differ from each other.
  python3 tests/snes/m7test_arm.py build/m7test.sfc build/host/snes_game_run build/arm/m7test.elf
The ELF is tests/snes/game_run_arm.c built with GR_ROM "rom.bin", GR_FW "-", GR_FRAMES "70", GR_SCRIPT "s.txt", GR_OUT "p" (make test-snes-m7rom-arm).
"""
import hashlib, os, shutil, subprocess, sys, tempfile

SHOTS = [40, 41, 60]
KEYS = {1: "8000", 2: "4000", 3: "0080", 4: "0040", 5: "0020"}   # pad B, Y, A, X, L; the ROM builds its data in forced blank, so hold the key early

def run(exe, rom, variant, qemu):
    tmp = tempfile.mkdtemp()
    try:
        open(os.path.join(tmp, "s.txt"), "w").write("0 26 %s\n" % KEYS[variant] + "".join("shot %d\n" % f for f in SHOTS))
        if qemu:
            shutil.copy(rom, os.path.join(tmp, "rom.bin"))
            subprocess.run(["qemu-system-arm", "-M", "versatilepb", "-cpu", "arm1176", "-m", "128", "-semihosting", "-nographic", "-audio", "none",
                            "-kernel", os.path.abspath(exe)], cwd=tmp, stdout=subprocess.DEVNULL, check=True)
        else:
            subprocess.run([os.path.abspath(exe), os.path.abspath(rom), "-", "70", "s.txt", "p"], cwd=tmp, stdout=subprocess.DEVNULL, check=True)
        return [hashlib.md5(open(os.path.join(tmp, "p%d.ppm" % f), "rb").read()).hexdigest() for f in SHOTS]
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

def main():
    rom, host, elf = sys.argv[1:4]
    bad = 0
    for v in sorted(KEYS):
        a, b = run(host, rom, v, False), run(elf, rom, v, True)
        ok = a == b and len(set(a)) == len(SHOTS)
        bad += not ok
        print("variant %d: host and ARM %s, %d different pictures" % (v, "equal" if a == b else "DIFFER", len(set(a))))
    print("m7test_arm: %s" % ("FAILED" if bad else "ok"))
    return 1 if bad else 0

if __name__ == "__main__":
    sys.exit(main())
