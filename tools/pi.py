#!/usr/bin/env python3
"""Client of tools/pi_bridge.py:  python3 tools/pi.py line gpu | send FILE | play FILE | kernel FILE | shot OUT.png"""
import os
import socket
import sys

if not hasattr(socket, "AF_UNIX"):
    sys.exit("error: needs Unix sockets: run it in WSL (python3 tools/pi.py ...)")
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.settimeout(320)
try:
    s.connect(os.path.expanduser("~/.pi_bridge.sock"))
except OSError:
    sys.exit("the bridge is not running (python3 tools/pi_bridge.py 192.168.1.108)")
s.sendall((" ".join(sys.argv[1:]) + "\n").encode())
data = b""
while True:
    b = s.recv(4096)
    if not b:
        break
    data += b
print(data.decode("utf-8", "replace"))
