#!/usr/bin/env python3
"""
bm network console: the monitor of a Pi running bm, over WiFi (M18.7) or
the Ethernet of a Pi 1 B / B+ (M29).
Standard library only (Linux / macOS / WSL).

  bm_net.py 192.168.1.108             asks for the password
  bm_net.py 192.168.1.108 -p 123456   (or BM_PASSWORD=123456)

  bm_net.py IP --send build/carts/pong.bm   saved on the SD card in /carts
  bm_net.py IP --send x.bm --to /bm   (another folder)
  bm_net.py IP --play game.bm           played at once, not saved
  bm_net.py IP --kernel build/kernel.img written as kernel.img, then reboot
  bm_net.py IP --kernel build/kernel7.img   the same on a Pi Zero 2 W (kernel7.img)
  bm_net.py IP --kernel build/rgb30/kernel8.img   the same on the RGB30 (kernel8.img)
  bm_net.py IP --config                 the console's bm/config.txt (secrets hidden)
  bm_net.py IP --config report_upload=0 game_intro=   a key set, one removed
  bm_net.py IP --line "gpu; b3d; send"  a monitor line run (from the menu or the
                                        monitor), its output shown until it is done

The password is the one shown on the Pi's screen after 'W' (net_password in
bm/config.txt). In the console keys go to the Pi one by one, as on its
keyboard, to what has them (the console says which: the menu takes them
without echo, the monitor echoes; in the menu a line typed after ':' goes to
the monitor); Ctrl-Q (or Ctrl-], or Enter ~ . as in ssh) quits. Plain text:
use it on the home network only.
Files on the SD card need 8.3 names (PONG.BM, not chaos_kitchen.bm):
--name sets another one.
"""
import argparse
import getpass
import os
import re
import select
import socket
import struct
import sys
import time
import zlib

PORT = 3333
XFER_PORT = 3334
ANSWERS = {b"OK": "ok", b"PW": "wrong password", b"SZ": "file too big (or no memory)",
           b"BH": "bad request", b"CE": "damaged in transit (crc)",
           b"WE": "could not write on the SD card",
           b"KV": "not key=value lines: nothing changed",
           b"KA": "not a kernel for this console (the Pi Zero 2 W takes build/kernel7.img, "
                  "the RGB30 build/rgb30/kernel8.img, the other boards build/kernel.img)"}
QUIT_KEYS = (b"\x11", b"\x1d")  # Ctrl-Q, Ctrl-]


def read_until(sock, markers, timeout=5.0):
    """What arrives until one of the markers (or markers(data) is true),
    the end or the timeout."""
    done = markers if callable(markers) else lambda d: any(m in d for m in markers)
    data = b""
    sock.settimeout(timeout)
    try:
        while not done(data):
            chunk = sock.recv(4096)
            if not chunk:
                break
            data += chunk
    except socket.timeout:
        pass
    sock.settimeout(None)
    return data


def recv_answer(sock, timeout):
    sock.settimeout(timeout)
    data = b""
    try:
        while len(data) < 2:
            chunk = sock.recv(2 - len(data))
            if not chunk:
                break
            data += chunk
    except (socket.timeout, ConnectionResetError):
        pass
    return data


def open_request(args, op, dest, data, password):
    """The request sent to the transfer port: the socket and the console's
    first answer (OK: the data can go)."""
    pw = password.encode()
    rest = (op + bytes([len(pw)]) + pw + bytes([len(dest)]) + dest.encode()
            + struct.pack("<II", len(data), zlib.crc32(data) & 0xFFFFFFFF))
    # a kernel from before the rename only knows the old request tag: it
    # answers "bad request" or drops the connection (the rest of the
    # request arrives after it gave up), and the request goes again with that
    for tag in (b"BMXF", b"BM3X"):
        sock = socket.create_connection((args.host, XFER_PORT), timeout=5)
        try:
            sock.sendall(tag + rest)
        except (ConnectionResetError, BrokenPipeError):
            pass
        a = recv_answer(sock, 10)
        if a not in (b"BH", b""):
            break
        sock.close()
        time.sleep(0.3)
    return sock, a


