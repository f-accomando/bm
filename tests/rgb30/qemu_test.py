#!/usr/bin/env python3
"""
Tests of the RGB30 kernel (AArch64) in QEMU's virt machine: the build with
PLAT=virt (PL011 serial port, ramfb screen, GICv3, generic timer).

  tests/rgb30/qemu_test.py [--build build/rgb30-virt] [-k name]
"""
import argparse
import os
import re
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools"))
from qemu_test import read_ppm, screen_text, pixel, free_port, sdk_template  # noqa: E402
import bm_load  # noqa: E402
sys.path.insert(0, os.path.join(HERE, "..", "..", "scripts"))
import mksd  # noqa: E402
import mkbm  # noqa: E402
import bmaudio  # noqa: E402

QEMU = os.environ.get("QEMU64", "qemu-system-aarch64")

TMP_DIRS = []           # the temporary folders of the running test, removed by main() after it


def mktmp(prefix):
    """A temporary folder (an SD image is 64 MB, and /tmp of WSL is a 3.9 GB tmpfs)."""
    d = tempfile.mkdtemp(prefix=prefix)
    TMP_DIRS.append(d)
    return d


def rm_tmps():
    while TMP_DIRS:
        shutil.rmtree(TMP_DIRS.pop(), ignore_errors=True)

SCREEN = 360           # the menu (made as big as the 720x720 panel on the console)


RAMDISK = 0x50000000    # src/rgb30/sd_virt.c


class Qemu:
    def __init__(self, image, machine="virt,gic-version=3", extra=(), sd=None):
        """sd: an SD card image (scripts/mksd.py), loaded as a RAM disk."""
        self.tmp = mktmp(prefix="bm64-")
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


def declare_both(data):
    """A real .bm (not a stub) with no compatibility block gets one for the Pi and the RGB30: the policy
    (docs/spec/compat-policy.toml) greys an old .bm on the RGB30 and asks before it starts."""
    if len(data) >= 128 and data[:6] == b"BMCART" and not data[18] & 16:        # flag bit 4, BM_FLAG_COMPAT
        return mkbm.mark_compat(data, 3)
    return data


def make_sd(tmp, files, declare=True):
    """files: {"bm/name": bytes}; returns the image path. declare: the .bm carts of bm/ and carts/ are made
    for both kernels (declare_both), so the menu starts them with no question; False leaves them as they are."""
    src = []
    for i, (dest, data) in enumerate(files.items()):
        if declare and dest.lower().endswith(".bm") and dest.split("/")[0] in ("bm", "carts"):
            data = declare_both(data)
        path = os.path.join(tmp, f"f{i}")
        with open(path, "wb") as f:
            f.write(data)
        src.append((path, dest))
    out = os.path.join(tmp, "sd.img")
    mksd.build(out, src, size_mib=64)
    return out


def wait_screen(q, want, tries=80):
    """Poll the screen until all the words of `want` are on it (the menu's row must be there BEFORE Enter)."""
    text = ""
    for _ in range(tries):
        text = screen_all(q.screendump())
        if all(w in text for w in want):
            return text
        time.sleep(0.25)
    raise AssertionError(f"want {want} on the screen:\n{text}")


def press(q, k, want, tries=6, wait=6.0):
    """Send the button `k` and wait until all the words of `want` are on the screen; send it again if they do not
    come within `wait` seconds. The menu reads a serial key as held for PAD_SERIAL_MS (120 ms): a frame
    longer than that, as under load, drops the press, and two presses of one key close together merge. So the test
    waits for the state that the key makes, not for a fixed time. `want` must not be on the screen before the key.
    Returns the text of the screen."""
    text = ""
    for _ in range(tries):
        q.send(k)
        end = time.time() + wait
        while time.time() < end:
            text = screen_all(q.screendump())
            if all(w in text for w in want):
                return text
            time.sleep(0.25)
    raise AssertionError(f"key {k!r}: {want} never came on the screen:\n{text}")


def press_serial(q, k, needle, tries=6, wait=10.0):
    """As press(), for a key whose effect is a line on the serial port; returns the serial text up to `needle`."""
    for _ in range(tries):
        q.send(k)
        try:
            return q.expect(needle, timeout=wait).decode(errors="replace")
        except AssertionError:
            pass
    raise AssertionError(f"key {k!r}: {needle!r} never came on the serial port:\n" + q.buf.decode(errors="replace"))


def walk_until(q, key, needle, limit=120.0):
    """Send `key` (one press every 0.2 s) until `needle` shows on the serial port; returns the serial text seen."""
    end = time.time() + limit
    seen = b""
    while time.time() < end:
        q.send(key)
        time.sleep(0.2)
        try:
            seen += q.expect(needle, timeout=0.1)
            return seen.decode(errors="replace")
        except AssertionError:
            pass
    raise AssertionError(f"{needle!r} not seen in {limit:.0f} s; serial output:\n" + q.buf.decode(errors="replace"))


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
        # (no Lua on the RGB30's Dev tab: the Pi's only; ` on the serial port opens it)
        for k, name in (("r", "3D Bench"), ("s", "Input test"), ("d", "Boot log"), ("d", "GPU test"),
                        ("w", "Display"), ("s", "GPU test")):
            keys(q, k)
            text = screen_all(q.screendump())
            assert name in text and in_menu(text), f"{name!r} not selected on the Dev tab:\n{text}"
        keys(q, "r")                            # Settings: its panel, the Pi's sections (settings.c)
        text = screen_all(q.screendump())
        for needle in ("Controllers", "Controls", "WiFi and network", "Screen and sound", "Updates", "Reports"):   # System is below the sixth row (Controls added one)
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
        assert "GPU test" in text and "Screen and sound" not in text, text
        keys(q, "l")                            # L1: Games
        assert "No games yet" in screen_all(q.screendump())
    finally:
        q.close()


def test_sd_map(b, opts):
    """Settings > System > SD map: the scan runs in the menu's free time, the
    panel shows the bar, the legend, the folders and the files the menu cannot show."""
    tmp = tempfile.mkdtemp(prefix="bm64sd-")
    big = bytes(3 << 19)                        # 1.5 MiB: wide enough to show in the bar on a 64 MiB card
    sd = make_sd(tmp, {"bm/racer.b16": b"B16" + bytes(100), "bme/PROJ.BME": big,
                       "bm/DATA.DAT": big, "bm/NOTES.TXT": b"x" * 3000})
    q = Qemu(os.path.join(b, "kernel.elf"), sd=sd)

    def until(words, keyname, tries=16):
        text = ""
        for _ in range(tries):
            text = screen_all(q.screendump())
            if all(w in text for w in words):
                return text
            keys(q, keyname)
        raise AssertionError(f"{words!r} not on the screen:\n{text}")

    try:
        boot(q)
        time.sleep(0.5)
        keys(q, "rr")                           # Settings
        keys(q, "www")                          # System: round up from the first row
        keys(q, "\r")
        until(["What is on the card"], "s")     # the SD map row
        keys(q, "\r")
        text = until(["Settings > System > SD map", "Kernel", "bm (games)", "bme (projects)", "glb (models)", "bmm (models)"], "s", tries=1)
        until(["Scan", "done"], "s", tries=60)  # the scan ends in the menu's free time
        text = until(["/bm", "/bme", "files"], "s")
        until(["Kernel", "bm (games)", "glb (models)"], "w", tries=20)    # back up to the bar
        img = q.screendump()
        w, h, px = img
        seen = {tuple(px[i:i + 3]) for i in range(0, w * h * 3, 3)}
        for name, c in (("bm", (0x61, 0xAF, 0xEF)), ("bme", (0x98, 0xC3, 0x79)), ("free", (0x4A, 0x50, 0x60))):
            assert any(all(abs(a - b2) <= 8 for a, b2 in zip(c, got)) for got in seen),                 f"bar colour {name} {c} not on the screen"
        until(["Files the menu cannot show"], "s", tries=20)
        keys(q, "\r")
        until(["Settings > System > SD map > Other", "DATA.DAT"], "s", tries=1)
        until(["NOTES.TXT", "2 KB"], "s", tries=6)
    finally:
        q.close()


