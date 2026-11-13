#!/usr/bin/env python3
"""
smw2_frames.py - Yoshi's Island (Super FX GSU-2) frame hashes: a game run of 4000 frames with a scripted pad (START and A every 50 frames from frame 600),
with the picture hashed every 250 frames (Nintendo logo, title with the island, the story, the first level). The hashes are in tests/snes/smw2_frames.txt.
A change of the CPU, GSU, PPU or IRQ code that changes one picture fails the test: look at the picture before you update the file.
  BM_SNES_ROMS=/folder python3 tests/snes/smw2_frames.py build/host/snes_game_run [--update]
With --qemu the argument is an ELF of tests/snes/game_run_arm.c (GR_ROM "rom.bin", GR_FW "-", GR_FRAMES "4000", GR_SCRIPT "s.txt", GR_OUT "p") that runs in qemu-system-arm:
the CPU, GSU and PPU assembly loops give the same hashes (make test-snes-smw2-arm).
Without the ROM (smw2.smc or smw2.sfc in the folder of BM_SNES_ROMS) it prints "skipped" and exits with 0. No ROM byte is kept: the pictures go to a temporary folder.
"""
import hashlib, os, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
EXPECT = os.path.join(HERE, "smw2_frames.txt")
FRAMES = 4000
SHOTS = list(range(250, FRAMES, 250)) + [FRAMES - 1]

def script():
    out = []
    for f in range(600, FRAMES, 50):
        out.append("%d %d %s" % (f, f + 3, "1000" if (f // 50) % 2 else "0080"))
    out += ["shot %d" % f for f in SHOTS]
    return "\n".join(out) + "\n"

def find_rom():
    d = os.environ.get("BM_SNES_ROMS", "")
    for n in ("smw2.smc", "smw2.sfc"):
        p = os.path.join(d, n)
        if d and os.path.exists(p):
            return p
    return None

def main():
    exe = sys.argv[1]
    rom = find_rom()
    if not rom:
        print("smw2_frames: skipped (set BM_SNES_ROMS to a folder with smw2.smc; no ROM is in the repository)")
        return 0
    tmp = tempfile.mkdtemp()
    try:
        open(os.path.join(tmp, "s.txt"), "w").write(script())
        if "--qemu" in sys.argv:
            shutil.copy(rom, os.path.join(tmp, "rom.bin"))
            subprocess.run(["qemu-system-arm", "-M", "versatilepb", "-cpu", "arm1176", "-m", "128", "-semihosting", "-nographic", "-audio", "none", "-kernel", os.path.abspath(exe)],
                           cwd=tmp, stdout=subprocess.DEVNULL, check=True)
        else:
            subprocess.run([os.path.abspath(exe), os.path.abspath(rom), "-", str(FRAMES), "s.txt", "p"], cwd=tmp, stdout=subprocess.DEVNULL, check=True)
        got = {f: hashlib.md5(open(os.path.join(tmp, "p%d.ppm" % f), "rb").read()).hexdigest() for f in SHOTS}
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    if "--update" in sys.argv:
        open(EXPECT, "w").write("".join("%d %s\n" % (f, h) for f, h in sorted(got.items())))
        print("smw2_frames: updated", EXPECT)
        return 0
    want = {int(a): b for a, b in (l.split() for l in open(EXPECT))}
    bad = [f for f in SHOTS if got[f] != want.get(f)]
    print("smw2_frames: %d pictures, %d different%s" % (len(SHOTS), len(bad), (" (frames %s)" % bad) if bad else ""))
    return 1 if bad else 0

if __name__ == "__main__":
    sys.exit(main())
