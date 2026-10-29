#!/usr/bin/env python3
"""Client of tools/pi_bridge.py:  python3 tools/pi.py line render | send FILE | play FILE | kernel FILE | shot OUT.png | frame OUT.ppm | keys NAME | logs NAME | status
(keys NAME: monitor, crash-dabt|undef|prefetch, freeze, freeze-irq, reboot; the bridge: tools/pi_bridge.py [--detach] IP)"""
import os
import re
import socket
import sys

if not hasattr(socket, "AF_UNIX"):
    sys.exit("error: needs Unix sockets: run it in WSL (python3 tools/pi.py ...)")
args = sys.argv[1:]
dev = ""
if args[:1] == ["--dev"] and len(args) > 2:
    if not re.fullmatch(r"[a-z0-9]{1,12}", args[1]):
        sys.exit("--dev needs a name of 1-12 letters or digits")
    dev, args = "." + args[1], args[2:]
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.settimeout(320)
try:
    s.connect(os.path.expanduser("~/.pi_bridge%s.sock" % dev))
except OSError:
    sys.exit("the bridge is not running (python3 tools/pi_bridge.py [--dev NAME] IP)")
s.sendall((" ".join(args) + "\n").encode())
data = b""
while True:
    b = s.recv(4096)
    if not b:
        break
    data += b
print(data.decode("utf-8", "replace"))
