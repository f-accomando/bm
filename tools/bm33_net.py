#!/usr/bin/env python3
"""
bm33 network console: the monitor of a Pi running bm33, over WiFi (M18.7).
Standard library only (Linux / macOS / WSL).

  bm33_net.py 192.168.1.108             asks for the password
  bm33_net.py 192.168.1.108 -p 123456   (or BM33_PASSWORD=123456)

  bm33_net.py IP --send game.b33           saved on the SD card in /carts
  bm33_net.py IP --send x.b33 --to /bm33   (another folder)
  bm33_net.py IP --play game.b33           played at once, not saved
  bm33_net.py IP --kernel build/kernel.img written as kernel.img, then reboot

The password is the one shown on the Pi's screen after 'W' (net_password in
bm33/config.txt). In the console keys go to the Pi one by one, as on its
keyboard; Ctrl-] quits. Plain text: use it on the home network only.
Files on the SD card need 8.3 names (PONG.B33, not chaos_kitchen.b33):
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
QUIT_KEY = b"\x1d"  # Ctrl-]


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
    with open(path, "rb") as f:
        data = f.read()
    if op == b"S" and not re.fullmatch(r"[A-Za-z0-9_~$!#%&'()@^{}-]{1,8}(\.[A-Za-z0-9_~$!#%&'()@^{}-]{1,3})?", name):
        print(f"'{name}' is not an 8.3 name (at most 8 letters, dot, 3): use --name")
        return 1
    dest = (args.to.strip("/") + "/" if args.to.strip("/") else "") + name if op == b"S" else name
    pw = password.encode()
    req = (b"BM3X" + op + bytes([len(pw)]) + pw + bytes([len(dest)]) + dest.encode()
           + struct.pack("<II", len(data), zlib.crc32(data) & 0xFFFFFFFF))
    sock = socket.create_connection((args.host, XFER_PORT), timeout=5)
    sock.sendall(req)
    a = recv_answer(sock, 10)
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
        print("the Pi restarts with the new kernel")
    return 0 if a == b"OK" else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    ap.add_argument("host")
    ap.add_argument("-P", "--port", type=int, default=PORT)
    ap.add_argument("-p", "--password", default=os.environ.get("BM33_PASSWORD"))
    ap.add_argument("--send", metavar="FILE", help="save FILE on the SD card (folder --to)")
    ap.add_argument("--to", default="/carts", help="folder for --send (default /carts)")
    ap.add_argument("--name", help="8.3 name on the SD card (default: the file's)")
    ap.add_argument("--play", metavar="FILE", help="play a .b33 / .cart at once")
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
                if QUIT_KEY in key:
                    break
                sock.sendall(key)
    finally:
        termios.tcsetattr(fd, termios.TCSADRAIN, old)
        sock.close()
    print("\n[bm33_net] disconnected")
    return 0


if __name__ == "__main__":
    sys.exit(main())
