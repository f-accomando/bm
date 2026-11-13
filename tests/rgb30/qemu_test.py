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
sys.path.insert(0, os.path.join(HERE, "..", "..", "core", "tools"))
from qemu_test import read_ppm, screen_text, pixel, free_port  # noqa: E402
import bm_load  # noqa: E402
sys.path.insert(0, os.path.join(HERE, "..", "..", "core", "scripts"))
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


RAMDISK = 0x50000000    # core/src/rgb30/sd_virt.c


class Qemu:
    def __init__(self, image, machine="virt,gic-version=3", extra=(), sd=None):
        """sd: an SD card image (core/scripts/mksd.py), loaded as a RAM disk."""
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
    """files: {"bm/name": bytes}; returns the image path. declare: the .bm carts of bm/ and bm/ are made
    for both kernels (declare_both), so the menu starts them with no question; False leaves them as they are."""
    src = []
    for i, (dest, data) in enumerate(files.items()):
        if declare and dest.lower().endswith(".bm") and dest.split("/")[0] in ("bm", "bm"):
            data = declare_both(data)
        path = os.path.join(tmp, f"f{i}")
        with open(path, "wb") as f:
            f.write(data)
        src.append((path, dest))
    out = os.path.join(tmp, "sd.img")
    mksd.build(out, src, size_mib=64)
    return out


# The menu tabs are round icons since 2026-11-06 (df050a96): their names are not on the screen.
TAB_NAMES = ("Market", "Games", "Dev", "Settings")


def _words(want):
    return [w for w in want if w not in TAB_NAMES]


def wait_screen(q, want, tries=80):
    """Poll the screen until all the words of `want` are on it (the menu's row must be there BEFORE Enter)."""
    text = ""
    for _ in range(tries):
        text = screen_all(q.screendump())
        if all(w in text for w in _words(want)):
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
            if all(w in text for w in _words(want)):
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


def down_until(q, want, limit=24):
    """Press the down key until all the words of `want` (a row's help) are on the screen."""
    for _ in range(limit):
        try:
            return press(q, "s", want, tries=1, wait=1.5)
        except AssertionError:
            pass
    raise AssertionError(f"down {limit} times: {want} never came:\n" + screen_all(q.screendump()))


def to_dev(q):
    """From the Games tab: the Dev tab (the installed tools; nothing is built in)."""
    keys(q, "r")
    time.sleep(0.6)
    return screen_all(q.screendump())


def open_settings_screen(q):
    """From the Games tab: the Settings tab, its Screen section opened (the Screen modes row selected)."""
    to_dev(q)                            # the Dev tab
    press(q, "r", ["Controllers", "built in"])  # the Settings tab
    press(q, "s", ["The link, the address"])    # WiFi
    press(q, "s", ["Screen modes, 3D Bench"])   # Screen
    return press(q, "\r", ["Settings > Screen"])


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


def test_smp_cores_up(b, opts):
    """M1 of docs/progress/multicore.md: PSCI CPU_ON brings cores 1-3 up on the same page tables."""
    q = Qemu(os.path.join(b, "kernel.elf"), extra=("-smp", "4"))
    try:
        out = boot(q)
        for core in (1, 2, 3):
            assert f"smp: core {core} up" in out, f"core {core} not up:\n{out}"
        assert "smp: selftest ok (4 cores, 400000)" in out, out    # atomics and a lock, 4 cores
        assert "smp: offload sha256 ok (2 jobs on workers)" in out, out   # M5: the hash of a release file
        assert "smp: raster bands ok" in out, out        # V1: the picture by bands equals the picture by one core
        assert "smp: jobs ok" in out and "jobs on workers)" in out and "0 jobs on workers" not in out, out   # V2: a job on a worker gives the answer of core 0
        assert "smp: core 2 FAULT" in out and "smp: core 3 HUNG" in out, out
        assert "smp: fault test ok" in out, out         # a faulting and a looping job do not stop core 0
        assert "IRQ on: timer 1000 Hz" in out, out      # core 0 goes on to boot
    finally:
        q.close()