def test_el2_image_relocation(b, opts):
    """kernel8.img as U-Boot's booti would start it: EL2, x0 = device tree,
    loaded away from the link address (QEMU: above it, at 0x40200000;
    start.S moves it and drops to EL1)."""
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
    tmp = mktmp(prefix="bm64sd-")
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
    tmp = mktmp(prefix="bm64sd-")
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
    mtools before the restart, which reloads it), and the console restarts.
    Both in the menu's background: the panel stays on the screen."""
    tmp = mktmp(prefix="bm64up-")
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
        press(q, "r", ["3D Bench"])             # the Dev tab
        press(q, "r", ["Controllers", "built in"])      # the Settings tab
        press(q, "s", ["Which button does what"])       # Controls
        press(q, "s", ["The link, the address"])        # WiFi
        press(q, "s", ["Screen modes, performance"])    # Screen
        press(q, "s", ["The latest bm from GitHub"])    # Updates
        press(q, "\r", ["Settings > Updates"])
        press(q, "s", ["The latest release on GitHub"])  # Check for updates
        out = press_serial(q, "\r", "releases: ")      # the check starts (the line is printed before the list)
        out += q.expect("can be installed", timeout=60).decode(errors="replace")
        for want in ("releases: sd:/release/", "v9.9.9", "commit abc1234, signed: good"):
            assert want in out, out
        # a build of the sources, or a later commit than a tag (git describe)
        assert "newer than this kernel" in out or "a build of the sources" in out, out
        lines = {l.split()[0]: l for l in out.splitlines() if l.strip().startswith("/")}
        assert lines["/kernel8.img"].endswith("changed") and lines["/bm/ca.pem"].endswith("changed"), lines
        press(q, "s", ["Install the update", "v9.9.9", "Keeps the old kernels"])   # the panel stayed
        press(q, "\r", ["Install bm v9.9.9?"])   # the question
        out = press_serial(q, "\r", "installing v9.9.9")   # confirm: install
        out += q.expect("Restarting in 1", timeout=120).decode(errors="replace")
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


def test_crash_report(b, opts):
    """A crash becomes a report, as on the Pi (tests/qemu_test.py): the red
    screen, after 60 s (and 20 s of uptime) the board restarts with its RAM
    kept, and the next boot files the crash as a report with the lines
    printed before it (src/kernel/crumbs.c). crash() on the serial prompt
    is a BRK. The kernel starts from kernel8.img: QEMU's ELF loader writes
    zeros over .noinit (NOBITS inside the one PT_LOAD) at every reset and the
    crash record would be lost; the raw Image leaves it. So QEMU does not prove
    that the RK3566 keeps its DRAM across the PSCI reset: a check on the
    hardware is still due."""
    tmp = tempfile.mkdtemp(prefix="bm64crash-")
    sd = make_sd(tmp, {"bm/config.txt": b"report_upload=0\n"})
    size = os.path.getsize(sd)
    q = Qemu(os.path.join(b, "kernel8.img"), sd=sd)
    try:
        boot(q)
        time.sleep(21)                          # past the first 20 s of the boot
        lua_prompt(q)
        q.send("crash()\r")
        q.expect("*** EXCEPTION: synchronous exception - BRK (breakpoint) ***", timeout=10)
        q.expect("Restarting in 60 s", timeout=20)
        q.expect("the last run ended in a crash: saved as a report", timeout=120)
        q.expect("rpt00001.txt", timeout=30)
        dump = os.path.join(tmp, "after.img")
        q.monitor(f'pmemsave {RAMDISK:#x} {size} "{dump}"',
                  until=lambda: os.path.exists(dump) and os.path.getsize(dump) == size)
    finally:
        q.close()
    env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
    part = f"{dump}@@{1024 * 1024}"
    rep = subprocess.run(["mtype", "-i", part, "::/bm/reports/rpt00001.txt"], capture_output=True, text=True,
                         env=env).stdout
    shutil.rmtree(tmp, ignore_errors=True)
    head, body = rep.split("\n\n", 1)
    assert "kind: crash" in head.splitlines(), rep[:600]
    assert body.startswith("a crash (the red screen) while:"), body[:300]
    assert "--- the last lines printed ---" in body and "*** EXCEPTION" in body, body[-1500:]


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


def test_y_goes_back(b, opts):
    """Y goes back like A (the back button): a Settings page opened with
    Enter closes with Y, in both confirm settings (the serial port's y is the
    Y button)."""
    for config in (b"layout=us\n", b"confirm=a\n"):
        tmp = tempfile.mkdtemp(prefix="bm64sd-")
        sd = make_sd(tmp, {"bm/config.txt": config})
        q = Qemu(os.path.join(b, "kernel.elf"), sd=sd)
        try:
            boot(q)
            for _ in range(6):                  # to the Settings tab
                if "Controllers" in screen_all(q.screendump()):
                    break
                keys(q, "r", 0.5)
            q.send("\r")                        # Controllers
            time.sleep(0.5)
            text = screen_all(q.screendump())
            assert "Settings > Controllers" in text, text
            q.send("y")                         # back, as A
            time.sleep(0.6)
            text = screen_all(q.screendump())
            assert in_menu(text) and "Settings > Controllers" not in text, (config, text)
        finally:
            q.close()
            shutil.rmtree(tmp, ignore_errors=True)


def test_wifi_page_without_chip(b, opts):
    """Settings > WiFi and network > Connect to a network starts the chip;
    QEMU has none: it says so, B goes back."""
    q = Qemu(os.path.join(b, "kernel.elf"))
    try:
        boot(q)
        press(q, "r", ["every 3D test"])                # the Dev tab
        press(q, "r", ["Controllers", "built in"])      # the Settings tab
        press(q, "s", ["Which button does what"])       # Controls
        press(q, "s", ["The link, the address"])        # WiFi and network
        press(q, "\r", ["Settings > WiFi and network"])
        press(q, "s", ["The link to the access point"])  # State
        press(q, "s", ["Lists the networks, joins"])    # Connect to a network
        press_serial(q, "\r", "no WiFi chip in QEMU")   # the chip starts; the line is printed
        wait_screen(q, ["no WiFi chip in QEMU"])
        text = press(q, "\x7f", ["Games", "Dev", "Settings"])    # B: back to the panel
        assert in_menu(text) and "Settings > WiFi and network" in text, text
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
    """B (the RGB30's lower face button) confirms and Y goes back by
    default: the hints of the Settings panel say so (Open on the B chip, or
    the A chip with confirm=a in bm/config.txt, or what mo= and mc= of controls= set;
    Back on the Y chip) and the
    serial port's Enter presses the confirm button."""
    chips = {}
    for config, ok, held in ((b"layout=us\n", "B", "00000020"), (b"confirm=a\n", "A", "00000010"),
                             (b"controls=mo:x mc:a\n", "X", "00000040")):
        tmp = mktmp(prefix="bm64sd-")
        sd = make_sd(tmp, {"bm/config.txt": config})     # no games: Dev is one tab away
        q = Qemu(os.path.join(b, "kernel.elf"), sd=sd)
        try:
            boot(q)
            time.sleep(0.4)
            for _ in range(6):                 # to the Settings tab
                if "Controllers" in screen_all(q.screendump()):
                    break
                keys(q, "r", 0.5)
            keys(q, "s")
            keys(q, "w")                       # the Settings panel: Open and Back
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
    assert chips["B"][0] != chips["A"][0], "confirm=a does not change the chip of Open"
    assert chips["B"][1] == chips["A"][1], "the chip of Back (Y) is the same in both settings"
    assert chips["X"][0] != chips["B"][0] and chips["X"][1] != chips["B"][1], "controls=mo:x mc:a: the chips of Open (X) and Back (A) change"


def test_controls_menu(b, opts):
    """Settings > Controls (CONTROLS-1): a scheme's actions show the button the
    config sets (controls= in bm/config.txt) or Default with the game's own;
    Right changes the button of the row (X -> Y here) and Reset all (asked)
    puts every action back on Default."""
    def open_scroller(q):
        for _ in range(6):                      # to the Settings tab
            if "Controllers" in screen_all(q.screendump()):
                break
            keys(q, "r", 0.5)
        keys(q, "s")                            # Controllers -> Controls
        q.send("\r")
        time.sleep(0.5)
        text = screen_all(q.screendump())
        assert "Settings > Controls" in text and "Stick games" in text and "Reset all" in text, text
        q.send("\r")                            # Scroller games
        time.sleep(0.5)
        return screen_all(q.screendump())

    tmp = tempfile.mkdtemp(prefix="bm64sd-")
    sd = make_sd(tmp, {"bm/config.txt": b"layout=us\ncontrols=c1:x\n"})
    q = Qemu(os.path.join(b, "kernel.elf"), sd=sd)
    try:
        boot(q)
        text = open_scroller(q)
        assert "Settings > Controls > Scroller games" in text, text
        a1 = [l for l in text.split("\n") if "Action 1" in l]
        a2 = [l for l in text.split("\n") if "Action 2" in l]
        assert a1 and "< X >" in a1[0] and "Default" not in a1[0], text
        assert a2 and "Default (Y)" in a2[0], text
        keys(q, "d")                            # Right: X -> Y on the first row
        text = screen_all(q.screendump())
        a1 = [l for l in text.split("\n") if "Action 1" in l]
        assert a1 and "< Y >" in a1[0], text
        q.send("\x7f")                          # back, then Reset all
        time.sleep(0.4)
        keys(q, "w")                            # up from the first row: the last, Reset all
        q.send("\r")
        time.sleep(0.4)
        assert "Reset all the controls?" in screen_all(q.screendump())
        q.send("\r")                            # yes
        time.sleep(0.4)
        assert "0 changed" in screen_all(q.screendump())
        keys(q, "wwwww")                        # up to Scroller games
        q.send("\r")
        time.sleep(0.4)
        text = screen_all(q.screendump())
        a1 = [l for l in text.split("\n") if "Action 1" in l]
        assert a1 and "Default (B)" in a1[0], text
    finally:
        q.close()


