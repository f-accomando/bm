#!/usr/bin/env python3
"""
Tests of the RGB30 kernel (AArch64) in QEMU's virt machine: the build with
PLAT=virt (PL011 serial port, ramfb screen, GICv3, generic timer).

  tests/rgb30/qemu_test.py [--build build/rgb30-virt] [-k name]
"""
import argparse
import os
import re
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
import mkbm  # noqa: E402

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


def in_menu(text):
    """The menu (the Pi's menu_ui.c at 360x360): its bar of tabs."""
    return all(t in text for t in ("Games", "Dev", "Settings"))


def hint_chip(img, text_rows, label):
    """The pixels of the button's chip left of a hint's label on the footer
    (row 21): the 3 cells before it."""
    col = text_rows[21].index(label)
    w = img[0]
    return bytes(b for y in range(21 * 16, 22 * 16) for x in range(col * 8 - 24, col * 8)
                 for b in img[2][(y * w + x) * 3:(y * w + x) * 3 + 3])


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
        for needle in ("Games", "Dev", "Settings", "No games yet"):
            assert needle in text, f"{needle!r} not on screen:\n{text}"
        # the Dev tab: three square covers a row, the selected one's name on the pill
        for k, name in (("r", "3D Bench"), ("s", "Input test"), ("d", "Boot log"), ("d", "Lua"),
                        ("w", "Display"), ("s", "Lua")):
            keys(q, k)
            text = screen_all(q.screendump())
            assert name in text and in_menu(text), f"{name!r} not selected on the Dev tab:\n{text}"
        keys(q, "r")                            # Settings: its panel, the Pi's sections (settings.c)
        text = screen_all(q.screendump())
        for needle in ("Controllers", "WiFi and network", "Screen and sound", "Updates", "Reports", "System"):
            assert needle in text, f"{needle!r} not in Settings:\n{text}"
        assert "none waiting" in text, text    # no reports waiting
        keys(q, "w")                            # up from the first row: round to the last
        text = screen_all(q.screendump())
        assert "Turns the console off" in text, text    # Shut down, then Restart (2026-10-04)
        keys(q, "w")
        keys(q, "w")
        text = screen_all(q.screendump())
        assert "Version, memory, the log" in text, text
        keys(q, "\r")                          # System: its rows
        text = screen_all(q.screendump())
        assert "Settings > System" in text and "Uptime" in text and "Cortex-A55" in text, text
        keys(q, "w")                            # the last rows: the battery, the log
        text = screen_all(q.screendump())
        assert "Battery" in text and "Log since boot" in text, text
        keys(q, "\x7f")                        # back: Settings
        keys(q, "r")                            # R1 on the last tab: nothing
        assert "Version, memory, the log" in screen_all(q.screendump())
        keys(q, "\x7f")                        # back: out of Settings, to Dev
        text = screen_all(q.screendump())
        assert "Lua" in text and "Screen and sound" not in text, text
        keys(q, "l")                            # L1: Games
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
    """.b16 files in bm/ are the games; the .bm cartridges are listed too,
    for testing, unless show_bm=0 in bm/config.txt (then the Games tab says
    how many are hidden)."""
    tmp = tempfile.mkdtemp(prefix="bm64sd-")
    sd = make_sd(tmp, {"bm/racer.b16": b"B16" + bytes(100), "bm/pong.bm": b"BMCART" + bytes(64),
                       "bm/notes.txt": b"hello", "bm/config.txt": b"show_bm=0\n"})
    q = Qemu(os.path.join(b, "kernel.elf"), sd=sd)
    try:
        out = boot(q)
        assert "SD: FAT32" in out and "ramdisk" in out, out
        assert "cartridge menu: 1 games" in out, out
        time.sleep(0.5)
        text = screen_all(q.screendump())
        assert "racer.b16" in text.lower(), text
        assert "pong" not in text.lower(), text
        out = q.expect("\n", timeout=5).decode(errors="replace")
        assert "(1 .bm hidden by show_bm=0" in out, out
        keys(q, "rr")                           # R1 twice: Settings, its panel over the covers
        text = screen_all(q.screendump())
        assert "Controllers" in text and "WiFi and network" in text and "racer" not in text.lower(), text
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
    sd = make_sd(tmp, {"bm/racer.b16": b"B16" + bytes(100), "bm/pong.bm": b"BMCART" + bytes(64)})
    q = Qemu(os.path.join(b, "kernel.elf"), sd=sd)
    try:
        out = boot(q)
        assert "cartridge menu: 2 games" in out, out
        time.sleep(0.5)
        text = screen_all(q.screendump())
        keys(q, "d")                            # the other cover
        text += screen_all(q.screendump())
        assert "pong.bm" in text.lower() and "racer.b16" in text.lower(), text
    finally:
        q.close()