def test_sd_map(b, opts):
    """Settings > System > SD map: the scan runs in the menu's free time, the
    panel shows the bar, the legend, the folders and the files the menu cannot show."""
    tmp = tempfile.mkdtemp(prefix="bm64sd-")
    big = bytes(3 << 19)                        # 1.5 MB: wide enough to show in the bar on a 64 MB card
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


def test_menu_games_bm_and_b16(b, opts):
    """The Games tab lists the .bm cartridges and the old .b16 ones in bm/; no key hides any of them
    (show_bm is gone). With no game it says where to put them."""
    tmp = mktmp(prefix="bm64sd-")
    sd = make_sd(tmp, {"bm/racer.b16": b"B16" + bytes(100), "bm/pong.bm": b"BMCART" + bytes(64),
                       "bm/notes.txt": b"hello"})
    q = Qemu(os.path.join(b, "kernel.elf"), sd=sd)
    try:
        out = boot(q)
        assert "SD: FAT32" in out and "ramdisk" in out, out
        assert "cartridge menu: 2 games" in out, out
        time.sleep(0.5)
        text = screen_all(q.screendump())
        first = ("racer.b16" in text.lower(), "pong" in text.lower())
        assert first.count(True) == 1, text         # the carousel shows one title: the first game
        keys(q, "d")
        time.sleep(0.5)
        text2 = screen_all(q.screendump())
        assert ("racer.b16" in text2.lower(), "pong" in text2.lower()) == first[::-1], text2   # the other one
        keys(q, "rr")                           # R1 twice: Settings, its panel over the covers
        text = screen_all(q.screendump())
        assert "Controllers" in text and "WiFi and network" in text and "racer" not in text.lower(), text
    finally:
        q.close()
    sd = make_sd(tmp, {"bm/notes.txt": b"hello"})
    q = Qemu(os.path.join(b, "kernel.elf"), sd=sd)
    try:
        boot(q)
        time.sleep(0.5)
        text = screen_all(q.screendump())
        assert "No games yet" in text and "put .bm games in bm/" in text, text
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
    """Settings > Updates, as on the Pi (core/src/kernel/update.c): a release copied
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
    ca = os.path.join(HERE, "..", "..", "core", "boot", "ca.pem")
    rel = os.path.join(tmp, "release")
    subprocess.run([sys.executable, os.path.join(HERE, "..", "..", "core", "scripts", "mkrelease.py"), rel,
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
        to_dev(q)                        # the Dev tab
        press(q, "r", ["Controllers", "built in"])      # the Settings tab
        press(q, "s", ["The link, the address"])        # WiFi
        press(q, "s", ["Screen modes, 3D Bench"])       # Screen
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
    printed before it (core/src/kernel/crumbs.c). crash() on the serial prompt
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
            # (the sections again, the first row selected: Controllers)
            press(q, "\r", ["Settings > Controllers"])
            down_until(q, ["Every button and both sticks"])     # Test the buttons
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


def test_grey_cart_and_confirm(b, opts):
    """A cartridge that is not made for the RGB30 (an old .bm, or a header that lists the Pi only) is not hidden:
    its cover is faded (no pill, no info row), and the start asks first: the reason is the question
    ('This edition is for the Pi'), with OK and Cancel; back cancels, OK starts it (docs/spec/compat-policy.toml).
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
        assert "Not here" not in text, text                 # no pill on any cover
        keys(q, "d")                                        # Old Pi cart: an old .bm is for the Pi only
        text = wait_screen(q, ["Old Pi cart"])
        keys(q, "x")                                        # X: the options have no 'Not here' row
        text = wait_screen(q, ["Play", "Author"])
        assert "Not here" not in text, text
        keys(q, "y")                                        # back: the covers
        text = wait_screen(q, ["Old Pi cart"])
        assert "Author" not in text, text
        keys(q, "\r")                                       # confirm: the question, the game does not start
        text = wait_screen(q, ["This edition is for the Pi", "OK"])
        assert b"play: /bm/old.bm" not in q.buf, "it started before the answer"
        keys(q, "y")                                        # back: no
        text = wait_screen(q, ["Old Pi cart"])
        assert "This edition is for the Pi" not in text, text
        assert b"play: /bm/old.bm" not in q.buf, "back must not start it"
        keys(q, "d")                                        # Pi only: the header lists the Pi alone
        wait_screen(q, ["Pi only"])
        keys(q, "\r")
        wait_screen(q, ["This edition is for the Pi"])
        keys(q, "\r")                                       # OK: it starts (a faded cover is not a lock)
        q.expect("play: /bm/pionly.bm", timeout=10)
        q.expect("made for the Pi Zero W only, it may not run well here", timeout=10)   # the load-time message
        time.sleep(1.5)
        q.send("q")                                         # Start + Select: back to the menu
        q.expect("suspended", timeout=10)
        time.sleep(1.0)                                     # back in the menu, Pi only is frozen (a star on its cover)
        for _ in range(4):                                  # to Both, a key at a time (one can fall in the return)
            if "bm/both.bm" in screen_all(q.screendump()):
                break
            keys(q, "a", pause=0.6)
        wait_screen(q, ["Both"])                             # Both: made for the RGB30, so only the question about the frozen game
        keys(q, "\r")
        text = wait_screen(q, ["Close Pi only?"])
        assert "This edition is for" not in text, text
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