PI_CART = r"""
local started = false
function _update()
  if not started and (btnp(4) or btnp(5)) then started = true log("pi cart started") end
end
function _draw()
  for i = 0, 35 do                        -- 36 bands of colour over the 360x360 screen
    local c = (i * 7 % 256) * 65536 + (255 - i * 7) * 256 + (started and 200 or 40)
    rectfill(i * 10, 0, 10, 360, c)
  end
end
"""


def test_bm_cartridge(b, opts):
    """A Pi cartridge on the RGB30 (listed with no config): a .bm at 360x360,
    RGB565, runs on the AArch64 kernel (QEMU shows its screen 1:1, widened to
    32 bits); its screen, then A reaches the game; 'q' leaves with the frame
    statistics and the menu's 360x360 comes back."""
    tmp = mktmp(prefix="bm64sd-")
    sd = make_sd(tmp, {"bm/picart.bm": mkbm.pack(PI_CART.encode(), title="Pi cart", author="tests",
                                                 res=(360, 360))})
    q = Qemu(os.path.join(b, "kernel.elf"), sd=sd)
    try:
        boot(q)
        time.sleep(0.4)
        assert "picart.bm" in screen_all(q.screendump())
        q.send("\r")                            # confirm: play
        q.expect("play: /bm/picart.bm", timeout=10)
        q.expect("bm: loaded", timeout=30)      # the system's splash, then the game
        time.sleep(2)
        img = q.screendump()
        assert img[0] == 360 and img[1] == 360, f"screen {img[0]}x{img[1]}"
        colours = {img[2][i:i + 3] for i in range(0, len(img[2]), 3 * 97)}
        assert len(colours) > 20, f"{len(colours)} colours: not the cartridge's bands"
        q.send(" ")                              # the game's A
        q.expect("pi cart started", timeout=10)
        time.sleep(2)
        q.send("q")
        # q suspends the game (2026-10-12, as on the Pi): the frame statistics
        # come when it is closed, not here
        out = q.expect("suspended", timeout=10).decode(errors="replace")
        assert '"Pi cart"' in out, out
        time.sleep(1)
        img = q.screendump()
        assert img[0] == SCREEN and img[1] == SCREEN, f"screen {img[0]}x{img[1]}"
        assert in_menu(screen_all(img))
    finally:
        q.close()


def test_grey_cart_and_confirm(b, opts):
    """A cartridge that is not made for the RGB30 (an old .bm, or a header that lists the Pi only) is not hidden:
    its cover is grey with the pill 'Not here', the details line says why, and the start asks first
    ('Start it anyway?', the reason); back cancels, the confirm button starts it (docs/spec/compat-policy.toml).
    A cartridge made for both starts with no question."""
    tmp = mktmp(prefix="bm64grey-")
    old = mkbm.pack(PI_CART.encode(), title="Old Pi cart", author="tests", res=(360, 360))
    both = mkbm.pack(PI_CART.encode(), title="Both", author="tests", res=(360, 360))
    pi_only = mkbm.pack(PI_CART.encode(), title="Pi only", author="tests", res=(360, 360))
    sd = make_sd(tmp, {"bm/old.bm": old, "bm/both.bm": mkbm.mark_compat(both, 3),
                       "bm/pionly.bm": mkbm.mark_compat(pi_only, 1)}, declare=False)
    q = Qemu(os.path.join(b, "kernel.elf"), sd=sd)
    try:
        boot(q)
        text = wait_screen(q, ["Both"])                     # sorted by title: Both, Old Pi cart, Pi only
        assert "Not here:" not in text, text                # the first cover is made for both: no reason
        keys(q, "d")                                        # Old Pi cart: an old .bm is for the Pi only
        text = wait_screen(q, ["Not here: made for the Pi Zero W"])
        keys(q, "\r")                                       # confirm: the question, the game does not start
        text = wait_screen(q, ["Start it anyway?", "made for the Pi Zero W only", "Start"])
        assert b"play: /bm/old.bm" not in q.buf, "it started before the answer"
        keys(q, "y")                                        # back: no
        text = wait_screen(q, ["Old Pi cart"])
        assert "Start it anyway?" not in text, text
        assert b"play: /bm/old.bm" not in q.buf, "back must not start it"
        keys(q, "d")                                        # Pi only: the header lists the Pi alone
        wait_screen(q, ["Not here: made for the Pi Zero W"])
        keys(q, "\r")
        wait_screen(q, ["Start it anyway?"])
        keys(q, "\r")                                       # yes: it starts (grey is not a lock)
        q.expect("play: /bm/pionly.bm", timeout=10)
        q.expect("made for the Pi Zero W only, it may not run well here", timeout=10)   # the load-time message
        time.sleep(1.5)
        q.send("q")                                         # Start + Select: back to the menu
        q.expect("suspended", timeout=10)
        wait_screen(q, ["Playing"])                         # back in the menu, Pi only is frozen
        for _ in range(4):                                  # to Both, a key at a time (one can fall in the return)
            if "bm/both.bm" in screen_all(q.screendump()):
                break
            keys(q, "a", pause=0.6)
        wait_screen(q, ["bm/both.bm"])                      # Both: made for the RGB30, so only the question about the frozen game
        keys(q, "\r")
        text = wait_screen(q, ["Close Pi only?"])
        assert "Start it anyway?" not in text, text
        keys(q, "\r")
        q.expect("play: /bm/both.bm", timeout=10)
    finally:
        q.close()


def test_menu_start_confirms(b, opts):
    """Start confirms in the menu, as on the Pi (carts.c:1474): one cartridge,
    Start (space on the serial line) plays it."""
    tmp = tempfile.mkdtemp(prefix="bm64sd-")
    sd = make_sd(tmp, {"bm/picart.bm": mkbm.pack(PI_CART.encode(), title="Pi cart", author="tests",
                                                 res=(360, 360))})
    q = Qemu(os.path.join(b, "kernel.elf"), sd=sd)
    try:
        boot(q)
        time.sleep(0.4)
        q.send(" ")                             # Start
        q.expect("play: /bm/picart.bm", timeout=10)
    finally:
        q.close()


def test_menu_tab_keys(b, opts):
    """D04, the number keys in the menu as on the Pi (carts.c:1421-1427, 1481): 1 2 3 5 go to Market, Games,
    Dev and Settings; 4 is the Pi's Lib tab, which the RGB30 does not have: nothing happens. (Tab goes to the
    next tab and wraps after the last, from a keyboard only: on the serial line Tab is the Select button, and
    QEMU has no keyboard. tab_of_key() in src/rgb30/ui.c is the one place that maps both.)"""
    q = Qemu(os.path.join(b, "kernel.elf"))

    def tab_now():
        text = screen_all(q.screendump())
        return ("Settings" if "Controllers" in text and "Screen and sound" in text else
                "Dev" if "3D Bench" in text else
                "Games" if "No games yet" in text else
                "Market" if "Market" in text else "?")

    def key_then(k, want, what):
        q.send(k)
        for _ in range(12):
            time.sleep(0.3)
            if tab_now() == want:
                return
        raise AssertionError(f"{what}: wanted {want}, on {tab_now()}:" + chr(10) + screen_all(q.screendump()))
    try:
        boot(q)
        time.sleep(0.5)
        assert tab_now() == "Games", tab_now()
        key_then("3", "Dev", "key 3")
        key_then("5", "Settings", "key 5")
        key_then("1", "Market", "key 1")
        key_then("2", "Games", "key 2")
        key_then("3", "Dev", "key 3 again")
        q.send("4")                             # the Pi's Lib tab: not here
        time.sleep(1.0)
        assert tab_now() == "Dev", "key 4 must do nothing, on " + tab_now()
    finally:
        q.close()


def test_stages_template(b, opts):
    """The SDK's 'stages' template (carts/sdk_b16/main.lua) on the RGB30: a
    Pi cartridge at 360x360, played from the menu with confirm (Enter); ok in the game is Space;
    its title logs the best time, the ok button starts stage 1; 'q' leaves."""
    tmp = mktmp(prefix="bm64sd-")
    sd = make_sd(tmp, {
        "bm/config.txt": b"game_intro=0\n",
        "bm/stages.bm": mkbm.pack(sdk_template("stages").encode(), title="Stages", author="tests",
                                  res=(360, 360)),
    })
    q = Qemu(os.path.join(b, "kernel.elf"), sd=sd)
    try:
        boot(q)
        time.sleep(0.4)
        q.send("\r")                            # confirm: play
        q.expect("play: /bm/stages.bm", timeout=10)
        q.expect("stages: title best -", timeout=30)
        q.send("x")                             # serial x = the X button, which the game reads as "ok": stage 1
        q.expect("stages: stage 1", timeout=10)
        q.send("q")
        q.expect("fps", timeout=10)
    finally:
        q.close()


