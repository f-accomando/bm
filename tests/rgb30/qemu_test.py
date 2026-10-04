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
SCREEN = 360           # the menu (made as big as the 720x720 panel on the console)


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


def keys(q, ks, pause=0.3):
    """Buttons from the serial port, one at a time (each is held 120 ms)."""
    for k in ks:
        q.send(k)
        time.sleep(pause)


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
                       "ramfb 360x360 32 bpp", "IRQ on: timer 1000 Hz",
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
        for needle in ("Games", "Dev", "System", "No games yet", "L1/R1: tab   Up/Down: choose"):
            assert needle in text, f"{needle!r} not on screen:\n{text}"
        keys(q, "r")                            # R1: the Dev tab
        text = screen_all(q.screendump())
        for needle in ("3D Bench", "Render bench", "Display", "Input test", "Boot log"):
            assert needle in text, f"{needle!r} not on the Dev tab:\n{text}"
        keys(q, "r")                            # the System tab
        text = screen_all(q.screendump())
        for needle in ("Bluetooth", "WiFi", "System", "Reboot", "Power off"):
            assert needle in text, f"{needle!r} not on the System tab:\n{text}"
        keys(q, "r")                            # back round to Games
        assert "No games yet" in screen_all(q.screendump())
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
    """.s16 files in bm/ are the games; the .bm cartridges are listed too,
    for testing, unless show_bm=0 in bm/config.txt (then the Games tab says
    how many are hidden)."""
    tmp = tempfile.mkdtemp(prefix="bm64sd-")
    sd = make_sd(tmp, {"bm/racer.s16": b"S16" + bytes(100), "bm/pong.bm": b"BMCART" + bytes(64),
                       "bm/notes.txt": b"hello", "bm/config.txt": b"show_bm=0\n"})
    q = Qemu(os.path.join(b, "kernel.elf"), sd=sd)
    try:
        out = boot(q)
        assert "SD: FAT32" in out and "ramdisk" in out, out
        assert "cartridge menu: 1 games" in out, out
        time.sleep(0.5)
        text = screen_all(q.screendump())
        assert "racer.s16" in text.lower(), text
        assert "pong" not in text.lower(), text
        out = q.expect("\n", timeout=5).decode(errors="replace")
        assert "(1 .bm hidden by show_bm=0" in out, out
        keys(q, "l")                            # L1: round to the System tab
        text = screen_all(q.screendump())
        assert "Bluetooth" in text and "WiFi" in text and "racer" not in text.lower(), text
    finally:
        q.close()
    sd = make_sd(tmp, {"bm/pong.bm": b"BMCART" + bytes(64), "bm/config.txt": b"show_bm=0\n"})
    q = Qemu(os.path.join(b, "kernel.elf"), sd=sd)
    try:
        boot(q)
        time.sleep(0.5)
        text = screen_all(q.screendump())
        assert "No games yet" in text and "1 Pi cartridge (.bm) hidden by show_bm=0" in text, text
    finally:
        q.close()
    sd = make_sd(tmp, {"bm/racer.s16": b"S16" + bytes(100), "bm/pong.bm": b"BMCART" + bytes(64)})
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
    # the SD card is read before the display starts (a display hang still
    # leaves the log), and the log is rewritten up to "ready"
    assert log.index("SD: ") < log.index("display: starting") < log.index("ready"), log
    files = subprocess.run(["mdir", "-i", part, "-b", "::/bm"], capture_output=True,
                           env=env, check=True).stdout.decode()
    assert "racer.s16" in files.lower(), files