def test_frozen_game_menu(b, opts):
    """A frozen game in the menu, as on the Pi (D06, D07, D08) and the Games order (D09): the list is sorted by
    title, not by file name; a frozen game has a star, Resume and 'Close the game' in its options;
    starting another game asks 'Close <game>?' first (back keeps the frozen one); deleting the frozen game closes
    it, so the next game starts with no question."""
    tmp = mktmp(prefix="bm64frozen-")
    sd = make_sd(tmp, {"bm/a_zulu.bm": mkbm.pack(PI_CART.encode(), title="Zulu", author="tests", res=(360, 360)),
                       "bm/b_alpha.bm": mkbm.pack(PI_CART.encode(), title="Alpha", author="tests", res=(360, 360))})
    q = Qemu(os.path.join(b, "kernel.elf"), sd=sd)
    try:
        boot(q)
        wait_screen(q, ["Alpha"])               # the first cover is Alpha (b_alpha.bm), not a_zulu.bm: sorted by title
        q.send("\r")                            # the first cover: Alpha
        q.expect("play: /bm/b_alpha.bm", timeout=10)
        q.expect("bm: loaded", timeout=30)
        time.sleep(2)
        q.send("q")                             # Start + Select: frozen, back in the menu
        q.expect("suspended", timeout=10)
        wait_screen(q, ["Resume"])               # Resume for A (the star is on the cover)
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
        wait_screen(q, ["Resume"])
        keys(q, "x")
        wait_screen(q, ["Resume"])
        keys(q, "w")                            # up from the first row: the last row, Delete from the SD card
        wait_screen(q, ["Delete from the SD card"])
        keys(q, "\r")
        wait_screen(q, ["Delete Alpha?"])
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
    """The bottom bar shows the key chips only (2026-10-25): no author, size or path line under the covers, and no
    performance note after a game. The speed of the game still goes to the console log."""
    import shutil
    tmp = tempfile.mkdtemp(prefix="bm64sd-")
    sd = make_sd(tmp, {"bm/picart.bm": mkbm.pack(PI_CART.encode(), title="Pi cart", author="tests",
                                                 res=(360, 360))})
    q = Qemu(os.path.join(b, "kernel.elf"), sd=sd)
    try:
        boot(q)
        time.sleep(0.6)
        text = screen_all(q.screendump())
        assert "Play" in text, text
        assert "KB" not in text and "tests   bm" not in text, "no details line under the covers:" + chr(10) + text
        q.send("\r")
        q.expect("play: /bm/picart.bm", timeout=10)
        q.expect("bm: loaded", timeout=30)
        time.sleep(2)
        q.send("q")
        q.expect("suspended", timeout=10)
        time.sleep(1)
        text = screen_all(q.screendump())
        assert "Resume" in text, "the chip says Resume (the game is frozen):" + chr(10) + text
        assert "fps" not in text and "last:" not in text and "frozen" not in text, \
            "no note under the covers:" + chr(10) + text
    finally:
        q.close()
        shutil.rmtree(tmp, ignore_errors=True)


