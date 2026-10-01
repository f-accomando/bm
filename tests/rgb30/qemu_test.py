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
sys.path.insert(0, os.path.join(HERE, "..", "..", "scripts"))
import mksd  # noqa: E402

QEMU = os.environ.get("QEMU64", "qemu-system-aarch64")
SCREEN = 512


RAMDISK = 0x50000000    # src/rgb30/sd_virt.c


class Qemu:
    def __init__(self, image, machine="virt,gic-version=3", extra=(), sd=None):
        """sd: an SD card image (scripts/mksd.py), loaded as a RAM disk."""
        self.tmp = tempfile.mkdtemp(prefix="bm64-")
        if sd:
            extra = (*extra, "-device", f"loader,file={sd},addr={RAMDISK:#x},force-raw=on")
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

    def monitor(self, cmd, until=None, timeout=10):
        """until: a function; the connection stays open until it returns
        true (the monitor may drop a command whose connection closed)."""
        with socket.socket(socket.AF_UNIX) as s:
            s.connect(self.mon_path)
            s.sendall(cmd.encode() + b"\n")
            deadline = time.time() + timeout
            time.sleep(0.5)
            while until and not until() and time.time() < deadline:
                time.sleep(0.1)

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


def screen_text_2x(img):
    """The menu's double-size text (16x32 cells at multiples of 16): each
    cell halved and read with the 8x16 font; both vertical phases."""
    w, h, px = img
    lines = []
    for yoff in (0, 16):
        rows = (h - yoff) // 32
        half = bytearray()
        for y in range(rows * 16):
            for x in range(w // 2):
                i = ((yoff + y * 2) * w + x * 2) * 3
                half += px[i:i + 3]
        lines += screen_text((w // 2, rows * 16, bytes(half)))
    return lines


def screen_all(img):
    return "\n".join(screen_text(img) + screen_text_2x(img))


def boot(q):
    """Boot ends in the menu."""
    out = q.expect("ready", timeout=30)
    out += q.expect(" games", timeout=10)
    return out.decode(errors="replace")


def lua_prompt(q):
    q.send("`")
    q.expect("\n> ", timeout=5)


def make_sd(tmp, files):
    """files: {"bm/name": bytes}; returns the image path."""
    src = []
    for i, (dest, data) in enumerate(files.items()):
        path = os.path.join(tmp, f"f{i}")
        with open(path, "wb") as f:
            f.write(data)
        src.append((path, dest))
    out = os.path.join(tmp, "sd.img")
    mksd.build(out, src, size_mib=64)
    return out


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
        time.sleep(0.5)
        img = q.screendump()
        assert img[0] == SCREEN and img[1] == SCREEN, f"screen {img[0]}x{img[1]}"
        text = screen_all(img)
        for needle in ("home", "Input test", "System", "Boot log"):
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
        lua_prompt(q)
        q.send("print(6*7, math.pi > 3, ('x'):rep(3))\r")
        out = q.expect("\n> ", timeout=5).decode(errors="replace")
        assert "42\ttrue\txxx" in out, out
        q.send("error('boom')\r")
        out = q.expect("\n> ", timeout=5).decode(errors="replace")
        assert "boom" in out, out
        q.send("print(collectgarbage('count') > 0)\r")
        out = q.expect("\n> ", timeout=5).decode(errors="replace")
        assert "true" in out, out
        q.send("exit()\r")
        q.expect("back to the menu", timeout=5)
    finally:
        q.close()


def test_menu_games_and_hidden_bm(b, opts):
    """.s16 files in bm/ are the games; .bm cartridges stay hidden unless
    show_bm=1 in bm/config.txt."""
    tmp = tempfile.mkdtemp(prefix="bm64sd-")
    sd = make_sd(tmp, {"bm/racer.s16": b"S16" + bytes(100), "bm/pong.bm": b"BMCART" + bytes(64),
                       "bm/notes.txt": b"hello"})
    q = Qemu(os.path.join(b, "kernel.elf"), sd=sd)
    try:
        out = boot(q)
        assert "SD: FAT32" in out and "ramdisk" in out, out
        assert "cartridge menu: 1 games" in out, out
        time.sleep(0.5)
        text = screen_all(q.screendump())
        assert "racer.s16" in text.lower(), text
        assert "pong" not in text.lower(), text
        assert "Input test" in text and "Bluetooth" in text and "WiFi" in text, text
    finally:
        q.close()
    sd = make_sd(tmp, {"bm/racer.s16": b"S16" + bytes(100), "bm/pong.bm": b"BMCART" + bytes(64),
                       "bm/config.txt": b"show_bm=1\n"})
    q = Qemu(os.path.join(b, "kernel.elf"), sd=sd)
    try:
        out = boot(q)
        assert "cartridge menu: 2 games" in out, out
        time.sleep(0.5)
        text = screen_all(q.screendump())
        assert "pong.bm" in text.lower(), text
    finally:
        q.close()


def test_bootlog_on_sd(b, opts):
    """The boot log goes to bm/bootlog.txt on the SD card (read back from
    the RAM disk with mtools: the FAT stays valid)."""
    tmp = tempfile.mkdtemp(prefix="bm64sd-")
    sd = make_sd(tmp, {"bm/racer.s16": b"S16" + bytes(100)})
    size = os.path.getsize(sd)
    q = Qemu(os.path.join(b, "kernel.elf"), sd=sd)
    try:
        out = boot(q)
        assert "boot log saved to bm/bootlog.txt" in out, out
        dump = os.path.join(tmp, "after.img")
        q.monitor(f'pmemsave {RAMDISK:#x} {size} "{dump}"',
                  until=lambda: os.path.exists(dump) and os.path.getsize(dump) == size)
        time.sleep(0.3)
    finally:
        q.close()
    env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
    part = f"{dump}@@{1024 * 1024}"
    log = subprocess.run(["mtype", "-i", part, "::/bm/bootlog.txt"], capture_output=True,
                         env=env, check=True).stdout.decode(errors="replace")
    assert "QEMU virt (AArch64)" in log and "2^10 = 1024.0" in log, log
    files = subprocess.run(["mdir", "-i", part, "-b", "::/bm"], capture_output=True,
                           env=env, check=True).stdout.decode()
    assert "racer.s16" in files.lower(), files


def test_bluetooth_page_without_chip(b, opts):
    """The Bluetooth page starts the stack; QEMU has no chip: it says so and
    B goes back to the menu."""
    q = Qemu(os.path.join(b, "kernel.elf"))
    try:
        boot(q)
        for k in "ss":                          # Input test, System, Bluetooth
            q.send(k)
            time.sleep(0.25)
        q.send("\r")
        q.expect("no Bluetooth controller in QEMU", timeout=10)
        time.sleep(0.5)
        text = "\n".join(screen_text(q.screendump()))
        assert "Bluetooth is off" in text, text
        q.send("\x7f")                          # B
        time.sleep(0.6)
        text = screen_all(q.screendump())
        assert "Up/Down: choose" in text, text
    finally:
        q.close()


def test_wifi_page_without_chip(b, opts):
    """The WiFi page starts the chip; QEMU has none: it says so, B goes back."""
    q = Qemu(os.path.join(b, "kernel.elf"))
    try:
        boot(q)
        for k in "sss":                         # Input test, System, Bluetooth, WiFi
            q.send(k)
            time.sleep(0.25)
        q.send("\r")
        q.expect("no WiFi chip in QEMU", timeout=10)
        time.sleep(0.5)
        text = "\n".join(screen_text(q.screendump()))
        assert "WiFi is off" in text, text
        q.send("\x7f")
        time.sleep(0.6)
        assert "Up/Down: choose" in screen_all(q.screendump())
    finally:
        q.close()


def test_menu_input_page(b, opts):
    """Down to the input test, A opens it, the serial port presses buttons."""
    q = Qemu(os.path.join(b, "kernel.elf"))
    try:
        boot(q)
        q.send("\r")                           # A on the first entry: Input test
        time.sleep(0.4)
        q.send("x")                            # X held for a moment
        time.sleep(0.05)
        img = q.screendump()
        text = screen_all(img)
        assert "Input test" in text, text
        assert "held: 00000040" in text, text  # PAD_X
        q.send("\t ")                          # Select + Start: back
        time.sleep(0.6)
        text = screen_all(q.screendump())
        assert "Up/Down: choose" in text, text
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