def transfer(args, op, path, name, password):
    try:
        with open(path, "rb") as f:
            data = f.read()
    except OSError as e:
        print(f"{path}: {e.strerror} (the games are in build/carts/, e.g. build/carts/pong.bm)")
        return 1
    if op == b"S" and not re.fullmatch(r"[A-Za-z0-9_~$!#%&'()@^{}-]{1,8}(\.[A-Za-z0-9_~$!#%&'()@^{}-]{1,3})?", name):
        print(f"'{name}' is not an 8.3 name (at most 8 letters, dot, 3): use --name")
        return 1
    dest = (args.to.strip("/") + "/" if args.to.strip("/") else "") + name if op == b"S" else name
    before = console_version(args.host) if op == b"K" else None
    sock, a = open_request(args, op, dest, data, password)
    if a != b"OK":
        print("refused:", ANSWERS.get(a, a or "no answer"))
        return 1
    t0 = time.time()
    sock.settimeout(60)
    step = 64 * 1024
    for off in range(0, len(data), step):
        sock.sendall(data[off:off + step])
        print(f"\r{min(off + step, len(data)) * 100 // len(data):3d}%  {len(data)} bytes", end="", flush=True)
    # the answer comes once the file is written: a console that writes its SD
    # card slowly (the RGB30) needs more than a minute for a kernel
    a = recv_answer(sock, 60 + len(data) // 8192)
    dt = time.time() - t0
    print(f"\r{len(data)} bytes in {dt:.1f} s ({len(data) / 1024 / max(dt, 1e-3):.0f} KiB/s): "
          + ANSWERS.get(a, "no answer"))
    sock.close()
    if a == b"OK" and op == b"K":
        return wait_reboot(args.host)
    if not a and op == b"K":
        # the answer can be lost while the kernel is written all the same
        # (the RGB30 used to drop its WiFi after a long write): what it runs says
        print("the answer was lost: the console may have written the kernel all the same")
        return wait_reboot(args.host, before)
    return 0 if a == b"OK" else 1


def recv_exact(sock, n, timeout):
    sock.settimeout(timeout)
    data = b""
    try:
        while len(data) < n:
            chunk = sock.recv(n - len(data))
            if not chunk:
                break
            data += chunk
    except (socket.timeout, ConnectionResetError):
        pass
    return data


# a setting: the key letters, digits and _ (at most 23), the value one line
# of at most 127 bytes ("key=" removes the key); as src/kernel/config.c
KEY_VALUE = re.compile(r"([A-Za-z0-9_]{1,23})=([^\x00-\x1f\x7f]*)")
SECRET = re.compile(r".*(_token|_psk|_password|_key)$")


def config(args, password):
    """--config: the changes sent (C), the settings after them shown"""
    changes = []
    for kv in args.config:
        m = KEY_VALUE.fullmatch(kv.rstrip(" "))
        if not m or len(m.group(2).encode()) > 127:
            print(f"'{kv}' is not key=value (the key: letters, digits, _, at most 23; the value: "
                  "one line, at most 127 characters)")
            return 1
        changes.append((m.group(1), m.group(2)))
    data = "".join(f"{k}={v}\n" for k, v in changes).encode() if changes else b"#\n"
    sock, a = open_request(args, b"C", "bm/config.txt", data, password)
    if a == b"BH":
        print("refused: this console's kernel does not take settings from the network yet: "
              "send it a newer kernel first (--kernel)")
        return 1
    if a != b"OK":
        print("refused:", ANSWERS.get(a, a or "no answer"))
        return 1
    sock.sendall(data)
    a = recv_answer(sock, 20)
    if a != b"OK":
        sock.close()
        print("refused:", ANSWERS.get(a, a or "no answer"))
        return 1
    head = recv_exact(sock, 4, 10)
    text = recv_exact(sock, struct.unpack("<I", head)[0], 10).decode(errors="replace") if len(head) == 4 else ""
    sock.close()
    now = {}
    for line in text.splitlines():
        k, _, v = line.partition("=")
        now[k] = v
    print("bm/config.txt on the console:")
    for k, v in now.items():
        print(f"  {k}={v}")
    if not now:
        print("  (empty)")
    wrong = []
    for k, v in changes:
        if not v and k in now:
            wrong.append(f"{k} still there")
        elif v and k not in now:
            wrong.append(f"{k} missing (no room for more keys?)")
        elif v and not SECRET.match(k) and now[k] != v:
            wrong.append(f"{k}={now[k]}, not {v}")
    if wrong:
        print("not as asked: " + "; ".join(wrong))
        return 1
    if changes:
        print(f"settings changed: {len(changes)} (some count from the console's next start: WiFi, "
              "the network console's code, the GPU)")
    return 0


def console_version(host):
    """The version in the console's greeting, or None if it does not answer."""
    try:
        with socket.create_connection((host, PORT), timeout=2) as s:
            g = read_until(s, [b"password: "], timeout=3).decode(errors="replace")
    except OSError:
        return None
    m = re.search(r"bm (\S+) network console", g)
    return m.group(1) if m else None


def wait_reboot(host, before=None, limit=120):
    """After --kernel: the console goes away, then comes back; prints the
    version. With before (the version before the kernel was sent), another
    version is also a console that came back (it may have restarted while
    nobody looked)."""
    print("waiting for the console to restart...", end="", flush=True)
    t0 = time.time()
    went_down = False
    while time.time() - t0 < limit:
        v = console_version(host)
        if v is None:
            went_down = True
        elif went_down or (before and v != before):
            print(f"\rback after {time.time() - t0:.0f} s, running bm {v}      ")
            return 0
        time.sleep(2)
    print("\rthe console " + ("did not come back" if went_down else "did not restart")
          + f" within {limit} s: look at its screen")
    return 1


# the end of the login: the monitor's prompt, the password asked again, the
# goodbye, or the line saying what takes the keys (a kernel of 2026-10-05 on)
FOCUS = re.compile(rb"the keys go to ([^\r\n]*)\r\n")


def logged_in(data):
    return any(m in data for m in (b"> ", b"password: ", b"bye")) or FOCUS.search(data) is not None


def run_line(sock, line, answer):
    """--line: ':' + the line sent; what the console prints shown until the
    line is done (or the console goes away: reboot)"""
    m = FOCUS.search(answer)
    where = m.group(1).decode(errors="replace") if m else ""
    if where.startswith("the buttons"):
        print("\n[bm_net] this console has no monitor (its keys are its buttons): no line to run")
        return 2
    if where.startswith("the application") or where.startswith("a page"):
        print(f"\n[bm_net] the keys go to {where}: back to the menu first (Ctrl-Esc on the console)")
        return 2
    if not m:
        print("\n[bm_net] this kernel does not say where the keys go: the line works from its "
              "monitor ('q' in the menu first)")
    sock.sendall(b":" + line.encode() + b"\r")
    seen = b""
    sock.settimeout(None)
    while True:
        data = sock.recv(4096)
        if not data:
            print("\n[bm_net] the console closed the connection")
            return 0
        sys.stdout.write(data.decode(errors="replace"))
        sys.stdout.flush()
        seen = (seen + data)[-64:]
        if b"the line is done" in seen:
            print()
            return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    ap.add_argument("host")
    ap.add_argument("-P", "--port", type=int, default=PORT)
    ap.add_argument("-p", "--password", default=os.environ.get("BM_PASSWORD"))
    ap.add_argument("--send", metavar="FILE", help="save FILE on the SD card (folder --to)")
    ap.add_argument("--to", default="/carts", help="folder for --send (default /carts)")
    ap.add_argument("--name", help="8.3 name on the SD card (default: the file's)")
    ap.add_argument("--play", metavar="FILE", help="play a .bm at once")
    ap.add_argument("--kernel", metavar="FILE",
                    help="write the kernel (kernel.img; kernel7.img on a Pi Zero 2 W, kernel8.img on "
                         "the RGB30) and restart the console")
    ap.add_argument("--line", metavar="LINE",
                    help="run a monitor line (commands separated by ';': gpu, b3d tests=... "
                         "profiles=..., set key=value, send...) and show its output")
    ap.add_argument("--config", nargs="*", metavar="KEY=VALUE",
                    help="the console's bm/config.txt: shown (secrets hidden), KEY=VALUE sets a key, "
                         "KEY= removes it")
    args = ap.parse_args()

    if args.config is not None:
        return config(args, args.password or getpass.getpass("password: "))

    jobs = [(b"S", args.send), (b"P", args.play), (b"K", args.kernel)]
    jobs = [(op, f) for op, f in jobs if f]
    if jobs:
        op, path = jobs[0]
        pw = args.password or getpass.getpass("password: ")
        return transfer(args, op, path, args.name or os.path.basename(path), pw)

    sock = socket.create_connection((args.host, args.port), timeout=5)
    greeting = read_until(sock, [b"password: "])
    sys.stdout.write(greeting.decode(errors="replace").replace("password: ", ""))
    sys.stdout.flush()
    pw = args.password or getpass.getpass("password: ")
    sock.sendall(pw.encode() + b"\r\n")
    answer = read_until(sock, logged_in)
    sys.stdout.write(answer.decode(errors="replace"))
    sys.stdout.flush()
    if b"ok - " not in answer:
        return 1
    if args.line is not None:
        try:
            return run_line(sock, args.line, answer)
        except KeyboardInterrupt:
            print("\n[bm_net] stopped here (the console goes on with the line)")
            return 130
        finally:
            sock.close()

    import termios
    import tty

    fd = sys.stdin.fileno()
    old = termios.tcgetattr(fd)
    tty.setraw(fd)
    print("(Ctrl-Q or Enter ~ . to leave; in the menu a monitor line starts with ':')\r")
    last_enter, tilde = True, False
    try:
        while True:
            r, _, _ = select.select([sock, fd], [], [])
            if sock in r:
                data = sock.recv(4096)
                if not data:
                    break
                os.write(sys.stdout.fileno(), data)
            if fd in r:
                key = os.read(fd, 64)
                if any(q in key for q in QUIT_KEYS):
                    break
                # Enter ~ . (as in ssh): the '~' after Enter is held back
                if tilde:
                    tilde = False
                    if key.startswith(b"."):
                        break
                    key = b"~" + key
                if key == b"~" and last_enter:
                    tilde = True
                    continue
                last_enter = key[-1:] in (b"\r", b"\n")
                sock.sendall(key)
    finally:
        termios.tcsetattr(fd, termios.TCSADRAIN, old)
        sock.close()
    print("\n[bm_net] disconnected")
    return 0


if __name__ == "__main__":
    sys.exit(main())