def test_sdk_stages_build(b, opts):
    """From the bm SDK to the game: Ctrl+N, the ninth template (Stages 2D),
    Ctrl+B and a name build /bm/MYGAME.BM from the project in /bme; its Lua is
    the template's (the 'stages: title best' log)."""
    tmp = mktmp(prefix="bm64sdk-")
    sd = make_sd(tmp, {"bm/config.txt": b"game_intro=0\n"})
    size = os.path.getsize(sd)
    dump = os.path.join(tmp, "after.img")

    q = Qemu(os.path.join(b, "kernel.elf"), sd=sd)
    try:
        boot(q)
        press(q, "r", ["every 3D test"])        # Dev
        press(q, "s", ["both sticks"])          # the second row: Input test
        press(q, "s", ["build and try"])        # the third row: bm SDK
        q.buf = b""
        press_serial(q, "\r", "bm SDK\r\n", tries=3, wait=60.0)
        # Ctrl+N: new project from a template (sent again until the SDK is ready to read it)
        press(q, "\x0e", ["new project from a template", "Stages 2D"], wait=15.0)
        for desc in ("run and jump", "pick up the coins", "waves of enemies", "punches with",
                     "two to four consoles", "floor, cubes", "the models of bm Studio",
                     "three stages"):           # down (arrow) to the ninth row; each row shows its text
            press(q, "\x1b[B", [desc], tries=1, wait=90.0)      # the SDK reads text bytes: none is lost,
        press(q, "\r", ["new project (Stages 2D)"], tries=1, wait=90.0)     # so none is sent again (a second
        press(q, "\x02", ["file name", "mygame.bme"], tries=1, wait=90.0)   # arrow would skip a row)
        press(q, "\r", ["built /bm/mygame.bm"], tries=1, wait=90.0)         # Ctrl+B: Save as, then the name
        q.monitor(f'pmemsave {RAMDISK:#x} {size} "{dump}"',
                  until=lambda: os.path.exists(dump) and os.path.getsize(dump) == size)
        press(q, "\x1c", ["Games", "Dev", "Settings"], wait=10.0)     # Ctrl+\: leave the SDK
    finally:
        q.close()
    env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
    part = f"{dump}@@{1024 * 1024}"
    listing = subprocess.run(["mdir", "-i", part, "-b", "::/BM"], capture_output=True,
                             env=env, check=True).stdout.decode().upper()
    projects = subprocess.run(["mdir", "-i", part, "-b", "::/BME"], capture_output=True,
                              env=env, check=True).stdout.decode().upper()
    assert "MYGAME.BME" in projects and "MYGAME.BM" in listing, (projects, listing)   # project in /bme, game in /bm
    game = subprocess.run(["mtype", "-i", part, "::/BM/MYGAME.BM"], capture_output=True,
                          env=env, check=True).stdout
    assert game[:8] == b"BMCART\x00\x00", game[:16]
    lua = next(body for t, body in mkbm.bmmesh.cart_sections(game) if t == 1)
    assert b"stages: title best" in lua, lua[:200]


def test_frozen_game_menu(b, opts):
    """A frozen game in the menu, as on the Pi (D06, D07, D08) and the Games order (D09): the list is sorted by
    title, not by file name; a frozen game has the 'Playing' badge, Resume and 'Close the game' in its options;
    starting another game asks 'Close <game>?' first (back keeps the frozen one); deleting the frozen game closes
    it, so the next game starts with no question."""
    tmp = mktmp(prefix="bm64frozen-")
    sd = make_sd(tmp, {"bm/a_zulu.bm": mkbm.pack(PI_CART.encode(), title="Zulu", author="tests", res=(360, 360)),
                       "bm/b_alpha.bm": mkbm.pack(PI_CART.encode(), title="Alpha", author="tests", res=(360, 360))})
    q = Qemu(os.path.join(b, "kernel.elf"), sd=sd)
    try:
        boot(q)
        wait_screen(q, ["b_alpha.bm"])          # the first cover is Alpha (b_alpha.bm), not a_zulu.bm: sorted by title
        q.send("\r")                            # the first cover: Alpha
        q.expect("play: /bm/b_alpha.bm", timeout=10)
        q.expect("bm: loaded", timeout=30)
        time.sleep(2)
        q.send("q")                             # Start + Select: frozen, back in the menu
        q.expect("suspended", timeout=10)
        wait_screen(q, ["Playing", "Resume"])    # the badge on the cover, and Resume for A
        keys(q, "x")                            # its options
        wait_screen(q, ["Resume", "Close the game"])
        keys(q, "y")                            # back
        keys(q, "d")                            # Zulu
        wait_screen(q, ["Zulu"])
        keys(q, "\r")                           # another game while Alpha is frozen: the question
        wait_screen(q, ["Close Alpha?", "Close it"])
        keys(q, "y")                            # back: Alpha stays frozen
        text = wait_screen(q, ["Zulu"])
        assert "Close it" not in text, text
        keys(q, "a")                            # Alpha again
        wait_screen(q, ["Playing", "Resume"])
        keys(q, "x")
        wait_screen(q, ["Resume"])
        keys(q, "w")                            # up from the first row: the last row, Delete from the SD card
        wait_screen(q, ["Delete from the SD card"])
        keys(q, "\r")
        wait_screen(q, ["Delete the game?"])
        keys(q, "\r")                           # Delete: the frozen copy goes with the file
        wait_screen(q, ["Zulu"])
        assert "b_alpha" not in screen_all(q.screendump()), "Alpha is gone"
        keys(q, "\r")                           # Zulu starts with no question
        q.expect("play: /bm/a_zulu.bm", timeout=10)
    finally:
        q.close()


def test_tool_not_a_game(b, opts):
    """A tool installed from Market > Apps (a file named like a bm tool) is not a game on the RGB30 (D13), as on
    the Pi (carts.c tool_id_of): bm/studio.b16 stays out of Games, racer.b16 is a game."""
    tmp = mktmp(prefix="bm64tool-")
    sd = make_sd(tmp, {"bm/racer.b16": b"B16" + bytes(100), "bm/studio.b16": b"B16" + bytes(100),
                       "bm/Mesh.BM": b"BMCART" + bytes(64)})
    q = Qemu(os.path.join(b, "kernel.elf"), sd=sd)
    try:
        out = boot(q)
        assert "cartridge menu: 1 games" in out, out
        time.sleep(0.5)
        text = screen_all(q.screendump()).lower()
        assert "racer.b16" in text and "studio" not in text and "mesh" not in text, text
    finally:
        q.close()


def test_cover_details_perf(b, opts):
    """Cover details show author, size, path and performance note after a game (D12)."""
    import shutil
    tmp = tempfile.mkdtemp(prefix="bm64sd-")
    sd = make_sd(tmp, {"bm/picart.bm": mkbm.pack(PI_CART.encode(), title="Pi cart", author="tests",
                                                 res=(360, 360))})
    q = Qemu(os.path.join(b, "kernel.elf"), sd=sd)
    try:
        boot(q)
        time.sleep(0.6)
        text = screen_all(q.screendump())
        assert "tests" in text and "KiB" in text, text
        q.send("\r")
        q.expect("play: /bm/picart.bm", timeout=10)
        q.expect("bm: loaded", timeout=30)
        time.sleep(2)
        q.send("q")
        q.expect("suspended", timeout=10)
        time.sleep(1)
        text = screen_all(q.screendump())
        # the narrow screen shows the note on the details line (cut at 41 characters), as the Pi's "last:" note
        assert re.search(r"last: picart.bm, \d+\.\d fps, update\+draw", text), text
        assert "tests   bm" not in text, "the details wait while the note shows:" + chr(10) + text
        keys(q, "r")                            # Dev: another tab, the note is over
        time.sleep(0.5)
        keys(q, "l")                            # back to Games
        time.sleep(0.6)
        text = screen_all(q.screendump())
        assert "last: picart.bm" not in text, text
        assert "picart.bm: frozen" in text, "the details are back (the game is frozen):" + chr(10) + text
    finally:
        q.close()
        shutil.rmtree(tmp, ignore_errors=True)


def test_bench3d(b, opts):
    """The Dev tab's 3D Bench (src/bm/b3d.c) at 640x360: every test on the
    ARM, the report on the serial port, in bm/bench on the SD card and in
    bm/reports for GitHub; the back button returns to the menu's 360x360."""
    tmp = mktmp(prefix="bm64sd-")
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
    rpt = subprocess.run(["mtype", "-i", f"{dump}@@{1024 * 1024}", "::/bm/reports/rpt00001.txt"],
                         capture_output=True, env=env, check=True).stdout.decode(errors="replace")
    head = rpt.split("\n\n", 1)[0]
    assert rpt.startswith("bm report\nkind: bench3d\n") and "board: QEMU virt" in head, rpt[:400]
    assert "_bench3d_qemu-virt_" in head and "R,spheres,ARM" in rpt, rpt[:400]


def test_gpu_test(b, opts):
    """Dev > GPU test: on QEMU's virt there is no Mali, the page says
    so and nothing touches the GPU's registers; back returns to the menu."""
    q = Qemu(os.path.join(b, "kernel.elf"))
    try:
        boot(q)
        press(q, "r", ["every 3D test"])        # Dev tab: 3D Bench
        press(q, "s", ["both sticks"])          # the second row: Input test
        press(q, "d", ["since boot"])           # Boot log
        text = press(q, "d", ["Mali-G52"])      # GPU test
        assert "GPU test" in text and in_menu(text), text
        press_serial(q, "\r", "no Mali on QEMU virt", wait=30.0)
        wait_screen(q, ["GPU test", "no Mali on QEMU virt"])
        text = press(q, "\x7f", ["Games", "Dev", "Settings"])     # A: back
        assert in_menu(text), text
    finally:
        q.close()


