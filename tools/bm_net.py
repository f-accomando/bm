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

The password is the one shown on the Pi's screen after 'W' (net_password in
bm/config.txt). In the console keys go to the Pi one by one, as on its
keyboard; Ctrl-Q (or Ctrl-], or Enter ~ . as in ssh) quits. Plain text: use it on the home network only.
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
           b"WE": "could not write on the SD card"}
QUIT_KEYS = (b"\x11", b"\x1d")  # Ctrl-Q, Ctrl-]


def read_until(sock, markers, timeout=5.0):
    """What arrives until one of the markers, the end or the timeout."""
    data = b""
    sock.settimeout(timeout)
    try:
        while not any(m in data for m in markers):
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
    except socket.timeout:
        pass
    return data


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
    pw = password.encode()
    rest = (op + bytes([len(pw)]) + pw + bytes([len(dest)]) + dest.encode()
            + struct.pack("<II", len(data), zlib.crc32(data) & 0xFFFFFFFF))
    # a kernel from before the rename (M28) only knows the old request tag:
    # it answers "bad request", and the same request goes again with that
    for tag in (b"BMXF", b"BM3X"):
        sock = socket.create_connection((args.host, XFER_PORT), timeout=5)
        sock.sendall(tag + rest)
        a = recv_answer(sock, 10)
        if a != b"BH":
            break
        sock.close()
    if a != b"OK":
        print("refused:", ANSWERS.get(a, a or "no answer"))
        return 1
    t0 = time.time()
    sock.settimeout(60)
    step = 64 * 1024
    for off in range(0, len(data), step):
        sock.sendall(data[off:off + step])
        print(f"\r{min(off + step, len(data)) * 100 // len(data):3d}%  {len(data)} bytes", end="", flush=True)
    a = recv_answer(sock, 60)
    dt = time.time() - t0
    print(f"\r{len(data)} bytes in {dt:.1f} s ({len(data) / 1024 / max(dt, 1e-3):.0f} KiB/s): "
          + ANSWERS.get(a, "no answer"))
    sock.close()
    if a == b"OK" and op == b"K":
        return wait_reboot(args.host)
    return 0 if a == b"OK" else 1


def console_version(host):
    """The version in the console's greeting, or None if it does not answer."""
    try:
        with socket.create_connection((host, PORT), timeout=2) as s:
            g = read_until(s, [b"password: "], timeout=3).decode(errors="replace")
    except OSError:
        return None
    m = re.search(r"bm (\S+) network console", g)
    return m.group(1) if m else None


def wait_reboot(host, limit=120):
    """After --kernel: the Pi goes away, then comes back; prints the version."""
    print("waiting for the Pi to restart...", end="", flush=True)
    t0 = time.time()
    went_down = False
    while time.time() - t0 < limit:
        v = console_version(host)
        if v is None:
            went_down = True
        elif went_down:
            print(f"\rback after {time.time() - t0:.0f} s, running bm {v}      ")
            return 0
        time.sleep(2)
    print("\rthe Pi " + ("did not come back" if went_down else "did not restart")
          + f" within {limit} s: look at its screen")
    return 1


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    ap.add_argument("host")
    ap.add_argument("-P", "--port", type=int, default=PORT)
    ap.add_argument("-p", "--password", default=os.environ.get("BM_PASSWORD"))
    ap.add_argument("--send", metavar="FILE", help="save FILE on the SD card (folder --to)")
    ap.add_argument("--to", default="/carts", help="folder for --send (default /carts)")
    ap.add_argument("--name", help="8.3 name on the SD card (default: the file's)")
    ap.add_argument("--play", metavar="FILE", help="play a .bm / .cart at once")
    ap.add_argument("--kernel", metavar="FILE", help="write kernel.img and reboot the Pi")
    args = ap.parse_args()

    jobs = [(b"S", args.send), (b"P", args.play), (b"K", args.kernel)]
    jobs = [(op, f) for op, f in jobs if f]
    if jobs:
        op, path = jobs[0]
        pw = args.password or getpass.getpass("password: ")
        return transfer(args, op, path, args.name or os.path.basename(path), pw)

    import termios
    import tty
    sock = socket.create_connection((args.host, args.port), timeout=5)
    greeting = read_until(sock, [b"password: "])
    sys.stdout.write(greeting.decode(errors="replace").replace("password: ", ""))
    sys.stdout.flush()
    pw = args.password or getpass.getpass("password: ")
    sock.sendall(pw.encode() + b"\r\n")
    answer = read_until(sock, [b"> ", b"password: ", b"bye"])
    sys.stdout.write(answer.decode(errors="replace"))
    sys.stdout.flush()
    if b"ok - " not in answer:
        return 1

    fd = sys.stdin.fileno()
    old = termios.tcgetattr(fd)
    tty.setraw(fd)
    print("(Ctrl-Q or Enter ~ . to leave)\r")
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