def test_bootlog_on_sd(b, opts):
    """The boot log goes to bm/bootlog.txt on the SD card (read back from
    the RAM disk with mtools: the FAT stays valid)."""
    tmp = tempfile.mkdtemp(prefix="bm64sd-")
    sd = make_sd(tmp, {"bm/racer.b16": b"B16" + bytes(100)})
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
    assert "racer.b16" in files.lower(), files


def test_update_from_sd(b, opts):
    """Settings > Updates, as on the Pi (src/kernel/update.c): a release copied
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
        keys(q, "rrsss")                        # Settings: Controllers, WiFi, Screen, Updates
        q.send("\r")
        time.sleep(0.5)
        keys(q, "s")                            # Check for updates
        q.send("\r")
        out = q.expect("can be installed", timeout=30).decode(errors="replace")
        for want in ("releases: sd:/release/", "v9.9.9", "commit abc1234, signed: good"):
            assert want in out, out
        # a build of the sources, or a later commit than a tag (git describe)
        assert "newer than this kernel" in out or "a build of the sources" in out, out
        lines = {l.split()[0]: l for l in out.splitlines() if l.strip().startswith("/")}
        assert lines["/kernel8.img"].endswith("changed") and lines["/bm/ca.pem"].endswith("changed"), lines
        q.expect("back to the menu", timeout=10)
        time.sleep(0.3)
        q.send("\r")                           # back to the panel: Install the update under it
        time.sleep(0.8)
        keys(q, "s")
        text = screen_all(q.screendump())
        assert "Install the update" in text and "v9.9.9" in text, text
        q.send("\r")                           # the question
        time.sleep(0.5)
        assert "Install bm v9.9.9?" in screen_all(q.screendump())
        q.send("\r")                           # confirm: install
        out = q.expect("Restarting in 1", timeout=60).decode(errors="replace")
        assert "Restarting in 3" in out, out    # counted down
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
    """Settings > Controllers > Pair a new controller starts the stack; QEMU
    has no chip: it says so and B goes back to the menu."""
    q = Qemu(os.path.join(b, "kernel.elf"))
    try:
        boot(q)
        keys(q, "rr")                           # Settings: Controllers
        q.send("\r")
        time.sleep(0.5)
        text = screen_all(q.screendump())
        assert "Settings > Controllers" in text and "Player 1" in text and "built in" in text, text
        keys(q, "ssss")                         # Pair a new controller
        q.send("\r")
        q.expect("no Bluetooth controller in QEMU", timeout=10)
        time.sleep(0.5)
        text = "\n".join(screen_text(q.screendump()))
        assert "Bluetooth is off" in text, text
        q.send("\x7f")                          # back
        time.sleep(0.6)
        text = screen_all(q.screendump())
        assert in_menu(text) and "Pair a new controller" in text, text
    finally:
        q.close()


def test_wifi_page_without_chip(b, opts):
    """Settings > WiFi and network > Connect to a network starts the chip;
    QEMU has none: it says so, B goes back."""
    q = Qemu(os.path.join(b, "kernel.elf"))
    try:
        boot(q)
        keys(q, "rrs")                          # Settings: Controllers, WiFi and network
        q.send("\r")
        time.sleep(0.5)
        keys(q, "ssssss")                       # Connect to a network
        q.send("\r")
        q.expect("no WiFi chip in QEMU", timeout=10)
        time.sleep(0.5)
        text = "\n".join(screen_text(q.screendump()))
        assert "no WiFi chip in QEMU" in text, text
        q.send("\x7f")
        time.sleep(0.6)
        assert in_menu(screen_all(q.screendump()))
    finally:
        q.close()


def test_display_modes(b, opts):
    """The Display page: the modes a game or the GPU can use, each with its
    test image (QEMU shows the image 1:1: the screen takes its size), the
    GPU's layout (rows of 64 bytes); B goes back to the menu's 360x360."""
    q = Qemu(os.path.join(b, "kernel.elf"))
    try:
        boot(q)
        keys(q, "rdd")                          # Dev tab: 3D Bench, Render bench, Display
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
        assert in_menu(screen_all(img))
    finally:
        q.close()


def test_confirm_button(b, opts):
    """B (the RGB30's lower face button) confirms and A goes back by
    default: the hints of the Settings panel say so (Open on one chip,
    Back on the other, swapped by confirm=a in bm/config.txt) and the
    serial port's Enter presses the confirm button."""
    chips = {}
    for config, ok, held in ((b"layout=us\n", "B", "00000020"), (b"confirm=a\n", "A", "00000010")):
        tmp = tempfile.mkdtemp(prefix="bm64sd-")
        sd = make_sd(tmp, {"bm/config.txt": config})     # no games: Dev is one tab away
        q = Qemu(os.path.join(b, "kernel.elf"), sd=sd)
        try:
            boot(q)
            time.sleep(0.4)
            keys(q, "rr")                      # the Settings panel: Open and Back
            img = q.screendump()
            rows = screen_text(img)
            assert "Open" in rows[21] and "Back" in rows[21], "\n".join(rows)
            chips[ok] = (hint_chip(img, rows, "Open"), hint_chip(img, rows, "Back"))
            assert chips[ok][0] != chips[ok][1], "the same chip for Open and Back"
            keys(q, "\x7fs")                   # back to Dev; down a row: Input test
            q.send("\r")                       # confirm
            time.sleep(0.6)
            q.send("\r")                       # Enter: the confirm button, held
            time.sleep(0.05)
            text = screen_all(q.screendump())
            assert "Input test" in text and f"held: {held}" in text, text
        finally:
            q.close()
    assert chips["B"] == chips["A"][::-1], "confirm=a does not swap the chips of Open and Back"


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
        q.expect("bm: loaded", timeout=30)      # the system's splash, then the game
        time.sleep(4)
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
        assert in_menu(screen_all(img))
    finally:
        q.close()


def test_bench3d(b, opts):
    """The Dev tab's 3D Bench (src/bm/b3d.c) at 640x360: every test on the
    ARM, the report on the serial port, in bm/bench on the SD card and in
    bm/reports for GitHub; the back button returns to the menu's 360x360."""
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
        assert in_menu(screen_all(img))
        q.monitor(f'pmemsave {RAMDISK:#x} {size} "{dump}"',
                  until=lambda: os.path.exists(dump) and os.path.getsize(dump) == size)
        time.sleep(0.3)
    finally:
        q.close()
    env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
    rep = subprocess.run(["mtype", "-i", f"{dump}@@{1024 * 1024}", "::/bm/bench/3D0001.TXT"],
                         capture_output=True, env=env, check=True).stdout.decode(errors="replace")
    assert rep.startswith("bm 3D Bench") and "R,spheres,ARM" in rep and "total " in rep, rep
    # and as a report (src/kernel/reports.c): no token here, it waits on the card
    rpt = subprocess.run(["mtype", "-i", f"{dump}@@{1024 * 1024}", "::/bm/reports/RPT00001.TXT"],
                         capture_output=True, env=env, check=True).stdout.decode(errors="replace")
    head = rpt.split("\n\n", 1)[0]
    assert rpt.startswith("bm report\nkind: bench3d\n") and "board: QEMU virt" in head, rpt[:400]
    assert "_bench3d_qemu-virt_" in head and "R,spheres,ARM" in rpt, rpt[:400]


def test_gpu_test(b, opts):
    """Dev > GPU test (M41): on QEMU's virt there is no Mali, the page says
    so and nothing touches the GPU's registers; back returns to the menu."""
    q = Qemu(os.path.join(b, "kernel.elf"))
    try:
        boot(q)
        keys(q, "r")                            # Dev tab: 3D Bench
        keys(q, "ss")                           # the third row: GPU test
        text = screen_all(q.screendump())
        assert "GPU test" in text and in_menu(text), text
        q.send("\r")
        q.expect("no Mali on QEMU virt", timeout=20)
        time.sleep(0.5)
        text = screen_all(q.screendump())
        assert "GPU test" in text and "no Mali on QEMU virt" in text, text
        q.send("\x7f")                          # A: back
        time.sleep(1.5)
        assert in_menu(screen_all(q.screendump()))
    finally:
        q.close()


def test_menu_input_page(b, opts):
    """The Dev tab's input test: the serial port presses buttons."""
    q = Qemu(os.path.join(b, "kernel.elf"))
    try:
        boot(q)
        keys(q, "rs")                           # Dev tab: down a row: Input test
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
        assert in_menu(text) and "Input test" in text, text
    finally:
        q.close()


B16_GAME = r"""
function _init() log("the b16 runs") end
function _draw() cls(1) print("pocket", 8, 8, 7) end
"""


def test_market_b16(b, opts):
    """The Market tab, as on the Pi (src/kernel/market.c in fibers, the
    AArch64 ones of src/rgb30/fiber.S), with a catalog on the SD card
    (market_url=sd:/market/, slowed by market_delay): first, off the screen
    until it is the tab, nothing loads before; of the catalog only the .b16
    (the user's choice for the RGB30); one downloads after a question, goes
    to bm/ as POCKET.B16, plays, and the Games tab has it."""
    tmp = tempfile.mkdtemp(prefix="bm64mk-")
    key, pub = os.path.join(tmp, "key.pem"), os.path.join(tmp, "pub.pem")
    subprocess.run(["openssl", "genpkey", "-algorithm", "EC", "-pkeyopt", "ec_paramgen_curve:P-256",
                    "-out", key], check=True, capture_output=True)
    subprocess.run(["openssl", "pkey", "-in", key, "-pubout", "-out", pub], check=True, capture_output=True)
    games = os.path.join(tmp, "games")
    for gid, name, title in (("pocket", "pocket.b16", "Pocket"), ("bigpi", "bigpi.bm", "Big Pi Game")):
        os.makedirs(os.path.join(games, gid))
        with open(os.path.join(games, gid, name), "wb") as f:
            f.write(mkbm.pack(B16_GAME.encode(), title=title, author="tests"))
        with open(os.path.join(games, gid, "info.txt"), "w") as f:
            f.write("version: 1.0\nlicense: MIT\nabout: A game for the test.\n")
    site = os.path.join(tmp, "site")
    r = subprocess.run([sys.executable, os.path.join(HERE, "..", "..", "scripts", "mkmarket.py"), games,
                        "-o", site, "--key", key, "--serial", "20261005120000"], capture_output=True, text=True)
    assert r.returncode == 0, r.stdout + r.stderr
    files = {"bm/config.txt": b"market_url=sd:/market/\nmarket_delay=100\n", "bm/market.pem": open(pub, "rb").read()}
    for dp, _, fs in os.walk(site):
        for f in fs:
            src = os.path.join(dp, f)
            files["market/" + os.path.relpath(src, site).replace(os.sep, "/")] = open(src, "rb").read()
    sd = make_sd(tmp, files)
    size = os.path.getsize(sd)
    dump = os.path.join(tmp, "after.img")

    def screen(want, gone=(), tries=40):
        for _ in range(tries):
            text = screen_all(q.screendump())
            if all(w in text for w in want) and not any(g in text for g in gone):
                return text
            time.sleep(0.2)
        raise AssertionError(f"want {want}, not {gone}, on the screen:\n{text}")

    q = Qemu(os.path.join(b, "kernel.elf"), sd=sd)
    try:
        boot(q)
        time.sleep(1.0)
        assert b"market:" not in q.buf, "nothing loads before the tab is shown"
        text = screen_all(q.screendump())
        assert "Games" in text and "Market" not in text, text      # off the screen at the left
        keys(q, "l")                            # L1 from Games: the Market
        q.expect("market: catalog 20261005120000", timeout=20)
        text = screen(["Market", "Pocket"])
        assert "Big Pi Game" not in text, text  # the .bm is for the Pi
        keys(q, "\r")                           # confirm: a question first
        screen(["Download Pocket?", "free, license MIT"])
        keys(q, "\r")
        q.expect("market: games/pocket/pocket.b16 -> /bm/POCKET.B16", timeout=20)
        q.expect("market: Pocket installed", timeout=5)
        screen(["Installed", "Play"])
        keys(q, "x")                            # X: its details
        screen(["Market > Pocket", "Play", "Version", "1.0", "License", "MIT"])
        keys(q, "\x7f")                         # back: closed
        screen(["Installed"], gone=["Market > Pocket"])
        keys(q, "\r")                           # confirm: plays it, from the card
        q.expect("play: /bm/POCKET.B16", timeout=10)
        q.expect("the b16 runs", timeout=30)
        q.expect("bm: loaded", timeout=10)     # the system's splash, then the game
        time.sleep(1.0)
        q.send("q")
        q.expect("fps", timeout=10)
        time.sleep(1.0)
        keys(q, "r")                            # the Games tab: the new game
        screen(["Pocket", "Play"])
        q.monitor(f'pmemsave {RAMDISK:#x} {size} "{dump}"',
                  until=lambda: os.path.exists(dump) and os.path.getsize(dump) == size)
        time.sleep(0.3)
    finally:
        q.close()
    env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
    part = f"{dump}@@{1024 * 1024}"
    listing = subprocess.run(["mdir", "-i", part, "-b", "::/bm"], capture_output=True,
                             env=env, check=True).stdout.decode().upper()
    assert "POCKET.B16" in listing and "BIGPI" not in listing, listing
    owned = subprocess.run(["mtype", "-i", part, "::/bm/market/GAMES.TXT"], capture_output=True,
                           env=env, check=True).stdout.decode()
    assert owned.startswith("pocket ") and owned.rstrip().endswith(" /bm/POCKET.B16"), owned


def test_battery_icon(b, opts):
    """The battery at the right end of the bar (the user, 2026-10-05): four
    bars from 75%, fewer below, red under 10%, a bolt on the charger (over
    the outline's top); Settings > System says the charge. Nothing else on
    the bar without a network: no icons of controllers, mice and keyboards
    on the RGB30 (the user, 2026-10-05). QEMU has no battery:
    test_battery=mV[,charger] in bm/config.txt gives one (plat_virt.c)."""
    x0, x1, y0 = SCREEN - 16 - 27, SCREEN - 16, 12        # menu_ui.c: status_icons

    def look(config, settings=False):
        tmp = tempfile.mkdtemp(prefix="bm64bat-")
        q = Qemu(os.path.join(b, "kernel.elf"), sd=make_sd(tmp, {"bm/config.txt": config}))
        try:
            boot(q)
            time.sleep(0.5)
            img = q.screendump()
            white = red = top = 0
            for y in range(y0, y0 + 25):                # left of it: no icon (the pad was there)
                for x in range(x0 - 72, x0 - 4):
                    r, g_, bl = pixel(img, x, y)
                    assert not (r > 200 and g_ > 200 and bl > 200), f"an icon at {x},{y} ({config})"
            for y in range(y0, y0 + 18):
                for x in range(x0, x1):
                    r, g_, bl = pixel(img, x, y)
                    white += r > 200 and g_ > 200 and bl > 200
                    red += r > 200 and g_ < 120 and bl < 120
                    top += y < y0 + 3 and r > 200
            text = ""
            if settings:
                keys(q, "rr")                   # Settings; up from the first row: Shut down,
                keys(q, "www\r")                # Restart, System
                keys(q, "w")                    # its last rows: the battery, the log
                text = screen_all(q.screendump())
            return white, red, top, text
        finally:
            q.close()

    full = look(b"test_battery=4150\n")
    mid = look(b"test_battery=3880\n", settings=True)
    low = look(b"test_battery=3600\n")
    plug = look(b"test_battery=3900,1\n")
    assert full[0] > mid[0] > 0 and not full[1] and not mid[1], (full, mid)       # bars
    assert low[1] > 0 and low[0] == 0, low                                         # red, empty
    assert plug[2] > 0 and not full[2] and not mid[2] and not plug[1], plug        # the bolt
    assert "Battery" in mid[3] and "61%, 3.88 V" in mid[3], mid[3]


TONE_CART = r"""
local t = 0
function _init() note(0, 440, 0, 4, 200) end    -- a sine, held
function _update()
  t = t + 1
  if t == 170 then noteoff(0) end
  if t == 200 then log("tone done") quit() end
end
function _draw() cls(1) end
"""


RIFF_CART = r"""
local R = require "riff"
local t = 0
function _init()
  R.setcpm(60)
  -- a sine A4 held for the whole of each cycle, on the sound's clock
  R.play("tone", R.note "a4" :wave("sine") :sustain(1) :room(0) :legato(1))
end
function _update()
  t = t + 1
  R.update()
  if t == 170 then R.hush() end
  if t == 200 then log("riff done") quit() end
end
function _draw() cls(1) end
"""


def test_riff(b, opts):
    """riff (require "riff", M46) on the RGB30: a pattern of one A4 a cycle,
    queued with play_at, comes out of the console's output at 440 Hz."""
    tmp = tempfile.mkdtemp(prefix="bm64riff-")
    sd = make_sd(tmp, {"bm/config.txt": b"game_intro=0\n",
                       "bm/riff.bm": mkbm.pack(RIFF_CART.encode(), title="Riff", author="tests")})
    q = Qemu(os.path.join(b, "kernel.elf"), sd=sd)
    try:
        boot(q)
        time.sleep(0.5)
        q.send("\r")
        out = q.expect("riff done", timeout=30).decode(errors="replace")
        heard = [int(hz) for hz in re.findall(r"audio: heard (\d+) Hz", out)]
        assert any(436 <= h <= 444 for h in heard), out[-2000:]
        assert "riff:" not in out.replace("riff done", ""), out[-2000:]
    finally:
        q.close()


def test_sound(b, opts):
    """The RGB30's sound (the user, 2026-10-05): the Pi's synthesizer and
    player (src/audio/audio.c) through the console's output; in QEMU the
    sink of src/rgb30/virt_audio.c takes 48 kHz as the I2S does and says
    what it heard. A cartridge's held sine is heard at 440 Hz; the volume
    keys (+ and -, here from the serial port) show their bar, bring it to
    5/10 and the same tone comes out at a quarter of the peak (the gain is
    the square of the level); the level is saved in bm/config.txt;
    Settings > Screen and sound has the Sound, Volume, Bit depth (24-bit by
    default) and Test the sound rows."""
    tmp = tempfile.mkdtemp(prefix="bm64snd-")
    sd = make_sd(tmp, {"bm/config.txt": b"game_intro=0\n",
                       "bm/tone.bm": mkbm.pack(TONE_CART.encode(), title="Tone", author="tests")})
    size = os.path.getsize(sd)
    dump = os.path.join(tmp, "after.img")

    def tone():
        q.send("\r")                            # Games: the only one
        out = q.expect("tone done", timeout=30).decode(errors="replace")
        heard = [(int(hz), int(pk), int(rate)) for hz, pk, rate in
                 re.findall(r"audio: heard (\d+) Hz, peak (\d+), (\d+) samples/s", out)]
        pure = [h for h in heard if 438 <= h[0] <= 442]
        assert pure, out[-2000:]
        assert all(40000 <= h[2] <= 56000 for h in pure), pure         # 48 kHz, as the I2S
        time.sleep(1.0)
        return max(h[1] for h in pure)

    q = Qemu(os.path.join(b, "kernel.elf"), sd=sd)
    try:
        out = boot(q)
        assert "audio: QEMU sink, 48 kHz" in out, out
        time.sleep(0.5)
        full = tone()
        for _ in range(5):
            keys(q, "-", pause=0.15)            # the volume key, five times
        q.expect("volume: 5 / 10", timeout=5)
        text = screen_all(q.screendump())
        assert "Volume" in text and "5 / 10" in text, text      # the bar over the menu
        time.sleep(2.5)                         # the keys at rest: saved
        quarter = tone()
        assert 0.2 * full <= quarter <= 0.3 * full, (full, quarter)
        keys(q, "rr")                           # Settings; down to Screen and sound
        keys(q, "ss\r")
        # its rows longer than the screen since Sound style and 3D on the ARM:
        # seven down to the last (Bit depth before it), Test the sound, the
        # list scrolling with it
        keys(q, "sssssss")
        text = screen_all(q.screendump())
        for want in ("Sound", "Volume", "5 / 10", "Sound style", "Bit depth", "24-bit", "Test the sound"):
            assert want in text, f"{want!r} not in Screen and sound:\n{text}"
        q.monitor(f'pmemsave {RAMDISK:#x} {size} "{dump}"',
                  until=lambda: os.path.exists(dump) and os.path.getsize(dump) == size)
        time.sleep(0.3)
    finally:
        q.close()
    env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
    cfg = subprocess.run(["mtype", "-i", f"{dump}@@{1024 * 1024}", "::/bm/config.txt"], capture_output=True,
                         env=env, check=True).stdout.decode(errors="replace")
    assert "volume=5" in cfg, cfg


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