def test_stack_bench(b, opts):
    """Dev > Stack bench (D14, the Pi's bench): sprite stacking at 640x360, the numbers on the serial
    port, the back button returns to the menu's 360x360."""
    q = Qemu(os.path.join(b, "kernel.elf"))
    try:
        boot(q)
        press(q, "r", ["every 3D test"])        # Dev tab
        press(q, "s", ["both sticks"])          # the second row
        press(q, "s", ["build and try"])        # the third row: bm SDK
        press(q, "d", ["us a frame"])           # Stack bench, after it
        q.send("\r")
        q.expect("Stack bench: g16_stack", timeout=30)
        img = q.screendump()
        assert img[0] == 640 and img[1] == 360, f"screen {img[0]}x{img[1]}"
        out = q.expect("Stack bench done", timeout=300).decode(errors="replace")
        assert "slices/s" in out and out.count("128") >= 2, out
        time.sleep(1)
        text = press(q, "\x7f", ["Games", "Dev", "Settings"])     # A: back
        assert in_menu(text), text
    finally:
        q.close()


def test_menu_input_page(b, opts):
    """The Dev tab's input test: the serial port presses buttons."""
    q = Qemu(os.path.join(b, "kernel.elf"))
    try:
        boot(q)
        press(q, "r", ["every 3D test"])        # Dev tab
        press(q, "s", ["both sticks"])          # down a row: Input test
        press(q, "\r", ["held: "])              # the page
        for i in range(60):                     # X is held for 120 ms: a screen taken too early or too late
            q.send("x")                         # misses it (the frame has not read it, or it is let go); send again
            time.sleep(0.03 + 0.01 * (i % 6))
            text = screen_all(q.screendump())
            if "held: 00000040" in text:        # PAD_X
                break
        assert "Input test" in text, text
        assert "held: 00000040" in text, text
        text = press(q, "\t ", ["Games", "Dev", "Settings"])     # Select + Start: back
        assert in_menu(text) and "Input test" in text, text
    finally:
        q.close()


B16_GAME = r"""
function _init() log("the b16 runs") end
function _draw() cls(1) print("pocket", 8, 8, 7) end
"""


def market_sd(tmp, extra=b""):
    """An SD image with a signed catalog of two games (pocket.b16 and bigpi.bm) at sd:/market/; extra: more lines for bm/config.txt."""
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
    files = {"bm/config.txt": b"market_url=sd:/market/\nmarket_delay=100\n" + extra, "bm/market.pem": open(pub, "rb").read()}
    for dp, _, fs in os.walk(site):
        for f in fs:
            src = os.path.join(dp, f)
            files["market/" + os.path.relpath(src, site).replace(os.sep, "/")] = open(src, "rb").read()
    return make_sd(tmp, files)


def test_market_b16(b, opts):
    """The Market tab, as on the Pi (src/kernel/market.c in fibers, the
    AArch64 ones of src/rgb30/fiber.S), with a catalog on the SD card
    (market_url=sd:/market/, slowed by market_delay): first, off the screen
    until it is the tab, nothing loads before; of the catalog only the .b16
    (the user's choice for the RGB30); one downloads after a question, goes
    to bm/ as POCKET.B16, plays, and the Games tab has it."""
    tmp = mktmp(prefix="bm64mk-")
    sd = market_sd(tmp, b"market_bm=0\n")      # the default lists the .bm too, greyed (test_market_bm_key)
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
        q.expect("market: games/pocket/pocket.b16 -> /bm/pocket.b16", timeout=20)
        q.expect("market: Pocket installed", timeout=5)
        screen(["Installed", "Play"])
        keys(q, "x")                            # X: its details
        screen(["Market > Pocket", "Play", "Version", "1.0", "License", "MIT"])
        keys(q, "\x7f")                         # back: closed
        screen(["Installed"], gone=["Market > Pocket"])
        keys(q, "\r")                           # confirm: plays it, from the card
        q.expect("play: /bm/pocket.b16", timeout=10)
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
    assert owned.startswith("pocket ") and owned.rstrip().endswith(" /bm/pocket.b16"), owned


def test_market_bm_key(b, opts):
    """D16 and SYS-8: the Market of the RGB30 lists the .bm cartridges too (greyed by compat-policy.toml); only
    market_bm=0 hides them. show_bm is the Games tab's key and does not touch the Market."""
    for extra, bm_listed in ((b"", True), (b"show_bm=0\n", True), (b"show_bm=1\nmarket_bm=0\n", False)):
        tmp = mktmp(prefix="bm64mkbm-")
        q = Qemu(os.path.join(b, "kernel.elf"), sd=market_sd(tmp, extra))
        try:
            boot(q)
            keys(q, "l")
            q.expect("market: catalog 20261005120000", timeout=20)
            for _ in range(40):
                text = screen_all(q.screendump())
                if "Pocket" in text or "Big Pi Game" in text:      # the carousel shows one cover: Big Pi Game comes first
                    break
                time.sleep(0.2)
            assert "Pocket" in text or "Big Pi Game" in text, text
            assert ("Big Pi Game" in text) == bm_listed, (extra, text)
        finally:
            q.close()


TRY_ME = r"""
function _init() log("tryme runs") end
function _draw() cls(1) print("try me", 8, 8, 7) end
"""


def test_sdk_tool(b, opts):
    """bm SDK (RGB30) (carts/sdk_b16), the tool built into this kernel, as
    the Pi's SDK in its own (src/kernel/devtool.c): a game's options (X)
    open it on the game ("Open in bm SDK"); F5 tries it, the console plays
    it ('q' stops it) and the SDK comes back with the run's numbers;
    Ctrl+\\ (Ctrl+Esc) leaves it for the menu. The Dev tab has it last; it
    opens on the file of the last time."""
    tmp = mktmp(prefix="bm64sdk-")
    sd = make_sd(tmp, {"bm/config.txt": b"game_intro=0\n",
                       "bm/tryme.b16": mkbm.pack(TRY_ME.encode(), title="Try me", author="tests", res=(360, 360))})

    def screen(want, tries=80):
        for _ in range(tries):
            text = screen_all(q.screendump())
            if all(w in text for w in want):
                return text
            time.sleep(0.25)
        raise AssertionError(f"want {want} on the screen:\n{text}")

    q = Qemu(os.path.join(b, "kernel.elf"), sd=sd)
    try:
        boot(q)
        time.sleep(0.5)
        screen(["Try me"])                      # Games: the only one
        keys(q, "x")                            # X: its options
        screen(["Open in bm SDK", "Play"])
        keys(q, "s")                            # down: Open in bm SDK
        q.send("\r")                            # confirm
        q.expect("bm SDK on /bm/", timeout=10)    # (the name as the card has it)
        screen(["PROJECT", "Try me", "project", "code"])   # the SDK's project page, its tabs
        q.send("\x1b[15~")                      # F5: try it (a game: as it is, no copy)
        q.expect("tryme runs", timeout=30)
        time.sleep(1.5)
        q.send("q")                             # the game stops
        out = q.expect('bm: "Try me"', timeout=20).decode(errors="replace")
        assert "stopped with an error" not in out, out
        screen(["back from the game"])          # the SDK again, the run's numbers
        q.send("\x1c")                          # Ctrl+\ (Ctrl+Esc): leave the SDK
        text = screen(["Games", "Dev", "Settings"])
        assert in_menu(text), text
        keys(q, "r")                            # Dev: 3D Bench
        keys(q, "ss")                           # the third row: bm SDK, the last
        text = screen(["bm SDK", "Dev"])
        assert in_menu(text), text
        q.buf = b""
        q.send("\r")
        q.expect("bm SDK\r\n", timeout=10)      # on no file (page_sdk's line)
        screen(["PROJECT", "Try me"])           # the file of the last time
        q.send("\x1c")
        text = screen(["Games", "Dev", "Settings"])
        assert in_menu(text), text
    finally:
        q.close()


def test_game_send(b, opts):
    """A game's options (X) have "Send to a nearby console" (D11, as the Pi): it
    opens the list of the consoles nearby, the shared code of src/kernel/market.c;
    with no network the list says so; back returns to the options."""
    tmp = mktmp(prefix="bm64send-")
    sd = make_sd(tmp, {"bm/config.txt": b"game_intro=0\n",
                       "bm/tryme.b16": mkbm.pack(TRY_ME.encode(), title="Try me", author="tests", res=(360, 360))})

    def screen(want, tries=80):
        for _ in range(tries):
            text = screen_all(q.screendump())
            if all(w in text for w in want):
                return text
            time.sleep(0.25)
        raise AssertionError(f"want {want} on the screen:\n{text}")

    q = Qemu(os.path.join(b, "kernel.elf"), sd=sd)
    try:
        boot(q)
        time.sleep(0.5)
        screen(["Try me"])
        keys(q, "x")                           # X: its options
        screen(["Open in bm SDK", "Play"])
        keys(q, "ss")                           # down: Open in bm SDK, then Send (no Export .b16 for a .b16)
        screen(["Send to a nearby console", "To a console on this network"])
        q.send("\r")                           # confirm: the consoles nearby
        screen(["Send > Try me", "This console", "No console nearby yet"])
        keys(q, "x")                            # back: the options
        screen(["Open in bm SDK", "Play"])
    finally:
        q.close()