def test_update_from_sd(b, opts):
    """System > Updates, as on the Pi (src/kernel/update.c): a release copied
    on the card (update_url=sd:/release/), signed with a key the card adds
    (bm/release.pem); the RGB30's manifest (manifest-rgb30: kernel8.img,
    bm/ca.pem), what changes, then A installs: the old kernel kept in
    bm/backup, the new one written last (read back from the RAM disk with
    mtools before the restart, which reloads it), and the console restarts."""
    tmp = tempfile.mkdtemp(prefix="bm64up-")
    key, pub = os.path.join(tmp, "key.pem"), os.path.join(tmp, "pub.pem")
    subprocess.run(["openssl", "genpkey", "-algorithm", "EC", "-pkeyopt", "ec_paramgen_curve:P-256",
                    "-out", key], check=True, capture_output=True)
    subprocess.run(["openssl", "pkey", "-in", key, "-pubout", "-out", pub], check=True, capture_output=True)
    new_kernel = os.path.join(b, "kernel8.img")             # an arm64 Image, "ARM\x64" at +56
    ca = os.path.join(HERE, "..", "..", "boot", "ca.pem")
    rel = os.path.join(tmp, "release")
    subprocess.run([sys.executable, os.path.join(HERE, "..", "..", "scripts", "mkrelease.py"), rel,
                    "--manifest", "manifest-rgb30", "--version", "v9.9.9", "--commit", "abc1234",
                    "--key", key, "--pub", pub, "--file", f"{new_kernel}:/kernel8.img",
                    "--file", f"{ca}:/bm/ca.pem"], check=True, capture_output=True)
    old_kernel = bytes(56) + b"ARM\x64" + bytes(4096)
    files = {"kernel8.img": old_kernel, "bm/ca.pem": b"old certificates\n",
             "bm/config.txt": b"update_url=sd:/release/\n", "bm/release.pem": open(pub, "rb").read()}
    for n in os.listdir(rel):
        files["release/" + n] = open(os.path.join(rel, n), "rb").read()
    sd = make_sd(tmp, files)
    size = os.path.getsize(sd)
    q = Qemu(os.path.join(b, "kernel.elf"), sd=sd)
    try:
        boot(q)
        keys(q, "rrss")                         # System tab: Bluetooth, WiFi, Updates
        q.send("\r")
        out = q.expect("can be installed", timeout=30).decode(errors="replace")
        for want in ("releases: sd:/release/", "v9.9.9", "commit abc1234, signed: good"):
            assert want in out, out
        # a build of the sources, or a later commit than a tag (git describe)
        assert "newer than this kernel" in out or "a build of the sources" in out, out
        lines = {l.split()[0]: l for l in out.splitlines() if l.strip().startswith("/")}
        assert lines["/kernel8.img"].endswith("changed") and lines["/bm/ca.pem"].endswith("changed"), lines
        q.expect("install v9.9.9 and restart", timeout=10)
        time.sleep(0.3)
        q.send("\r")                           # confirm: install
        out = q.expect("installed: restarting", timeout=60).decode(errors="replace")
        dump = os.path.join(tmp, "after.img")
        q.monitor(f'pmemsave {RAMDISK:#x} {size} "{dump}"',
                  until=lambda: os.path.exists(dump) and os.path.getsize(dump) == size)
        for want in ("all 2 files downloaded and checked", "/kernel8.img kept in /bm/backup",
                     "written /bm/ca.pem", "written /kernel8.img"):
            assert want in out, out
        assert out.index("written /bm/ca.pem") < out.index("written /kernel8.img"), out
        q.expect("ready", timeout=30)           # restarted
    finally:
        q.close()
    env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
    part = f"{dump}@@{1024 * 1024}"

    def read(path):
        r = subprocess.run(["mtype", "-i", part, "::" + path], capture_output=True, env=env)
        return r.stdout if r.returncode == 0 else None
    assert read("/kernel8.img") == open(new_kernel, "rb").read(), "the new kernel8.img"
    assert read("/bm/backup/kernel8.img") == old_kernel, "the old one kept"
    assert read("/bm/ca.pem") == open(ca, "rb").read(), "the certificates"


def test_bluetooth_page_without_chip(b, opts):
    """The Bluetooth page starts the stack; QEMU has no chip: it says so and
    B goes back to the menu."""
    q = Qemu(os.path.join(b, "kernel.elf"))
    try:
        boot(q)
        keys(q, "rr")                           # System tab: Bluetooth
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
        keys(q, "rrs")                          # System tab: Bluetooth, WiFi
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


def test_display_modes(b, opts):
    """The Display page: the modes a game or the GPU can use, each with its
    test image (QEMU shows the image 1:1: the screen takes its size), the
    GPU's layout (rows of 64 bytes); B goes back to the menu's 360x360."""
    q = Qemu(os.path.join(b, "kernel.elf"))
    try:
        boot(q)
        keys(q, "rss")                          # Dev tab: 3D Bench, Render bench, Display
        q.send("\r")
        q.expect("720x720 x1 sharp: on", timeout=10)
        time.sleep(0.5)
        img = q.screendump()
        assert img[0] == 720 and img[1] == 720, f"screen {img[0]}x{img[1]}"
        text = "\n".join(screen_text(img))
        assert "720x720 x1 sharp" in text and "pitch 2880, 2 pages" in text, text
        assert "at 3c000000" in text or "at 5e000000" in text, text
        q.send("\r")                            # A: the next mode
        q.expect("360x360 x2 sharp: on", timeout=10)
        time.sleep(0.5)
        img = q.screendump()
        assert img[0] == 360 and img[1] == 360, f"screen {img[0]}x{img[1]}"
        text = "\n".join(screen_text(img))
        assert "360x360 x2 sharp" in text and "pitch 1472" in text, text   # 1440 -> 64-byte rows
        q.send("\x7f")                          # B: back
        time.sleep(0.8)
        img = q.screendump()
        assert img[0] == SCREEN and img[1] == SCREEN, f"screen {img[0]}x{img[1]}"
        assert "Up/Down: choose" in screen_all(img)
    finally:
        q.close()