def test_dev_tab_utilities(b, opts):
    """D52, D53, D15a: the Dev tab holds the installed tools, not the benchmarks and tests: the 3D
    Bench is a row of Settings > Screen, Settings > System > Utilities is hidden now (the Render bench was removed, 28 Oct 2026), and
    Settings > System has the '3D driver' row (the backend text of bm_port.c)."""
    q = Qemu(os.path.join(b, "kernel.elf"))
    try:
        boot(q)
        text = to_dev(q)                       # the Dev tab
        for gone in ("3D Bench", "Render bench", "Input test", "Boot log", "Display"):
            assert gone not in text, f"{gone!r} is not a Dev item:\n{text}"
        press(q, "r", ["Controllers", "built in"])      # the Settings tab
        press(q, "s", ["The link, the address"])        # WiFi
        press(q, "s", ["Screen modes, 3D Bench"])       # Screen
        text = press(q, "\r", ["Settings > Screen"])
        assert "3D Bench" in text and "Screen modes" in text, text
        keys(q, "\x7f")                         # back to the sections: Screen is selected
        down_until(q, ["Version, memory, the log"])             # System
        text = press(q, "\r", ["Settings > System"])
        assert "Benchmarks and tests" not in text, text         # no utility is left: the row is hidden
        down_until(q, ["3D driver"])
        text = wait_screen(q, ["3D driver", "ARM"])
        assert "no GPU driver on the RGB30 yet" in text, text   # its help: gpu3d_status() with mali3d off
    finally:
        q.close()


QUIT_CART = r"""
function _init() log("typed cart runs") quit() end
function _draw() cls(1) end
"""


def test_cart_type_dev(b, opts):
    """D51, as the Pi (carts.c is_dev): a cartridge whose header says type app is not on the Games tab but in Dev,
    and starts with no splash; a game starts with the splash."""
    tmp = mktmp(prefix="bm64sd-")
    app = mkbm.mark_type(mkbm.pack(QUIT_CART.encode(), title="Typed app", author="tests", res=(360, 360)), "app")
    game = mkbm.pack(QUIT_CART.encode(), title="Typed game", author="tests", res=(360, 360))
    sd = make_sd(tmp, {"bm/typedapp.bm": app, "bm/typedgame.bm": game})
    q = Qemu(os.path.join(b, "kernel.elf"), sd=sd)
    try:
        out = boot(q)
        assert "cartridge menu: 1 games" in out, out        # the app is not counted as a game
        text = wait_screen(q, ["Typed game"])
        assert "Typed app" not in text, text
        q.buf = b""
        q.send("\r")                            # the game: with the splash
        line = q.expect("play: /bm/typedgame.bm", timeout=10).decode(errors="replace")
        line += q.expect("\n", timeout=5).decode(errors="replace")
        assert "no splash" not in line, line
        q.expect("typed cart runs", timeout=30)
        wait_screen(q, ["Typed game", "Games", "Dev"])
        text = to_dev(q)                         # Dev: the app
        assert "Typed game" not in text, text
        text = wait_screen(q, ["Typed app", "Play"])   # the app's title on the pill and the Play chip
        assert "tests   app" not in text and "bm/typedapp.bm" not in text, text   # no details line (bottom bar: chips only)
        q.buf = b""
        q.send("\r")
        line = q.expect("play: /bm/typedapp.bm", timeout=10).decode(errors="replace")
        line += q.expect("\n", timeout=5).decode(errors="replace")
        assert "no splash" in line, line
        q.expect("typed cart runs", timeout=30)
        wait_screen(q, ["Games", "Dev", "Settings"])
    finally:
        q.close()
        shutil.rmtree(tmp, ignore_errors=True)


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
    r = subprocess.run([sys.executable, os.path.join(HERE, "..", "..", "core", "scripts", "mkmarket.py"), games,
                        "-o", site, "--key", key, "--serial", "20261005120000"], capture_output=True, text=True)
    assert r.returncode == 0, r.stdout + r.stderr
    files = {"bm/config.txt": b"market_url=sd:/market/\nmarket_delay=100\n" + extra, "bm/market.pem": open(pub, "rb").read()}
    for dp, _, fs in os.walk(site):
        for f in fs:
            src = os.path.join(dp, f)
            files["market/" + os.path.relpath(src, site).replace(os.sep, "/")] = open(src, "rb").read()
    return make_sd(tmp, files)