def test_games_in_carts(b, opts):
    """The Games tab lists games from both bm/ and /carts (D28): a cartridge
    built by the SDK or placed in /carts appears in the menu alongside bm/."""
    tmp = tempfile.mkdtemp(prefix="bm64carts-")
    built_bm = mkbm.pack(TRY_ME.encode(), title="Built in carts", author="tests", res=(360, 360))
    tryme_b16 = mkbm.pack(TRY_ME.encode(), title="Try me", author="tests", res=(360, 360))
    sd = make_sd(tmp, {"bm/config.txt": b"game_intro=0\n",
                       "bm/tryme.b16": tryme_b16,
                       "carts/built.bm": built_bm})
    q = Qemu(os.path.join(b, "kernel.elf"), sd=sd)
    try:
        out = boot(q)
        assert "cartridge menu: 2 games" in out, out
        time.sleep(0.5)
        text = screen_all(q.screendump())
        keys(q, "d")
        time.sleep(0.5)
        text += screen_all(q.screendump())
        assert "Built in carts" in text and "Try me" in text, text
    finally:
        q.close()
    sd2 = make_sd(tmp, {"bm/config.txt": b"game_intro=0\nshow_bm=0\n",
                        "bm/tryme.b16": tryme_b16,
                        "carts/built.bm": built_bm})
    q = Qemu(os.path.join(b, "kernel.elf"), sd=sd2)
    try:
        out = boot(q)
        assert "cartridge menu: 1 games" in out, out
        out += q.expect("\n", timeout=5).decode(errors="replace")
        assert "(1 .bm hidden by show_bm=0" in out, out
    finally:
        q.close()


def sdk_session(b, tmp, files=None):
    """A QEMU with an SD card for the SDK tests: (qemu, card size); the card is
    read back with sdk_exit_dump."""
    sd = make_sd(tmp, {"bm/config.txt": b"game_intro=0\n", **(files or {})})
    return Qemu(os.path.join(b, "kernel.elf"), sd=sd), os.path.getsize(sd)


def sdk_screen(q, want, tries=80):
    for _ in range(tries):
        text = screen_all(q.screendump())
        if all(w in text for w in want):
            return text
        time.sleep(0.25)
    raise AssertionError(f"want {want} on the screen:\n{text}")


def sdk_open(q):
    """Boot and open bm SDK from the Dev tab (as test_sdk_tool)."""
    boot(q)
    time.sleep(1.0)
    keys(q, "r")                                # Dev
    sdk_screen(q, ["3D Bench", "Dev"])
    keys(q, "ss")                               # the third row: bm SDK
    text = sdk_screen(q, ["bm SDK", "Dev"])
    assert in_menu(text), text
    q.buf = b""
    q.send("\r")
    q.expect("bm SDK\r\n", timeout=10)
    sdk_screen(q, ["PROJECT"])
    time.sleep(0.5)


# In the SDK (it calls keyp(): typing mode) the serial port is a keyboard:
# the pad cannot be pressed on QEMU virt (plat_buttons() is 0), so these tests
# do the same actions with keys. F-keys: ESC O P..S (F1..F4).
F2, F3 = "\x1bOQ", "\x1bOR"
F7 = "\x1b[18~"                                 # ESC [ 18 ~
UP, DOWN, RIGHT, LEFT = "\x1b[A", "\x1b[B", "\x1b[C", "\x1b[D"


def sdk_new_empty(q):
    """Ctrl+N, the first row (Empty 2D)."""
    q.send("\x0e")
    sdk_screen(q, ["new project from a template", "Empty 2D"])
    q.send("\r")
    sdk_screen(q, ["new project (Empty 2D)"])
    time.sleep(0.3)


def sdk_save_default(q):
    """Ctrl+S, Enter (accepts mygame.bme)."""
    q.send("\x13")
    sdk_screen(q, ["file name", "mygame.bme"])
    q.send("\r")
    sdk_screen(q, ["saved /bme/mygame.bme"])


def sdk_exit_dump(q, tmp, size):
    """Ctrl+backslash leaves the SDK, then the SD card's RAM disk to a file (its path)."""
    q.send("\x1c")
    text = sdk_screen(q, ["Games", "Dev", "Settings"])
    assert in_menu(text), text
    dump = os.path.join(tmp, "after.img")
    q.monitor(f'pmemsave {RAMDISK:#x} {size} "{dump}"',
              until=lambda: os.path.exists(dump) and os.path.getsize(dump) == size)
    time.sleep(0.3)
    return dump


def sd_read(dump, path):
    """A file of the dumped SD card (mtools)."""
    env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
    return subprocess.run(["mtype", "-i", f"{dump}@@{1024 * 1024}", "::" + path],
                          capture_output=True, env=env, check=True).stdout


def cart_secs(data):
    """{section type: body} of a cartridge or project."""
    return {t: body for t, body in mkbm.bmmesh.cart_sections(data)}


def sdk_kb_run(b, drive, check):
    """A new Empty 2D project: drive(q), save, leave, check(cart_secs of
    /bme/mygame.bme)."""
    tmp = mktmp(prefix="bm64kb-")
    q, size = sdk_session(b, tmp)
    try:
        sdk_open(q)
        sdk_new_empty(q)
        drive(q)
        sdk_save_default(q)
        dump = sdk_exit_dump(q, tmp, size)
    finally:
        q.close()
    check(cart_secs(sd_read(dump, "/bme/mygame.bme")))


def test_sdk_pad_sprite(b, opts):
    """bm SDK, by keyboard (the serial port): F3 is the sprite editor, space
    paints pixel (0,0) of cell 0 (white); Ctrl+S, leave: the SHEET of the
    saved project. (Same Lua action as the pad's ok, P.sprite.act("paint").)"""
    def drive(q):
        q.send(F3)
        sdk_screen(q, ["SPRITES"])
        time.sleep(0.4)
        q.send(" ")
        time.sleep(0.4)

    def check(secs):
        assert 2 in secs, sorted(secs)
        assert secs[2][4:8] == b"\xff\xff\xff\xff", secs[2][:16].hex()
    sdk_kb_run(b, drive, check)


def test_sdk_pad_map(b, opts):
    """By keyboard: F3 F3 is the map editor, space places tile 1 on cell (0,0)
    of layer 1; the saved project's MAP says so."""
    def drive(q):
        q.send(F3)
        sdk_screen(q, ["SPRITES"])
        time.sleep(0.4)
        q.send(F3)                              # again: the other view
        for _ in range(40):
            if "SPRITES" not in screen_all(q.screendump()):
                break
            time.sleep(0.25)
        else:
            raise AssertionError("still on the sprites after the second F3")
        time.sleep(0.4)
        q.send(" ")
        time.sleep(0.4)

    def check(secs):
        assert 3 in secs, sorted(secs)
        assert struct.unpack_from("<H", secs[3], 4)[0] == 1, secs[3][:16].hex()
    sdk_kb_run(b, drive, check)


def test_sdk_pad_code(b, opts):
    """By keyboard: F2 is the code, 'do end ' is typed at the cursor (the
    start of the template's Lua); the saved project's LUA has it."""
    def drive(q):
        q.send(F2)
        sdk_screen(q, ["my game"])
        time.sleep(0.4)
        for ch in "do end ":
            q.send(ch)
            time.sleep(0.2)
        time.sleep(0.4)

    def check(secs):
        lua = secs[1]
        assert lua.startswith(b"do end -- my game"), lua[:60]
    sdk_kb_run(b, drive, check)


def sdk_heard(q, timeout=8.0):
    """The Hz of the next 'audio: heard N Hz' line of the serial port (the sink
    of virt_audio.c), or None."""
    deadline = time.time() + timeout
    while True:
        m = re.search(rb"audio: heard (\d+) Hz", q.buf)
        if m:
            return int(m.group(1))
        if time.time() > deadline:
            return None
        q.buf += q.port.read(0.1)


def test_sdk_pad_sound(b, opts):
    """By keyboard: F7 is the sounds page, four Right keys take the wave of
    sound 0 from SQUARE to SINE, Enter plays it; the saved project's AUDIO
    has it. (The pad's actions are the same Lua, P.snd.act.)"""
    heard = []

    def drive(q):
        q.send(F7)
        sdk_screen(q, ["SOUNDS"])
        time.sleep(0.4)
        for _ in range(4):
            q.send(RIGHT)
            time.sleep(0.3)
        sdk_screen(q, ["SINE"])
        q.buf = b""
        q.send("\r")
        # the sink says what it heard once a second; the 350 ms preview falls
        # across two of its windows, so its pitch is not 440 Hz in either:
        # that sound reached the sink is what is checked
        heard.append(sdk_heard(q))

    def check(secs):
        assert 6 in secs, sorted(secs)
        bank = bmaudio.unpack(secs[6])
        assert bank["sounds"][0]["wave"] == "sine", bank["sounds"][:1]
    sdk_kb_run(b, drive, check)
    assert heard and heard[0] is not None, "the sink heard nothing of the preview"