def test_confirm_button(b, opts):
    """B (the RGB30's lower face button) confirms and A goes back by
    default: the menu says so and the serial port's Enter presses B;
    confirm=a in bm/config.txt swaps them."""
    for config, ok, held in ((b"layout=us\n", "B", "00000020"), (b"confirm=a\n", "A", "00000010")):
        tmp = tempfile.mkdtemp(prefix="bm64sd-")
        sd = make_sd(tmp, {"bm/config.txt": config})     # no games: Dev is one tab away
        q = Qemu(os.path.join(b, "kernel.elf"), sd=sd)
        try:
            boot(q)
            time.sleep(0.4)
            text = screen_all(q.screendump())
            assert f"Up/Down: choose   {ok}: open" in text, text
            keys(q, "rsss")                    # Dev tab: Input test
            q.send("\r")                       # confirm
            time.sleep(0.6)
            q.send("\r")                       # Enter: the confirm button, held
            time.sleep(0.05)
            text = screen_all(q.screendump())
            assert "Input test" in text and f"held: {held}" in text, text
        finally:
            q.close()


def test_bm_cartridge(b, opts):
    """A Pi cartridge on the RGB30 (listed with no config): Yharnam, 256x256 RGB565,
    runs on the AArch64 kernel (QEMU shows its screen 1:1, widened to
    32 bits); the title, then the game after A; 'q' leaves and the menu's
    360x360 comes back."""
    cart = os.path.join(b, "carts", "yharnam.bm")
    if not os.path.exists(cart):
        raise AssertionError(f"{cart} missing (make TARGET=rgb30 test builds it)")
    tmp = tempfile.mkdtemp(prefix="bm64sd-")
    with open(cart, "rb") as f:
        sd = make_sd(tmp, {"bm/yharnam.bm": f.read()})
    q = Qemu(os.path.join(b, "kernel.elf"), sd=sd)
    try:
        boot(q)
        time.sleep(0.4)
        assert "yharnam.bm" in screen_all(q.screendump())
        q.send("\r")                            # confirm: play
        q.expect("play: /bm/yharnam.bm", timeout=10)
        time.sleep(8)
        img = q.screendump()
        assert img[0] == 256 and img[1] == 256, f"screen {img[0]}x{img[1]}"
        colours = {img[2][i:i + 3] for i in range(0, len(img[2]), 3 * 97)}
        assert len(colours) > 20, f"{len(colours)} colours: not the town"
        q.send(" ")                              # the game's A: start
        time.sleep(3)
        q.send("q")
        out = q.expect("fps", timeout=10).decode(errors="replace")
        frames = int(out.split('"Yharnam" ')[1].split()[0])
        assert frames > 100, out
        time.sleep(1)
        img = q.screendump()
        assert img[0] == SCREEN and img[1] == SCREEN, f"screen {img[0]}x{img[1]}"
        assert "Up/Down: choose" in screen_all(img)
    finally:
        q.close()


def test_bench3d(b, opts):
    """The Dev tab's 3D Bench (src/bm/b3d.c) at 640x360: every test on the
    ARM, the report on the serial port and in bm/bench on the SD card;
    the back button returns to the menu's 360x360."""
    tmp = tempfile.mkdtemp(prefix="bm64sd-")
    sd = make_sd(tmp, {"bm/config.txt": b"layout=us\n"})
    size = os.path.getsize(sd)
    dump = os.path.join(tmp, "after.img")
    q = Qemu(os.path.join(b, "kernel.elf"), sd=sd)
    try:
        boot(q)
        keys(q, "r")                            # Dev tab: 3D Bench
        q.send("\r")
        q.expect("b3d spheres ARM n=1 ", timeout=30)
        img = q.screendump()
        assert img[0] == 640 and img[1] == 360, f"screen {img[0]}x{img[1]}"
        out = q.expect("total ", timeout=600)           # the report's last line
        out += q.expect("\n", timeout=10)
        out = out.decode(errors="replace")
        for needle in ("machine QEMU virt, Cortex-A55", "counters Cortex-A55 PMU", "R,spheres,ARM",
                       "R,quad_tex,ARM"):
            assert needle in out, f"{needle!r} missing:\n{out[-3000:]}"
        time.sleep(1)
        q.send("\x7f")                          # A: back
        q.expect("3D Bench: done", timeout=20)
        time.sleep(1)
        img = q.screendump()
        assert img[0] == SCREEN and img[1] == SCREEN, f"screen {img[0]}x{img[1]}"
        assert "Up/Down: choose" in screen_all(img)
        q.monitor(f'pmemsave {RAMDISK:#x} {size} "{dump}"',
                  until=lambda: os.path.exists(dump) and os.path.getsize(dump) == size)
        time.sleep(0.3)
    finally:
        q.close()
    env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
    rep = subprocess.run(["mtype", "-i", f"{dump}@@{1024 * 1024}", "::/bm/bench/3D0001.TXT"],
                         capture_output=True, env=env, check=True).stdout.decode(errors="replace")
    assert rep.startswith("bm 3D Bench") and "R,spheres,ARM" in rep and "total " in rep, rep


def test_menu_input_page(b, opts):
    """The Dev tab's input test: the serial port presses buttons."""
    q = Qemu(os.path.join(b, "kernel.elf"))
    try:
        boot(q)
        keys(q, "rsss")                         # Dev tab: Input test
        q.send("\r")
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
