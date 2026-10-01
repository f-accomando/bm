#!/usr/bin/env python3
"""
Tests of the RGB30 kernel (AArch64) in QEMU's virt machine: the build with
PLAT=virt (PL011 serial port, ramfb screen, GICv3, generic timer).

  tests/rgb30/qemu_test.py [--build build/rgb30-virt] [-k name]
"""
import argparse
import os
import socket
import subprocess
import sys
import tempfile
import time
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools"))
from qemu_test import read_ppm, screen_text, pixel, free_port  # noqa: E402
import bm_load  # noqa: E402

QEMU = os.environ.get("QEMU64", "qemu-system-aarch64")
SCREEN = 512


class Qemu:
    def __init__(self, image, machine="virt,gic-version=3", extra=()):
        self.tmp = tempfile.mkdtemp(prefix="bm64-")
        self.mon_path = os.path.join(self.tmp, "mon.sock")
        tcp = free_port()
        self.proc = subprocess.Popen(
            [QEMU, "-M", machine, "-cpu", "cortex-a55", "-m", "512M",
             "-device", "ramfb", "-nic", "none", "-display", "none",
             "-kernel", image,
             "-serial", f"tcp:127.0.0.1:{tcp},server=on,wait=on",
             "-monitor", f"unix:{self.mon_path},server=on,wait=off", *extra],
            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        self.port = bm_load.Port(f"tcp:127.0.0.1:{tcp}", 115200)
        self.buf = b""

    def expect(self, needle, timeout=10.0):
        if isinstance(needle, str):
            needle = needle.encode()
        deadline = time.time() + timeout
        while needle not in self.buf:
            if time.time() > deadline:
                raise AssertionError(
                    f"timeout waiting for {needle!r}; serial output:\n"
                    + self.buf.decode(errors="replace"))
            self.buf += self.port.read(0.1)
        i = self.buf.index(needle) + len(needle)
        seen, self.buf = self.buf[:i], self.buf[i:]
        return seen

    def send(self, data):
        self.port.write(data.encode() if isinstance(data, str) else data)

    def monitor(self, cmd):
        with socket.socket(socket.AF_UNIX) as s:
            s.connect(self.mon_path)
            s.sendall(cmd.encode() + b"\n")
            time.sleep(0.5)

    def screendump(self):
        path = os.path.join(self.tmp, "screen.ppm")
        if os.path.exists(path):
            os.remove(path)
        self.monitor(f"screendump {path}")
        for _ in range(50):
            if os.path.exists(path) and os.path.getsize(path) > 0:
                break
            time.sleep(0.1)
        return read_ppm(path)

    def close(self):
        self.proc.kill()
        self.proc.wait()


def boot(q):
    out = q.expect("ready", timeout=30)
    q.expect("> ")
    return out.decode(errors="replace")


def test_boot_banner(b, opts):
    q = Qemu(os.path.join(b, "kernel.elf"))
    try:
        out = boot(q)
        for needle in ("kernel", "QEMU virt (AArch64)", "Cortex-A55", "EL1",
                       "ramfb 512x512 32 bpp", "IRQ on: timer 1000 Hz",
                       "2^10 = 1024.0", "fib(25) = 75025"):
            assert needle in out, f"{needle!r} missing:\n{out}"
        hz = int(out.split("measured ")[1].split(" Hz")[0])
        assert 950 <= hz <= 1050, f"tick measured {hz} Hz"
    finally:
        q.close()


def test_screen_console(b, opts):
    q = Qemu(os.path.join(b, "kernel.elf"))
    try:
        boot(q)
        img = q.screendump()
        assert img[0] == SCREEN and img[1] == SCREEN, f"screen {img[0]}x{img[1]}"
        text = "\n".join(screen_text(img))
        for needle in ("bm kernel", "Lua 5.4, 2^10 = 1024.0", "ready"):
            assert needle in text, f"{needle!r} not on screen:\n{text}"
    finally:
        q.close()


def test_el2_image_relocation(b, opts):
    """kernel8.img as U-Boot's booti would start it: EL2, x0 = device tree,
    loaded below the link address (start.S moves it and drops to EL1)."""
    q = Qemu(os.path.join(b, "kernel8.img"), machine="virt,gic-version=3,virtualization=on")
    try:
        out = boot(q)
        assert "EL1" in out and "2^10 = 1024.0" in out, out
        dtb = out.split("device tree at ")[1].split()[0]
        assert int(dtb, 16) != 0, f"no device tree pointer: {dtb}"
    finally:
        q.close()


def test_lua_repl(b, opts):
    q = Qemu(os.path.join(b, "kernel.elf"))
    try:
        boot(q)
        q.send("print(6*7, math.pi > 3, ('x'):rep(3))\r")
        out = q.expect("\n> ", timeout=5).decode(errors="replace")
        assert "42\ttrue\txxx" in out, out
        q.send("error('boom')\r")
        out = q.expect("\n> ", timeout=5).decode(errors="replace")
        assert "boom" in out, out
        q.send("print(collectgarbage('count') > 0)\r")
        out = q.expect("\n> ", timeout=5).decode(errors="replace")
        assert "true" in out, out
    finally:
        q.close()


TESTS = [v for k, v in sorted(globals().items()) if k.startswith("test_")]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build", default="build/rgb30-virt")
    ap.add_argument("-k", default="")
    opts = ap.parse_args()
    failed = 0
    for t in TESTS:
        if opts.k and opts.k not in t.__name__:
            continue
        t0 = time.time()
        try:
            t(opts.build, opts)
            print(f"PASS {t.__name__} ({time.time() - t0:.1f}s)")
        except Exception:
            failed += 1
            print(f"FAIL {t.__name__}")
            traceback.print_exc()
    print(f"{len(TESTS) - failed if not opts.k else ''} passed, {failed} failed")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