def test_sdk_pad_lib(b, opts):
    """By keyboard: F7 is the sounds page, i (the pad's X: Sounds from a file
    (Lib)...) lists the files of bm/lib with a sound bank, Enter takes
    kit.bm and its sound BELL into the first empty slot; the saved
    project's AUDIO has it as sound 0."""
    kit = mkbm.pack(b"", title="Kit", audio=bmaudio.pack({"sounds": [{"name": "BELL", "wave": "metal"}]}))
    tmp = mktmp(prefix="bm64kb-")
    q, size = sdk_session(b, tmp, {"bm/lib/kit.bm": kit})
    try:
        sdk_open(q)
        sdk_new_empty(q)
        q.send(F7)
        sdk_screen(q, ["SOUNDS"])
        time.sleep(0.4)
        q.send("i")
        sdk_screen(q, ["sounds from a file"])
        q.send("\r")                            # the only file: kit.bm
        sdk_screen(q, ["a sound of", "BELL"])
        q.send("\r")                            # its sound BELL
        sdk_screen(q, ["taken from the file"])
        time.sleep(0.4)
        sdk_save_default(q)
        dump = sdk_exit_dump(q, tmp, size)
    finally:
        q.close()
    secs = cart_secs(sd_read(dump, "/bme/mygame.bme"))
    assert 6 in secs, sorted(secs)
    sounds = bmaudio.unpack(secs[6])["sounds"]
    assert sounds[0]["name"] == "BELL" and sounds[0]["wave"] == "metal", sounds[:1]


def test_sdk_pad_sfx(b, opts):
    """By keyboard: F7 F7 is the effects, Enter puts A4 (sound 0, volume 255)
    on step 0 of effect 0; the saved project's AUDIO has it."""
    def drive(q):
        q.send(F7)
        sdk_screen(q, ["SOUNDS"])
        time.sleep(0.4)
        q.send(F7)                              # again: the other view
        sdk_screen(q, ["EFFECTS"])
        time.sleep(0.4)
        q.send("\r")
        time.sleep(0.6)

    def check(secs):
        assert 6 in secs, sorted(secs)
        bank = bmaudio.unpack(secs[6])
        assert bank["sfx"][0]["steps"][0] == bmaudio.step_text(69, 0, 255, 0), bank["sfx"][:1]
    sdk_kb_run(b, drive, check)


def test_sdk_layers_b16(b, opts):
    """R11 in the RGB30 bm SDK (as the Pi's test_sdk_layers): a new Platform 2D
    project (the second template), 4 on the sprite page sets flag 4 of the
    cell, L on the map page adds a layer and space places a tile on it,
    Ctrl+S saves the project with its LAYERS and FLAGS sections."""
    tmp = tempfile.mkdtemp(prefix="bm64kb-")
    q, size = sdk_session(b, tmp)
    try:
        sdk_open(q)
        q.send("\x0e")                          # Ctrl+N: the templates
        sdk_screen(q, ["new project from a template", "Platform 2D"])
        q.send(DOWN)                            # Platform 2D
        time.sleep(0.4)
        q.send("\r")
        sdk_screen(q, ["new project (Platform 2D)"])
        time.sleep(0.3)
        q.send(F3)                              # the sprites, on cell 1
        sdk_screen(q, ["SPRITES"])
        time.sleep(0.4)
        q.send("4")
        sdk_screen(q, ["flag 4 on"])
        q.send(F3)                              # the map
        sdk_screen(q, ["L1/1 main"])
        time.sleep(0.4)
        q.send("L")
        sdk_screen(q, ["map layer 2/2: layer2"])
        time.sleep(0.3)
        q.send(RIGHT)
        time.sleep(0.3)
        q.send(" ")                             # tile 1 at (1, 0) of layer 2
        time.sleep(0.4)
        sdk_save_default(q)
        dump = sdk_exit_dump(q, tmp, size)
    finally:
        q.close()
    secs = cart_secs(sd_read(dump, "/bme/mygame.bme"))
    lay = secs[12]
    w, h, n = struct.unpack_from("<HHH", lay, 0)
    assert n == 2, (w, h, n)
    assert lay[8:24].rstrip(b"\0") == b"main" and lay[24:40].rstrip(b"\0") == b"layer2", lay[8:40]
    cells = lay[8 + 2 * 16:]
    assert struct.unpack_from("<H", cells, 1 * 2)[0] == 1, "tile 1 at (1, 0) of layer 2"
    per, rows = struct.unpack_from("<HH", secs[13], 0)
    fl = secs[13][4:]
    assert per == 32 and fl[1] == 16 and fl[2] == 1 and fl[6] == 2, (per, rows, list(fl[:8]))


def test_sdk_pad_build(b, opts):
    """The whole game with the keyboard (the pad's actions, as the others):
    a pixel, a tile, 'do end ', a sine, Ctrl+S, Ctrl+B builds MYGAME.BM
    next to the project, the menu's Export .b16 puts MYGAME.B16 in bm/. The
    game has the project's LUA, SHEET, MAP and AUDIO."""
    tmp = mktmp(prefix="bm64kb-")
    q, size = sdk_session(b, tmp)
    try:
        sdk_open(q)
        sdk_new_empty(q)
        q.send(F3)                              # the sprites: a white pixel
        sdk_screen(q, ["SPRITES"])
        time.sleep(0.4)
        q.send(" ")
        time.sleep(0.4)
        q.send(F3)                              # the map: tile 1 on (0,0)
        for _ in range(40):
            if "SPRITES" not in screen_all(q.screendump()):
                break
            time.sleep(0.25)
        else:
            raise AssertionError("still on the sprites after the second F3")
        time.sleep(0.4)
        q.send(" ")
        time.sleep(0.4)
        q.send(F2)                              # the code
        sdk_screen(q, ["my game"])
        time.sleep(0.4)
        for ch in "do end ":
            q.send(ch)
            time.sleep(0.2)
        q.send(F7)                              # the sounds: SINE
        sdk_screen(q, ["SOUNDS"])
        time.sleep(0.4)
        for _ in range(4):
            q.send(RIGHT)
            time.sleep(0.3)
        sdk_screen(q, ["SINE"])
        sdk_save_default(q)
        q.send("\x02")                         # Ctrl+B: the game next to the project
        sdk_screen(q, ["built /bm/mygame.bm"])
        q.send("\x1b")                         # Esc: the menu; Export .b16 is the seventh row
        for _ in range(6):
            q.send(DOWN)
            time.sleep(0.3)
        sdk_screen(q, ["Export .b16"])
        q.send("\r")
        sdk_screen(q, [".b16"])
        dump = sdk_exit_dump(q, tmp, size)
    finally:
        q.close()
    project = cart_secs(sd_read(dump, "/bme/mygame.bme"))
    game_bytes = sd_read(dump, "/bm/MYGAME.BM")
    game = cart_secs(game_bytes)
    for t in (1, 2, 3, 6):
        assert t in project, (t, sorted(project))
        assert game.get(t) == project[t], f"section {t} of the game is not the project's"
    assert project[1].startswith(b"do end -- my game"), project[1][:40]
    assert project[2][4:8] == b"\xff\xff\xff\xff", project[2][:16].hex()
    assert struct.unpack_from("<H", project[3], 4)[0] == 1, project[3][:16].hex()
    assert bmaudio.unpack(project[6])["sounds"][0]["wave"] == "sine"
    assert game_bytes[104:112] != bytes(8), game_bytes[96:120]     # the project's name (cart_build)
    env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
    listing = subprocess.run(["mdir", "-i", f"{dump}@@{1024 * 1024}", "-b", "::/bm"], capture_output=True,
                             env=env, check=True).stdout.decode().upper()
    assert "MYGAME.B16" in listing, listing
    b16 = sd_read(dump, "/bm/MYGAME.B16")
    assert struct.unpack_from("<H", b16, 18)[0] & 2, b16[16:24].hex()


def test_battery_icon(b, opts):
    """The battery at the right end of the bar (the user, 2026-10-05): four
    bars from 75%, fewer below, red under 10%, a bolt on the charger (over
    the outline's top); Settings > System says the charge. Nothing else on
    the bar without a network: no icons of controllers, mice and keyboards
    on the RGB30 (the user, 2026-10-05). QEMU has no battery:
    test_battery=mV[,charger] in bm/config.txt gives one (plat_virt.c)."""
    x0, x1, y0 = SCREEN - 16 - 27, SCREEN - 16, 12        # menu_ui.c: status_icons

    def look(config, settings=False):
        tmp = mktmp(prefix="bm64bat-")
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


