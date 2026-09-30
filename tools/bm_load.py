#!/usr/bin/env python3
"""
bm serial loader: sends kernel.img to a Pi running the bm chainloader,
then acts as a serial terminal. Standard library only (Linux / macOS).

  bm_load.py /dev/ttyUSB0 build/kernel.img          upload + terminal
  bm_load.py tcp:127.0.0.1:4444 build/kernel.img    QEMU serial socket
  bm_load.py /dev/ttyUSB0 --cart game.bm           send a cartridge to a
                                                      running kernel (monitor)

In the terminal, Ctrl-] quits. Whenever the chainloader announces itself
again (e.g. after the monitor 'r' command), the kernel file is re-read and
re-uploaded, so the loop is: edit, make, press 'r'.

Protocol: see chainloader/main.c.
"""
import argparse
import os
import select
import socket
import struct
import sys
import time
import zlib

READY = b"\x03\x03\x03"
MAGIC = b"BMLD"
QUIT_KEY = b"\x1d"  # Ctrl-]


def log(msg):
    sys.stderr.write(f"\r[bm-load] {msg}\r\n")
    sys.stderr.flush()


class Port:
    """Serial device or TCP socket with a minimal read/write API."""

    def __init__(self, spec, baud):
        self.sock = None
        if spec.startswith("tcp:"):
            host, port = spec[4:].rsplit(":", 1)
            deadline = time.time() + 10
            while True:
                try:
                    self.sock = socket.create_connection((host, int(port)))
                    break
                except OSError:
                    if time.time() > deadline:
                        raise
                    time.sleep(0.2)
            self.fd = self.sock.fileno()
        else:
            import termios
            import tty

            self.fd = os.open(spec, os.O_RDWR | os.O_NOCTTY)
            tty.setraw(self.fd)
            attrs = termios.tcgetattr(self.fd)
            speed = getattr(termios, f"B{baud}", None)
            if speed is None:
                raise SystemExit(f"unsupported baud rate {baud}")
            attrs[4] = attrs[5] = speed
            attrs[2] |= termios.CLOCAL | termios.CREAD
            attrs[2] &= ~termios.CRTSCTS if hasattr(termios, "CRTSCTS") else ~0
            termios.tcsetattr(self.fd, termios.TCSANOW, attrs)
            termios.tcflush(self.fd, termios.TCIOFLUSH)

    def fileno(self):
        return self.fd

    def read(self, timeout):
        r, _, _ = select.select([self.fd], [], [], timeout)
        if not r:
            return b""
        data = self.sock.recv(4096) if self.sock else os.read(self.fd, 4096)
        if not data:
            raise EOFError("port closed")
        return data

    def write(self, data):
        view = memoryview(data)
        while view:
            n = self.sock.send(view) if self.sock else os.write(self.fd, view)
            view = view[n:]


class Loader:
    def __init__(self, port, echo=sys.stdout.buffer):
        self.port = port
        self.echo = echo
        self.tail = b""     # recent bytes, to find READY across reads
        self.pending = b""  # bytes received but not yet consumed by a reply wait

    def _feed(self, data):
        """Echoes device output (minus READY markers); True if READY was seen."""
        buf = self.tail + data
        seen = READY in buf
        text = data.replace(b"\x03", b"")
        if text and self.echo:
            self.echo.write(text)
            self.echo.flush()
        self.tail = buf[-(len(READY) - 1):]
        return seen

    def wait_ready(self, timeout=None):
        deadline = None if timeout is None else time.time() + timeout
        while deadline is None or time.time() < deadline:
            if self._feed(self.port.read(0.2)):
                self.tail = b""
                return True
        return False

    def _reply(self, timeout=5.0):
        deadline = time.time() + timeout
        while time.time() < deadline:
            self.pending += self.port.read(0.2).replace(b"\x03", b"")
            if len(self.pending) >= 2:
                r, self.pending = self.pending[:2], self.pending[2:]
                return r
        return b"TO"

    def upload(self, image):
        size, crc = len(image), zlib.crc32(image) & 0xFFFFFFFF
        log(f"sending {size} bytes, crc32 {crc:08x}")
        self.pending = b""
        self.port.write(MAGIC + struct.pack("<II", size, crc))
        r = self._reply()
        if r != b"OK":
            log(f"header rejected: {r!r}")
            return False
        t0 = time.time()
        self.port.write(image)
        r = self._reply(timeout=10 + size / 5000)
        if r != b"OK":
            log(f"transfer failed: {r!r}")
            return False
        log(f"done in {time.time() - t0:.1f}s, kernel starting")
        if self.pending and self.echo:
            self.echo.write(self.pending)
            self.echo.flush()
        self.pending = b""
        return True

    def upload_file(self, path, retries=3):
        for _ in range(retries):
            with open(path, "rb") as f:
                image = f.read()
            if self.upload(image):
                return True
            if not self.wait_ready(timeout=5):
                break
        return False


def terminal(loader, kernel_path):
    import termios
    import tty

    stdin = sys.stdin.fileno()
    old = termios.tcgetattr(stdin) if os.isatty(stdin) else None
    if old:
        tty.setraw(stdin)
    log("terminal: Ctrl-] to quit")
    try:
        while True:
            r, _, _ = select.select([stdin, loader.port], [], [])
            if stdin in r:
                data = os.read(stdin, 1024)
                if not data or QUIT_KEY in data:
                    return
                loader.port.write(data)
            if loader.port in r:
                if loader._feed(loader.port.read(0)) and kernel_path:
                    loader.tail = b""
                    log("chainloader restarted, re-sending kernel")
                    loader.upload_file(kernel_path)
    finally:
        if old:
            termios.tcsetattr(stdin, termios.TCSADRAIN, old)


def send_cart(loader, path, term):
    """Asks the running kernel's monitor to receive a cartridge ('U')."""
    loader.port.write(b"U")
    deadline = time.time() + 5
    buf = b""
    while b"send a .bm" not in buf:
        if time.time() > deadline:
            log("the kernel did not answer (is the monitor prompt active?)")
            return 1
        buf += loader.port.read(0.2)
    with open(path, "rb") as f:
        data = f.read()
    if not loader.upload(data):
        return 1
    if term:
        terminal(loader, None)
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port", help="serial device or tcp:HOST:PORT")
    ap.add_argument("kernel", nargs="?", help="kernel.img to upload")
    ap.add_argument("--cart", help="send this .bm/.cart to a running kernel and play it")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--no-term", action="store_true", help="exit after upload")
    ap.add_argument("--timeout", type=float, default=None,
                    help="seconds to wait for the chainloader (default: forever)")
    args = ap.parse_args()

    port = Port(args.port, args.baud)
    loader = Loader(port)
    if args.cart:
        return send_cart(loader, args.cart, not args.no_term)
    if not args.kernel:
        ap.error("kernel image required (or --cart)")
    log(f"waiting for chainloader on {args.port} (reset the Pi)")
    if not loader.wait_ready(args.timeout):
        log("chainloader not found")
        return 1
    if not loader.upload_file(args.kernel):
        return 1
    if not args.no_term:
        terminal(loader, args.kernel)
    return 0


if __name__ == "__main__":
    sys.exit(main())
