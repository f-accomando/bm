#!/usr/bin/env python3
"""
bm33 network console: the monitor of a Pi running bm33, over WiFi (M18.7).
Standard library only (Linux / macOS / WSL).

  bm33_net.py 192.168.1.108             asks for the password
  bm33_net.py 192.168.1.108 -p 123456   (or BM33_PASSWORD=123456)

The password is the one shown on the Pi's screen after 'W' (net_password in
bm33/config.txt). Keys go to the Pi one by one, as on its keyboard;
Ctrl-] quits. Plain text: use it on the home network only.
"""
import argparse
import getpass
import os
import select
import socket
import sys
import termios
import tty

PORT = 3333
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


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    ap.add_argument("host")
    ap.add_argument("-P", "--port", type=int, default=PORT)
    ap.add_argument("-p", "--password", default=os.environ.get("BM33_PASSWORD"))
    args = ap.parse_args()

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