def test_market_lists_bm_and_b16(b, opts):
    """The Market of the RGB30 lists the .bm cartridges and the old .b16 ones (D16 closed, 26 Oct 2026): a
    cartridge for the Pi has a faded cover and no pill; no config key hides it."""
    tmp = mktmp(prefix="bm64mkbm-")
    q = Qemu(os.path.join(b, "kernel.elf"), sd=market_sd(tmp))
    try:
        boot(q)
        keys(q, "l")
        q.expect("market: catalog 20261005120000", timeout=20)
        for _ in range(40):
            text = screen_all(q.screendump())
            if "Big Pi Game" in text:          # the carousel shows one cover: Big Pi Game comes first
                break
            time.sleep(0.2)
        assert "Big Pi Game" in text and "Not here" not in text, text
        keys(q, "d")
        for _ in range(40):
            text = screen_all(q.screendump())
            if "Pocket" in text:
                break
            time.sleep(0.2)
        assert "Pocket" in text, text
    finally:
        q.close()


TRY_ME = r"""
function _init() log("tryme runs") end
function _draw() cls(1) print("try me", 8, 8, 7) end
"""


def test_game_send(b, opts):
    """A game's options (X) have "Send to a nearby console" (D11, as the Pi): it
    opens the list of the consoles nearby, the shared code of core/src/kernel/market.c;
    with no network the list says so; back returns to the options."""
    tmp = mktmp(prefix="bm64send-")
    sd = make_sd(tmp, {"bm/config.txt": b"game_intro=0\n",
                       "bm/tryme.b16": mkbm.pack(TRY_ME.encode(), title="Try me", author="tests", res=(360, 360))})

    def screen(want, tries=80):
        for _ in range(tries):
            text = screen_all(q.screendump())
            if all(w in text for w in _words(want)):
                return text
            time.sleep(0.25)
        raise AssertionError(f"want {want} on the screen:\n{text}")

    q = Qemu(os.path.join(b, "kernel.elf"), sd=sd)
    try:
        boot(q)
        time.sleep(0.5)
        screen(["Try me"])
        keys(q, "x")                           # X: its options
        screen(["Send to a nearby console", "Play"])
        keys(q, "s")                            # down: Send
        screen(["Send to a nearby console", "To a console on this network"])
        q.send("\r")                           # confirm: the consoles nearby
        screen(["Send > Try me", "This console", "No console nearby yet"])
        keys(q, "x")                            # back: the options
        screen(["Send to a nearby console", "Play"])
    finally:
        q.close()


