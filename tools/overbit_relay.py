#!/usr/bin/env python3
"""
overbit_relay.py - the relay for Overbit's matches over the internet (M31.5).

On the LAN the consoles find each other and talk with broadcasts. Over the
internet they all talk to this relay instead (a PC or a small server with a
public address): every packet a console sends to the relay goes to the
other consoles of the same room, as a broadcast would on a LAN.

  overbit_relay.py [--port 47310] [--quiet]

A packet is "OBR1" + the room (4 letters) + the game's own bytes; the relay
forwards the game's bytes, as they are, to everyone else in the room. A
console that is silent for 20 s leaves the room. Nothing else is kept.
"""
import argparse
import socket
import sys
import time


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=47310)
    ap.add_argument("--quiet", action="store_true")
    a = ap.parse_args()
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("0.0.0.0", a.port))
    rooms = {}            # room -> {address: last seen}
    last_clean = time.time()
    if not a.quiet:
        print(f"overbit relay on UDP port {a.port}", flush=True)
    while True:
        s.settimeout(1.0)
        try:
            data, addr = s.recvfrom(2048)
        except socket.timeout:
            data = None
        now = time.time()
        if data and len(data) >= 8 and data[:4] == b"OBR1":
            room = data[4:8]
            members = rooms.setdefault(room, {})
            if addr not in members and not a.quiet:
                print(f"{room.decode(errors='replace')}: {addr[0]}:{addr[1]} joins ({len(members) + 1})", flush=True)
            members[addr] = now
            for other in members:
                if other != addr:
                    s.sendto(data[8:], other)
        if now - last_clean > 5:
            last_clean = now
            for room in list(rooms):
                for m, t in list(rooms[room].items()):
                    if now - t > 20:
                        del rooms[room][m]
                        if not a.quiet:
                            print(f"{room.decode(errors='replace')}: {m[0]}:{m[1]} leaves", flush=True)
                if not rooms[room]:
                    del rooms[room]


if __name__ == "__main__":
    sys.exit(main())