LOW_CART = r"""
local t = 0
function _init()
  local p, c = battery()
  log("battery " .. tostring(p) .. " " .. tostring(c) .. " " .. tostring(battery_low()))
end
function _update() t = t + 1 if t == 40 then log("battery drawn") end end
function _draw() cls(1) end
"""


def test_battery_low_game(b, opts):
    """The low battery over the games (2026-10-10): battery() gives the
    charge and the charger, battery_low() 20% or less off the charger; while
    low a small red battery top right over the frame, none on the charger,
    none with battery_icon=0 (Settings > Screen and sound > Low battery
    icon), battery_low() still true."""

    def run(config):
        tmp = mktmp(prefix="bm64low-")
        sd = make_sd(tmp, {"bm/config.txt": b"game_intro=0\n" + config,
                           "bm/low.bm": mkbm.pack(LOW_CART.encode(), title="Low", author="tests")})
        q = Qemu(os.path.join(b, "kernel.elf"), sd=sd)
        try:
            boot(q)
            time.sleep(0.5)
            q.send("\r")                        # Games: the only one
            out = q.expect("battery drawn", timeout=30).decode(errors="replace")
            line = out.split("battery ")[1].split("\n")[0].strip()
            time.sleep(0.3)
            img = q.screendump()
            w = img[0]
            red = sum(1 for y in range(0, 16) for x in range(w - 40, w)
                      if (lambda p: p[0] > 200 and p[1] < 120 and p[2] < 120)(pixel(img, x, y)))
            return line, red
        finally:
            q.close()

    low = run(b"test_battery=3650\n")                   # 7%
    plug = run(b"test_battery=3650,1\n")
    off = run(b"test_battery=3650\nbattery_icon=0\n")
    assert low[0] == "7 false true" and low[1] > 20, low
    assert plug[0] == "7 true false" and plug[1] == 0, plug
    assert off[0] == "7 false true" and off[1] == 0, off


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
    tmp = mktmp(prefix="bm64riff-")
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


# nnet() (src/ai/net.c): a network of two layers (3 inputs, 4 hidden with
# relu, 3 outputs), integer weights so the reference below is exact
NNET_LAYERS = [
    # outputs x inputs, biases, relu, shift, mult
    ([[3, -2, 1], [-1, 4, 2], [2, 2, -3], [5, -1, 0]], [4, -3, 10, 0], 1, 8, 200),
    ([[2, -1, 1, 3], [-2, 3, 1, -1], [1, 1, -2, 2]], [-5, 7, 0], 0, 0, 0),
]
NNET_INPUTS = [(1, 2, 3), (-4, 5, 0), (7, -3, 2), (0, 0, 0), (-2, -6, 9), (30, 40, -50), (1, 1, 5)]


def nnet_blob(layers, nin, in_scale=1.0, out_scale=0.5):
    """the "BMNN" blob of src/ai/net.h"""
    import struct
    b = b"BMNN" + struct.pack("<BBHff", 1, len(layers), nin, in_scale, out_scale)
    width = nin
    for w, bias, relu, shift, mult in layers:
        nin4 = (width + 3) & ~3
        b += struct.pack("<HBBi", len(w), relu, shift, mult)
        b += struct.pack(f"<{len(bias)}i", *bias)
        for row in w:
            b += struct.pack(f"<{nin4}b", *(list(row) + [0] * (nin4 - len(row))))
        width = len(w)
    return b


def nnet_ref(layers, x, out_scale=0.5):
    """the outputs and pick (1-based, the first of the largest) of src/ai/net.c
    for integer inputs (input scale 1)"""
    v = [max(-127, min(127, a)) for a in x]
    for w, bias, relu, shift, mult in layers:
        acc = [bk + sum(wi * xi for wi, xi in zip(row, v)) for row, bk in zip(w, bias)]
        if relu:
            v = [0 if a <= 0 else min(127, (a * mult + (1 << (shift - 1))) >> shift) for a in acc]
    out = [a * out_scale for a in acc]
    return out, out.index(max(out)) + 1


def test_nnet(b, opts):
    """nnet() on the RGB30 (2026-10-10: it was a placeholder table there, and
    Overbit's match and benchmark stopped at the bots' network with "attempt
    to call a table value (global 'nnet')"): the network of a blob runs, and
    its outputs and picks are the Pi's (nn.c's plain loops, the same integers
    as the ARMv6 SIMD)."""
    blob = nnet_blob(NNET_LAYERS, 3)
    lua = ['log("nnet is a " .. type(nnet))',
           'local net = nnet("' + "".join(f"\\{c:03d}" for c in blob) + '")',
           "local t = 0", "function _init()"]
    for i, x in enumerate(NNET_INPUTS):
        args = ",".join(str(a) for a in x)
        lua.append(f"  do local o, k = net:run({{{args}}}), net:pick({{{args}}})")
        lua.append(f"    log(string.format('nnet {i} %d %.2f %.2f %.2f', k, o[1], o[2], o[3])) end")
    lua += ["  log('nnet size ' .. table.concat({net:size()}, ' '))", "end",
            "function _update() t = t + 1 if t == 20 then log('nnet done') quit() end end",
            "function _draw() cls(1) end"]
    tmp = mktmp(prefix="bm64nnet-")
    sd = make_sd(tmp, {"bm/config.txt": b"game_intro=0\n",
                       "bm/nnet.bm": mkbm.pack("\n".join(lua).encode(), title="Nnet", author="tests")})
    q = Qemu(os.path.join(b, "kernel.elf"), sd=sd)
    try:
        boot(q)
        time.sleep(0.5)
        q.send("\r")                            # Games: the only one
        out = q.expect("nnet is a ", timeout=30).decode(errors="replace")
        out += q.expect("\n", timeout=5).decode(errors="replace")
        assert "nnet is a function" in out, out[-2000:]
        out += q.expect("nnet done", timeout=30).decode(errors="replace")
    finally:
        q.close()
    assert "stopped with an error" not in out, out[-2000:]
    assert "nnet size 3 3" in out, out[-2000:]
    for i, x in enumerate(NNET_INPUTS):
        o, k = nnet_ref(NNET_LAYERS, x)
        want = f"nnet {i} {k} " + " ".join(f"{a:.2f}" for a in o)
        assert want in out, (want, out[-2000:])


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
    tmp = mktmp(prefix="bm64snd-")
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
        keys(q, "sss\r")
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


def test_stages3d_b16(b, opts):
    """Stages 3D (carts/stages3d, the .b16 edition): the game is on the Games tab, the Dev tab is still reachable,
    A starts it and the keys reach stage 3 with no exception; q leaves for the menu. Needs
    build/carts/stages3d.b16 (make build/carts/stages3d.b16)."""
    cart = os.path.join(os.path.dirname(os.path.normpath(b)), "carts", "stages3d.b16")
    if not os.path.exists(cart):
        print(f"SKIP test_stages3d_b16: {cart} missing (make build/carts/stages3d.b16)")
        return
    tmp = tempfile.mkdtemp(prefix="bm64s3d-")
    sd = make_sd(tmp, {"bm/config.txt": b"game_intro=0\n", "bm/stages3d.b16": open(cart, "rb").read()})
    q = Qemu(os.path.join(b, "kernel.elf"), sd=sd)
    serial = ""
    try:
        out = boot(q)
        assert "cartridge menu: 1 games" in out, out
        time.sleep(0.5)
        wait_screen(q, ["Stages 3D"])
        keys(q, "r")
        text = wait_screen(q, ["3D Bench"])
        assert in_menu(text), text
        keys(q, "l")
        wait_screen(q, ["Stages 3D"])
        q.buf = b""
        q.send("\r")
        serial += q.expect("play: /bm/stages3d.b16", timeout=10).decode(errors="replace")
        serial += q.expect("stages3d: title gpu3d=", timeout=60).decode(errors="replace")
        time.sleep(1)
        img = q.screendump()
        assert img[0] == 360 and img[1] == 360, f"screen {img[0]}x{img[1]}"
        colours = {img[2][i:i + 3] for i in range(0, len(img[2]), 3)}
        assert len(colours) > 3, f"{len(colours)} colours: not the title"
        serial += walk_until(q, " ", "stages3d: stage 1 start", limit=30)
        serial += walk_until(q, "w", "stages3d: stage 2 start", limit=180)
        serial += walk_until(q, "w", "stages3d: stage 3 start", limit=180)
        time.sleep(1.5)
        img = q.screendump()
        colours = {img[2][i:i + 3] for i in range(0, len(img[2]), 3)}
        assert len(colours) > 8, f"{len(colours)} colours: the 3D view is not drawn"
        q.send("q")
        serial += q.expect("suspended", timeout=10).decode(errors="replace")
        time.sleep(1)
        img = q.screendump()
        assert img[0] == SCREEN and img[1] == SCREEN
        assert in_menu(screen_all(img))
        serial += q.buf.decode(errors="replace")
        low = serial.lower()
        for bad in ("exception", "panic", "stopped with an error", "traceback"):
            assert bad not in low, f"{bad!r} in the serial output:\n{serial[-3000:]}"
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
        finally:
            rm_tmps()
    print(f"{len(TESTS) - failed if not opts.k else ''} passed, {failed} failed")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