def test_games_in_bm_only(b, opts):
    """The Games tab lists the games of bm/ and not those of another folder (/old):
    a cartridge sent over the network is in bm/; one left in /old is not listed."""
    tmp = tempfile.mkdtemp(prefix="bm64carts-")
    built_bm = mkbm.pack(TRY_ME.encode(), title="Built in bm", author="tests", res=(360, 360))
    old_bm = mkbm.pack(TRY_ME.encode(), title="Left in old", author="tests", res=(360, 360))
    tryme_b16 = mkbm.pack(TRY_ME.encode(), title="Try me", author="tests", res=(360, 360))
    sd = make_sd(tmp, {"bm/config.txt": b"game_intro=0\n",
                       "bm/tryme.b16": tryme_b16,
                       "bm/built.bm": built_bm,
                       "old/old.bm": old_bm})
    q = Qemu(os.path.join(b, "kernel.elf"), sd=sd)
    try:
        out = boot(q)
        assert "cartridge menu: 2 games" in out, out
        time.sleep(0.5)
        text = screen_all(q.screendump())
        keys(q, "d")
        time.sleep(0.5)
        text += screen_all(q.screendump())
        assert "Built in bm" in text and "Try me" in text and "Left in old" not in text, text
    finally:
        q.close()


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
    low = look(b"test_battery=3500\n")                  # 3%: the curve of 2026-10-27
    plug = look(b"test_battery=3900,1\n")
    assert full[0] > mid[0] > 0 and not full[1] and not mid[1], (full, mid)       # bars
    assert low[1] > 0 and low[0] == 0, low                                         # red, empty
    assert plug[2] > 0 and not full[2] and not mid[2] and not plug[1], plug        # the bolt
    assert "Battery" in mid[3] and "66%, 3.88 V" in mid[3], mid[3]


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
    none with battery_icon=0 (Settings > Screen > Low battery
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

    low = run(b"test_battery=3650\n")                   # 17%
    plug = run(b"test_battery=3650,1\n")
    off = run(b"test_battery=3650\nbattery_icon=0\n")
    assert low[0] == "17 false true" and low[1] > 20, low
    assert plug[0] == "17 true false" and plug[1] == 0, plug
    assert off[0] == "17 false true" and off[1] == 0, off


WIDE_CART = r"""
local t = 0
function _init() screen(720, 405) end
function _update()
  t = t + 1
  if t == 30 then
    local w, h = screen()
    log("wide " .. w .. "x" .. h)
  end
end
function _draw() cls(0x2040C0) end
"""


def test_screen_wide_mode(b, opts):
    """screen(720, 405) on the square panel: the 16:9 picture is
    centred on the 720x720 panel, black above and below. QEMU's ramfb does not scale, so test_panel=1
    makes plat_virt.c scale and centre the image as rk_display.c window() does on the console."""
    tmp = mktmp(prefix="bm64wide-")
    sd = make_sd(tmp, {"bm/config.txt": b"game_intro=0\ntest_panel=1\n",
                       "bm/wide.bm": mkbm.pack(WIDE_CART.encode(), title="Wide", author="tests", res=(360, 360))})
    q = Qemu(os.path.join(b, "kernel.elf"), sd=sd)
    try:
        boot(q)
        time.sleep(0.5)
        q.send("\r")                            # Games: the only one
        q.expect("wide 720x405", timeout=30)
        time.sleep(0.5)
        img = q.screendump()
        w, h, _ = img
        assert (w, h) == (720, 720), (w, h)
        top, bottom = (720 - 405) // 2, (720 - 405) // 2 + 405
        band = pixel(img, 360, 360)
        assert abs(band[0] - 0x20) < 12 and abs(band[1] - 0x40) < 12 and abs(band[2] - 0xC0) < 12, band
        for y in range(h):
            for x in (0, 1, 100, 359, 360, 600, 718, 719):
                p = pixel(img, x, y)
                if top <= y < bottom:
                    assert p == band, f"row {y} x {x}: {p}, the band is {band}"
                else:
                    assert p == (0, 0, 0), f"row {y} x {x}: {p}, outside the band must be black"
    finally:
        q.close()


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


# nnet() (core/src/ai/net.c): a network of two layers (3 inputs, 4 hidden with
# relu, 3 outputs), integer weights so the reference below is exact
NNET_LAYERS = [
    # outputs x inputs, biases, relu, shift, mult
    ([[3, -2, 1], [-1, 4, 2], [2, 2, -3], [5, -1, 0]], [4, -3, 10, 0], 1, 8, 200),
    ([[2, -1, 1, 3], [-2, 3, 1, -1], [1, 1, -2, 2]], [-5, 7, 0], 0, 0, 0),
]
NNET_INPUTS = [(1, 2, 3), (-4, 5, 0), (7, -3, 2), (0, 0, 0), (-2, -6, 9), (30, 40, -50), (1, 1, 5)]


def nnet_blob(layers, nin, in_scale=1.0, out_scale=0.5):
    """the "BMNN" blob of core/src/ai/net.h"""
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
    """the outputs and pick (1-based, the first of the largest) of core/src/ai/net.c
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
